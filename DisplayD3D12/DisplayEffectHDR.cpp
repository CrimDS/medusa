/*
	DisplayEffectHDR.cpp - D3D12 version
	Bloom post-processing: bright pass → Gaussian blur → additive composite.
	Operates on m_pSceneRT (FXAA intermediate RT) after materials render.
	(c)2024 Palestar
*/

#include "DisplayEffectHDR.h"
#include "Debug/Trace.h"
#include "Standard/Settings.h"		// for bloomScale config read

//---------------------------------------------------------------------------------------------------

IMPLEMENT_FACTORY( DisplayEffectHDRD3D12, DisplayEffect );

//---------------------------------------------------------------------------------------------------

// Post-process constant buffer layout — must match PostProcess.hlsl CBPostProcess
struct CBPostProcess
{
	float	texelSizeX;
	float	texelSizeY;
	float	fScale;
	float	fBrightThreshold;
};

//---------------------------------------------------------------------------------------------------

DisplayEffectHDRD3D12::DisplayEffectHDRD3D12() :
	m_nBloomLevels( 3 ),
	m_nBloomSize( 2 ),		// half-res bloom RT.  Tried full-res (1) — blur kernel is then half as wide in screen space, so the sun's surface-texture detail passed through visibly as wispy dark patches.  Tried quarter-res (4, original) — visible 4x4 blocks.  Half-res with 3 blur passes + linear-clamp sampler at slot 2 is the sweet spot.
	m_fBloomScale( 0.6f ),
	m_fBrightThreshold( 0.65f ),
	m_LastSize( 0, 0 ),
	m_BloomSize( 0, 0 ),
	m_bInitialized( false ),
	m_bBloomFailed( false )
{
	memset( m_nBloomRTVIndex, 0xff, sizeof(m_nBloomRTVIndex) );
	memset( m_nBloomSRVIndex, 0xff, sizeof(m_nBloomSRVIndex) );
}

DisplayEffectHDRD3D12::~DisplayEffectHDRD3D12()
{
	release();
}

//---------------------------------------------------------------------------------------------------

static bool compileShaderEntry( const wchar_t * pPath, const char * pEntry, const char * pTarget,
	ComPtr<ID3DBlob> & blobOut )
{
	UINT flags = 0;
#if defined(_DEBUG)
	flags |= D3DCOMPILE_DEBUG;
#endif

	ComPtr<ID3DBlob> errors;
	HRESULT hr = D3DCompileFromFile( pPath, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		pEntry, pTarget, flags, 0, &blobOut, &errors );
	if ( FAILED(hr) )
	{
		if ( errors )
			TRACE( "Bloom shader compile error (%s): %s", pEntry, (const char *)errors->GetBufferPointer() );
		return false;
	}
	return true;
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectHDRD3D12::initBloom( DisplayDeviceD3D12 * pDevice )
{
	if ( m_bBloomFailed )
		return false;

	RectInt rw = pDevice->renderWindow();
	UINT width  = (UINT)rw.width();
	UINT height = (UINT)rw.height();
	if ( width == 0 || height == 0 )
		return false;

	SizeInt currentSize( width, height );
	if ( m_bInitialized && m_LastSize == currentSize )
		return true;		// already initialized at this size

	// Release old resources
	release();
	m_bBloomFailed = true;		// assume failure until we succeed

	m_LastSize = currentSize;
	m_BloomSize = SizeInt( width / m_nBloomSize, height / m_nBloomSize );
	if ( m_BloomSize.width < 1 ) m_BloomSize.width = 1;
	if ( m_BloomSize.height < 1 ) m_BloomSize.height = 1;

	// Read bloomScale from user config — same key the D3D9 path used and
	// the in-game options slider writes (ViewOptions.cpp:585).  100 = full
	// intensity (1.0); user can dial down for subtler bloom.  Read once at
	// init; changing the slider in-game requires restart to apply (matches
	// other graphics options).
#ifdef _DEBUG
	Settings settings( "ClientD" );
#else
	Settings settings( "Client" );
#endif
	const int nScalePct = settings.get( "bloomScale", 100 );
	m_fBloomScale = Clamp<float>( (float)nScalePct / 100.0f, 0.0f, 1.0f );

	// --- Compile PostProcess.hlsl with different entry points ---
	// Resolve shader path the same way the shader system does
	CharString sPath = DisplayDevice::sm_sShadersPath + "Shaders/PostProcess.hlsl";
	wchar_t wszPath[MAX_PATH];
	MultiByteToWideChar( CP_ACP, 0, sPath, -1, wszPath, MAX_PATH );

	if ( !compileShaderEntry( wszPath, "vs_main", "vs_5_1", m_pVSBlob ) )
	{
		TRACE( "Bloom: Failed to compile VS" );
		return false;
	}
	if ( !compileShaderEntry( wszPath, "PS_BrightPass", "ps_5_1", m_pPSBrightPass ) )
	{
		TRACE( "Bloom: Failed to compile PS_BrightPass" );
		return false;
	}
	if ( !compileShaderEntry( wszPath, "PS_HorzBlur", "ps_5_1", m_pPSHorzBlur ) )
	{
		TRACE( "Bloom: Failed to compile PS_HorzBlur" );
		return false;
	}
	if ( !compileShaderEntry( wszPath, "PS_VertBlur", "ps_5_1", m_pPSVertBlur ) )
	{
		TRACE( "Bloom: Failed to compile PS_VertBlur" );
		return false;
	}
	if ( !compileShaderEntry( wszPath, "PS_Scale", "ps_5_1", m_pPSScale ) )
	{
		TRACE( "Bloom: Failed to compile PS_Scale" );
		return false;
	}

	// --- Create bloom root signature ---
	// [0] CBV at b0 (post-process constants)
	// [1] SRV table: 1 SRV at t0
	// [2] Sampler table: 1 sampler at s0
	D3D12_ROOT_PARAMETER params[3] = {};

	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].Descriptor.ShaderRegister = 0;
	params[0].Descriptor.RegisterSpace = 0;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 1;
	srvRange.BaseShaderRegister = 0;
	srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[1].DescriptorTable.NumDescriptorRanges = 1;
	params[1].DescriptorTable.pDescriptorRanges = &srvRange;
	params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_DESCRIPTOR_RANGE samplerRange = {};
	samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
	samplerRange.NumDescriptors = 1;
	samplerRange.BaseShaderRegister = 0;
	samplerRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[2].DescriptorTable.NumDescriptorRanges = 1;
	params[2].DescriptorTable.pDescriptorRanges = &samplerRange;
	params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters = 3;
	rsDesc.pParameters = params;
	rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> sig, err;
	HRESULT hr = D3D12SerializeRootSignature( &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err );
	if ( FAILED(hr) )
	{
		if ( err ) TRACE( "Bloom root sig error: %s", (const char *)err->GetBufferPointer() );
		return false;
	}

	hr = pDevice->getDevice()->CreateRootSignature( 0, sig->GetBufferPointer(), sig->GetBufferSize(),
		IID_PPV_ARGS(&m_pBloomRootSig) );
	if ( FAILED(hr) )
	{
		TRACE( "Bloom: Failed to create root signature" );
		return false;
	}

	// --- Create PSOs ---
	// Base PSO desc shared by all bloom passes
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_pBloomRootSig.Get();
	psoDesc.VS = { m_pVSBlob->GetBufferPointer(), m_pVSBlob->GetBufferSize() };
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState.DepthClipEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleMask = UINT_MAX;

	// Bright pass PSO
	psoDesc.PS = { m_pPSBrightPass->GetBufferPointer(), m_pPSBrightPass->GetBufferSize() };
	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pBrightPassPSO) );
	if ( FAILED(hr) ) { TRACE( "Bloom: Failed to create BrightPass PSO" ); return false; }

	// Horz blur PSO
	psoDesc.PS = { m_pPSHorzBlur->GetBufferPointer(), m_pPSHorzBlur->GetBufferSize() };
	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pHorzBlurPSO) );
	if ( FAILED(hr) ) { TRACE( "Bloom: Failed to create HorzBlur PSO" ); return false; }

	// Vert blur PSO
	psoDesc.PS = { m_pPSVertBlur->GetBufferPointer(), m_pPSVertBlur->GetBufferSize() };
	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pVertBlurPSO) );
	if ( FAILED(hr) ) { TRACE( "Bloom: Failed to create VertBlur PSO" ); return false; }

	// Additive composite PSO (writes bloom onto scene RT with additive blending).
	// Scene RT is 10-bit (DisplayDeviceD3D12::SCENE_RT_FORMAT) — PSO RTV format
	// must match exactly, the bright/blur passes above target the 8-bit bloom RT.
	psoDesc.PS = { m_pPSScale->GetBufferPointer(), m_pPSScale->GetBufferSize() };
	psoDesc.RTVFormats[0] = DisplayDeviceD3D12::SCENE_RT_FORMAT;
	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pAdditivePSO) );
	if ( FAILED(hr) ) { TRACE( "Bloom: Failed to create Additive PSO" ); return false; }

	// --- Create bloom render targets (2 ping-pong textures at 1/N screen size) ---
	D3D12_RESOURCE_DESC rtDesc = {};
	rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rtDesc.Width = m_BloomSize.width;
	rtDesc.Height = m_BloomSize.height;
	rtDesc.DepthOrArraySize = 1;
	rtDesc.MipLevels = 1;
	rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtDesc.SampleDesc.Count = 1;
	rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	for ( int i = 0; i < 2; ++i )
	{
		hr = pDevice->getDevice()->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE,
			&rtDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
			IID_PPV_ARGS(&m_pBloomTextures[i]) );
		if ( FAILED(hr) )
		{
			TRACE( "Bloom: Failed to create bloom RT %d", i );
			return false;
		}

		// Allocate RTV — reuse existing index if already allocated (resize case)
		if ( m_nBloomRTVIndex[i] == UINT(-1) )
			m_nBloomRTVIndex[i] = pDevice->m_RTVHeap.Allocate();
		if ( m_nBloomRTVIndex[i] == UINT(-1) )
		{
			TRACE( "Bloom: Failed to allocate RTV %d", i );
			return false;
		}
		pDevice->getDevice()->CreateRenderTargetView( m_pBloomTextures[i].Get(), nullptr,
			pDevice->m_RTVHeap.GetCPUHandle( m_nBloomRTVIndex[i] ) );

		// Allocate SRV in staging heap — reuse existing index if already allocated
		if ( m_nBloomSRVIndex[i] == UINT(-1) )
			m_nBloomSRVIndex[i] = pDevice->m_SRVStagingHeap.Allocate();
		if ( m_nBloomSRVIndex[i] == UINT(-1) )
		{
			TRACE( "Bloom: Failed to allocate SRV %d", i );
			return false;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		pDevice->getDevice()->CreateShaderResourceView( m_pBloomTextures[i].Get(), &srvDesc,
			pDevice->m_SRVStagingHeap.GetCPUHandle( m_nBloomSRVIndex[i] ) );
	}

	m_bInitialized = true;
	m_bBloomFailed = false;

	TRACE( "Bloom initialized: screen=%dx%d, bloom=%dx%d, levels=%d, scale=%.2f, threshold=%.2f",
		width, height, m_BloomSize.width, m_BloomSize.height,
		m_nBloomLevels, m_fBloomScale, m_fBrightThreshold );

	return true;
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectHDRD3D12::preRender( DisplayDevice * pDevice )
{
	// No preRender needed — bloom operates as pure post-process on m_pSceneRT
	return true;
}

//---------------------------------------------------------------------------------------------------

void DisplayEffectHDRD3D12::drawFullscreenTriangle( DisplayDeviceD3D12 * pDevice )
{
	ID3D12GraphicsCommandList * cl = pDevice->getCommandList();
	cl->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	cl->IASetVertexBuffers( 0, 0, nullptr );
	cl->DrawInstanced( 3, 1, 0, 0 );
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectHDRD3D12::postRender( DisplayDevice * pDevice )
{
	DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)pDevice;
	if ( !pDev || !pDev->isCommandListOpen() )
		return false;

	// Bloom requires m_pSceneRT (created by FXAA)
	if ( !pDev->m_bFXAAEnabled || !pDev->m_pSceneRT )
		return true;		// silently skip — no error

	if ( DisplayDevice::sm_bUseFixedFunction )
		return true;

	// Initialize bloom resources if needed
	if ( !m_bInitialized )
	{
		if ( !initBloom( pDev ) )
			return true;	// failed, skip bloom silently
	}

	ID3D12GraphicsCommandList * cl = pDev->getCommandList();
	ID3D12Device * dev = pDev->getDevice();

	// --- Setup bloom pipeline ---
	// Swapping root signature clears root-parameter bindings; drop the device's
	// redundant-bind cache so downstream helpers don't assume stale bindings are live.
	pDev->invalidateBoundSRVTable();
	cl->SetGraphicsRootSignature( m_pBloomRootSig.Get() );

	ID3D12DescriptorHeap * heaps[] = { pDev->m_SRVHeap.Get(), pDev->m_SamplerHeap.Get() };
	cl->SetDescriptorHeaps( _countof(heaps), heaps );

	// Bind sampler table (slot 2 in root sig).  Use the post-process sampler
	// at sampler heap index 2 (linear + CLAMP) — slot 0 is the material aniso/WRAP
	// sampler, which makes the 13-tap blur wrap across edges and produces
	// streaky banding when bright content sits at the screen border.
	cl->SetGraphicsRootDescriptorTable( 2, pDev->m_SamplerHeap.GetGPUHandle( 2 ) );

	// --- Step 1: Bright pass (m_pSceneRT → bloom[0]) ---
	if ( pDev->m_bSceneRTisRT )
	{
		TransitionResource( cl, pDev->m_pSceneRT.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		pDev->m_bSceneRTisRT = false;
	}
	TransitionResource( cl, m_pBloomTextures[0].Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );

	// Upload bright pass constants
	CBPostProcess cbBright = {};
	cbBright.texelSizeX = 1.0f / (float)m_BloomSize.width;
	cbBright.texelSizeY = 1.0f / (float)m_BloomSize.height;
	cbBright.fScale = 1.0f;
	cbBright.fBrightThreshold = m_fBrightThreshold;

	UploadRingBuffer::Allocation cbAlloc = pDev->allocateCB( sizeof(CBPostProcess) );
	memcpy( cbAlloc.cpuAddress, &cbBright, sizeof(cbBright) );
	cl->SetGraphicsRootConstantBufferView( 0, cbAlloc.gpuAddress );

	// Bind scene SRV (copy from staging to shader-visible heap)
	UINT sceneSRVSlot = pDev->allocSRVSlots( 1 );
	dev->CopyDescriptorsSimple( 1,
		pDev->m_SRVHeap.GetCPUHandle( sceneSRVSlot ),
		pDev->m_SRVStagingHeap.GetCPUHandle( pDev->m_nSceneSRVIndex ),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	cl->SetGraphicsRootDescriptorTable( 1, pDev->m_SRVHeap.GetGPUHandle( sceneSRVSlot ) );

	// Set bloom[0] as render target
	D3D12_CPU_DESCRIPTOR_HANDLE bloomRTV0 = pDev->m_RTVHeap.GetCPUHandle( m_nBloomRTVIndex[0] );
	cl->OMSetRenderTargets( 1, &bloomRTV0, FALSE, nullptr );
	float clearColor[4] = { 0, 0, 0, 0 };
	cl->ClearRenderTargetView( bloomRTV0, clearColor, 0, nullptr );

	D3D12_VIEWPORT bloomVP = { 0, 0, (float)m_BloomSize.width, (float)m_BloomSize.height, 0, 1 };
	D3D12_RECT bloomScissor = { 0, 0, (LONG)m_BloomSize.width, (LONG)m_BloomSize.height };
	cl->RSSetViewports( 1, &bloomVP );
	cl->RSSetScissorRects( 1, &bloomScissor );

	cl->SetPipelineState( m_pBrightPassPSO.Get() );
	drawFullscreenTriangle( pDev );

	// --- Step 2: Gaussian blur (ping-pong between bloom[0] and bloom[1]) ---
	CBPostProcess cbBlur = {};
	cbBlur.texelSizeX = 1.0f / (float)m_BloomSize.width;
	cbBlur.texelSizeY = 1.0f / (float)m_BloomSize.height;
	cbBlur.fScale = 1.0f;
	cbBlur.fBrightThreshold = 0.0f;

	for ( int level = 0; level < m_nBloomLevels; ++level )
	{
		// Horizontal blur: bloom[0] → bloom[1]
		TransitionResource( cl, m_pBloomTextures[0].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		TransitionResource( cl, m_pBloomTextures[1].Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );

		UINT bloomSRV0 = pDev->allocSRVSlots( 1 );
		dev->CopyDescriptorsSimple( 1,
			pDev->m_SRVHeap.GetCPUHandle( bloomSRV0 ),
			pDev->m_SRVStagingHeap.GetCPUHandle( m_nBloomSRVIndex[0] ),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		cl->SetGraphicsRootDescriptorTable( 1, pDev->m_SRVHeap.GetGPUHandle( bloomSRV0 ) );

		D3D12_CPU_DESCRIPTOR_HANDLE bloomRTV1 = pDev->m_RTVHeap.GetCPUHandle( m_nBloomRTVIndex[1] );
		cl->OMSetRenderTargets( 1, &bloomRTV1, FALSE, nullptr );

		cbAlloc = pDev->allocateCB( sizeof(CBPostProcess) );
		memcpy( cbAlloc.cpuAddress, &cbBlur, sizeof(cbBlur) );
		cl->SetGraphicsRootConstantBufferView( 0, cbAlloc.gpuAddress );

		cl->SetPipelineState( m_pHorzBlurPSO.Get() );
		drawFullscreenTriangle( pDev );

		// Vertical blur: bloom[1] → bloom[0]
		TransitionResource( cl, m_pBloomTextures[1].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		TransitionResource( cl, m_pBloomTextures[0].Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );

		UINT bloomSRV1 = pDev->allocSRVSlots( 1 );
		dev->CopyDescriptorsSimple( 1,
			pDev->m_SRVHeap.GetCPUHandle( bloomSRV1 ),
			pDev->m_SRVStagingHeap.GetCPUHandle( m_nBloomSRVIndex[1] ),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		cl->SetGraphicsRootDescriptorTable( 1, pDev->m_SRVHeap.GetGPUHandle( bloomSRV1 ) );

		cl->OMSetRenderTargets( 1, &bloomRTV0, FALSE, nullptr );

		cbAlloc = pDev->allocateCB( sizeof(CBPostProcess) );
		memcpy( cbAlloc.cpuAddress, &cbBlur, sizeof(cbBlur) );
		cl->SetGraphicsRootConstantBufferView( 0, cbAlloc.gpuAddress );

		cl->SetPipelineState( m_pVertBlurPSO.Get() );
		drawFullscreenTriangle( pDev );
	}

	// --- Step 3: Additive composite (bloom[0] → m_pSceneRT with additive blending) ---
	TransitionResource( cl, m_pBloomTextures[0].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	if ( !pDev->m_bSceneRTisRT )
	{
		TransitionResource( cl, pDev->m_pSceneRT.Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );
		pDev->m_bSceneRTisRT = true;
	}

	// Bind bloom[0] as input
	UINT bloomSRVFinal = pDev->allocSRVSlots( 1 );
	dev->CopyDescriptorsSimple( 1,
		pDev->m_SRVHeap.GetCPUHandle( bloomSRVFinal ),
		pDev->m_SRVStagingHeap.GetCPUHandle( m_nBloomSRVIndex[0] ),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	cl->SetGraphicsRootDescriptorTable( 1, pDev->m_SRVHeap.GetGPUHandle( bloomSRVFinal ) );

	// Set scene RT as render target (full screen viewport)
	D3D12_CPU_DESCRIPTOR_HANDLE sceneRTV = pDev->m_RTVHeap.GetCPUHandle( pDev->m_nSceneRTVIndex );
	cl->OMSetRenderTargets( 1, &sceneRTV, FALSE, nullptr );

	RectInt rw = pDev->renderWindow();
	D3D12_VIEWPORT sceneVP = { 0, 0, (float)rw.width(), (float)rw.height(), 0, 1 };
	D3D12_RECT sceneScissor = { 0, 0, (LONG)rw.width(), (LONG)rw.height() };
	cl->RSSetViewports( 1, &sceneVP );
	cl->RSSetScissorRects( 1, &sceneScissor );

	// Upload composite constants (fScale controls bloom intensity).  Re-read
	// the bloomScale setting every frame — same pattern as D3D9
	// (DisplayD3D/DisplayEffectHDR.cpp:133).  This lets the in-game slider
	// update bloom live without recreating the HDR effect, which previously
	// released D3D12 command allocators the GPU still had in flight.  The
	// Settings lookup is an in-memory hash access; the cost is trivial once
	// per composite vs. the cost of device removal.
#ifdef _DEBUG
	Settings liveSettings( "ClientD" );
#else
	Settings liveSettings( "Client" );
#endif
	const int nLiveScalePct = liveSettings.get( "bloomScale", 100 );
	m_fBloomScale = Clamp<float>( (float)nLiveScalePct / 100.0f, 0.0f, 1.0f );

	CBPostProcess cbComposite = {};
	cbComposite.texelSizeX = 1.0f / (float)rw.width();
	cbComposite.texelSizeY = 1.0f / (float)rw.height();
	cbComposite.fScale = m_fBloomScale;
	cbComposite.fBrightThreshold = 0.0f;

	cbAlloc = pDev->allocateCB( sizeof(CBPostProcess) );
	memcpy( cbAlloc.cpuAddress, &cbComposite, sizeof(cbComposite) );
	cl->SetGraphicsRootConstantBufferView( 0, cbAlloc.gpuAddress );

	cl->SetPipelineState( m_pAdditivePSO.Get() );
	drawFullscreenTriangle( pDev );

	// --- Restore main pipeline state ---
	// Re-bind main root signature and heaps so subsequent operations work correctly.
	// Drop the device's redundant-bind cache — root-sig/heap swaps cleared the GPU
	// state, so the next material draw must re-issue its own SetGraphicsRoot* calls.
	cl->SetGraphicsRootSignature( pDev->getRootSignature() );
	cl->SetDescriptorHeaps( _countof(heaps), heaps );
	pDev->invalidateBoundSRVTable();
	cl->SetGraphicsRootDescriptorTable( 4, pDev->m_SRVHeap.GetGPUHandle( 0 ) );
	cl->SetGraphicsRootDescriptorTable( 5, pDev->m_SamplerHeap.GetGPUHandle( 0 ) );

	// Restore render target with depth stencil
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = pDev->m_DSVHeap.GetCPUHandle( 0 );
	cl->OMSetRenderTargets( 1, &sceneRTV, FALSE, &dsvHandle );

	return true;
}

//---------------------------------------------------------------------------------------------------

void DisplayEffectHDRD3D12::release()
{
	m_pBloomTextures[0].Reset();
	m_pBloomTextures[1].Reset();
	m_pBrightPassPSO.Reset();
	m_pHorzBlurPSO.Reset();
	m_pVertBlurPSO.Reset();
	m_pAdditivePSO.Reset();
	m_pBloomRootSig.Reset();
	m_pVSBlob.Reset();
	m_pPSBrightPass.Reset();
	m_pPSHorzBlur.Reset();
	m_pPSVertBlur.Reset();
	m_pPSScale.Reset();

	m_bInitialized = false;
	m_bBloomFailed = false;
}

//---------------------------------------------------------------------------------------------------
// EOF
