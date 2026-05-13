/*
	DisplayEffectSSAO.cpp - D3D12 version
	Screen-Space Ambient Occlusion: depth sampling → bilateral blur → multiplicative apply.
	Operates on m_pSceneRT after materials render, before FXAA.
	(c)2024 Palestar
*/

#include "DisplayEffectSSAO.h"
#include "Debug/Trace.h"

//---------------------------------------------------------------------------------------------------

IMPLEMENT_FACTORY( DisplayEffectSSAOD3D12, DisplayEffect );

//---------------------------------------------------------------------------------------------------

struct CBSSAO
{
	ShaderMatrix	mProj;
	ShaderMatrix	mInvProj;
	float			texelSizeX;
	float			texelSizeY;
	float			fRadius;
	float			fBias;
	float			fIntensity;
	float			fScale;
	float			pad[2];
	int				nGTAOSlices;	// per-frame from DisplayDevice::sm_nShaderDetail
	int				nGTAOSteps;
	int				pad2[2];
};

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
			TRACE( "SSAO shader compile error (%s): %s", pEntry, (const char *)errors->GetBufferPointer() );
		return false;
	}
	return true;
}

//---------------------------------------------------------------------------------------------------

DisplayEffectSSAOD3D12::DisplayEffectSSAOD3D12() :
	m_fRadius( 25.0f ),
	m_fBias( 0.10f ),		// GTAO thickness threshold — minimum height above tangent plane (as fraction of sample distance) for a sample to count as a real occluder. 0.10 ≈ ~5.7° above-plane. Rejects same-surface samples that were causing view-dependent darkening of tilted flat faces and smooth spheres.
	m_fIntensity( 1.0f ),	// pow() exponent on final AO. 1.0 = no bias.
	m_LastSize( 0, 0 ),
	m_AOSize( 0, 0 ),
	m_bInitialized( false ),
	m_bFailed( false ),
	m_pCachedDevice( NULL )
{
	memset( m_nAORTVIndex, 0xff, sizeof(m_nAORTVIndex) );
	memset( m_nAOSRVIndex, 0xff, sizeof(m_nAOSRVIndex) );
}

DisplayEffectSSAOD3D12::~DisplayEffectSSAOD3D12()
{
	// Return descriptor slots to the device's heaps before release() drops
	// the resources — see DisplayEffectHDR for the leak this fixes.
	freeOwnedDescriptors();
	release();
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectSSAOD3D12::initSSAO( DisplayDeviceD3D12 * pDevice )
{
	if ( m_bFailed )
		return false;

	// Stash for destructor — see HDR.h's rationale.
	m_pCachedDevice = pDevice;

	RectInt rw = pDevice->renderWindow();
	UINT width  = (UINT)rw.width();
	UINT height = (UINT)rw.height();
	if ( width == 0 || height == 0 )
		return false;

	SizeInt currentSize( width, height );
	if ( m_bInitialized && m_LastSize == currentSize )
		return true;

	release();
	m_bFailed = true;

	m_LastSize = currentSize;
	// Full-res GTAO: half-res looked visibly pixelated against screen-space
	// detail (straight edges on ship hulls, planet silhouettes).  16 taps at
	// full res is still cheap on any GPU that can run the game — ~32M texture
	// reads at 1080p.  If this becomes a perf issue on low-end hardware the
	// divisor can be moved to a config var.
	m_AOSize = SizeInt( width, height );
	if ( m_AOSize.width < 1 ) m_AOSize.width = 1;
	if ( m_AOSize.height < 1 ) m_AOSize.height = 1;

	// Compile SSAO.hlsl with different entry points
	CharString sPath = DisplayDevice::sm_sShadersPath + "Shaders/SSAO.hlsl";
	wchar_t wszPath[MAX_PATH];
	MultiByteToWideChar( CP_ACP, 0, sPath, -1, wszPath, MAX_PATH );

	if ( !compileShaderEntry( wszPath, "vs_main", "vs_5_1", m_pVSBlob ) )
	{
		TRACE( "SSAO: Failed to compile VS" );
		return false;
	}
	if ( !compileShaderEntry( wszPath, "PS_SSAO", "ps_5_1", m_pPSSSAO ) )
	{
		TRACE( "SSAO: Failed to compile PS_SSAO" );
		return false;
	}
	if ( !compileShaderEntry( wszPath, "PS_SSAOBlur", "ps_5_1", m_pPSBlur ) )
	{
		TRACE( "SSAO: Failed to compile PS_SSAOBlur" );
		return false;
	}
	if ( !compileShaderEntry( wszPath, "PS_SSAOApply", "ps_5_1", m_pPSApply ) )
	{
		TRACE( "SSAO: Failed to compile PS_SSAOApply" );
		return false;
	}

	// Create root signature
	// [0] CBV at b0 (SSAO constants)
	// [1] SRV table: 2 SRVs at t0, t1
	// [2] Sampler table: 1 sampler at s0
	D3D12_ROOT_PARAMETER params[3] = {};

	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].Descriptor.ShaderRegister = 0;
	params[0].Descriptor.RegisterSpace = 0;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 2;
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
		if ( err ) TRACE( "SSAO root sig error: %s", (const char *)err->GetBufferPointer() );
		return false;
	}

	hr = pDevice->getDevice()->CreateRootSignature( 0, sig->GetBufferPointer(), sig->GetBufferSize(),
		IID_PPV_ARGS(&m_pSSAORootSig) );
	if ( FAILED(hr) )
	{
		TRACE( "SSAO: Failed to create root signature" );
		return false;
	}

	// Base PSO desc
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_pSSAORootSig.Get();
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

	// SSAO compute PSO
	psoDesc.PS = { m_pPSSSAO->GetBufferPointer(), m_pPSSSAO->GetBufferSize() };
	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pSSAOPSO) );
	if ( FAILED(hr) ) { TRACE( "SSAO: Failed to create SSAO PSO" ); return false; }

	// Blur PSO
	psoDesc.PS = { m_pPSBlur->GetBufferPointer(), m_pPSBlur->GetBufferSize() };
	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pBlurPSO) );
	if ( FAILED(hr) ) { TRACE( "SSAO: Failed to create Blur PSO" ); return false; }

	// Apply PSO (multiplicative: DST * SRC).  Writes back into the scene RT
	// (HDR float, DisplayDeviceD3D12::SCENE_RT_FORMAT), unlike the SSAO/Blur
	// PSOs above which target the 8-bit AO ping-pong textures.  PSO RTV format
	// must match the bound RTV exactly.
	psoDesc.PS = { m_pPSApply->GetBufferPointer(), m_pPSApply->GetBufferSize() };
	psoDesc.RTVFormats[0] = DisplayDeviceD3D12::SCENE_RT_FORMAT;
	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
	psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
	psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pApplyPSO) );
	if ( FAILED(hr) ) { TRACE( "SSAO: Failed to create Apply PSO" ); return false; }

	// Create AO render targets (half-res RGBA8)
	D3D12_RESOURCE_DESC rtDesc = {};
	rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rtDesc.Width = m_AOSize.width;
	rtDesc.Height = m_AOSize.height;
	rtDesc.DepthOrArraySize = 1;
	rtDesc.MipLevels = 1;
	rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtDesc.SampleDesc.Count = 1;
	rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE clearVal = {};
	clearVal.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	clearVal.Color[0] = clearVal.Color[1] = clearVal.Color[2] = 1.0f;
	clearVal.Color[3] = 1.0f;

	for ( int i = 0; i < 2; ++i )
	{
		hr = pDevice->getDevice()->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE,
			&rtDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearVal,
			IID_PPV_ARGS(&m_pAOTextures[i]) );
		if ( FAILED(hr) )
		{
			TRACE( "SSAO: Failed to create AO RT %d", i );
			return false;
		}

		// Reuse existing index if already allocated (resize case)
		if ( m_nAORTVIndex[i] == UINT(-1) )
			m_nAORTVIndex[i] = pDevice->m_RTVHeap.Allocate();
		if ( m_nAORTVIndex[i] == UINT(-1) )
		{
			TRACE( "SSAO: Failed to allocate RTV %d", i );
			return false;
		}
		pDevice->getDevice()->CreateRenderTargetView( m_pAOTextures[i].Get(), nullptr,
			pDevice->m_RTVHeap.GetCPUHandle( m_nAORTVIndex[i] ) );

		// Reuse existing index if already allocated (resize case)
		if ( m_nAOSRVIndex[i] == UINT(-1) )
			m_nAOSRVIndex[i] = pDevice->m_SRVStagingHeap.Allocate();
		if ( m_nAOSRVIndex[i] == UINT(-1) )
		{
			TRACE( "SSAO: Failed to allocate SRV %d", i );
			return false;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		pDevice->getDevice()->CreateShaderResourceView( m_pAOTextures[i].Get(), &srvDesc,
			pDevice->m_SRVStagingHeap.GetCPUHandle( m_nAOSRVIndex[i] ) );
	}

	m_bInitialized = true;
	m_bFailed = false;

	TRACE( "SSAO initialized: screen=%dx%d, AO=%dx%d, radius=%.1f, intensity=%.1f",
		width, height, m_AOSize.width, m_AOSize.height, m_fRadius, m_fIntensity );

	return true;
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectSSAOD3D12::preRender( DisplayDevice * pDevice )
{
	return true;
}

//---------------------------------------------------------------------------------------------------

void DisplayEffectSSAOD3D12::drawFullscreenTriangle( DisplayDeviceD3D12 * pDevice )
{
	ID3D12GraphicsCommandList * cl = pDevice->getCommandList();
	cl->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	cl->IASetVertexBuffers( 0, 0, nullptr );
	cl->DrawInstanced( 3, 1, 0, 0 );
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectSSAOD3D12::postRender( DisplayDevice * pDevice )
{
	DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)pDevice;
	if ( !pDev || !pDev->isCommandListOpen() )
		return false;

	if ( !pDev->m_bSceneRTEnabled || !pDev->m_pSceneRT )
		return true;

	if ( DisplayDevice::sm_bUseFixedFunction )
		return true;

	if ( pDev->m_nDepthSRVIndex == UINT(-1) )
		return true;

	if ( !m_bInitialized )
	{
		if ( !initSSAO( pDev ) )
			return true;
	}

	ID3D12GraphicsCommandList * cl = pDev->getCommandList();
	ID3D12Device * dev = pDev->getDevice();

	// Setup SSAO pipeline.  Swapping root signature clears root bindings — drop
	// the device-side cache so later bindSRVTableIfChanged() calls re-issue.
	pDev->invalidateBoundSRVTable();
	cl->SetGraphicsRootSignature( m_pSSAORootSig.Get() );

	ID3D12DescriptorHeap * heaps[] = { pDev->m_SRVHeap.Get(), pDev->m_SamplerHeap.Get() };
	cl->SetDescriptorHeaps( _countof(heaps), heaps );

	// Bind point sampler (slot 2) — use sampler slot 1 which is the point sampler
	cl->SetGraphicsRootDescriptorTable( 2, pDev->m_SamplerHeap.GetGPUHandle( 1 ) );

	RectInt rw = pDev->renderWindow();

	// Build SSAO constant buffer
	XMMATRIX proj = pDev->getProjMatrix();
	XMMATRIX invProj = XMMatrixInverse( nullptr, proj );

	CBSSAO cb = {};
	cb.mProj = ShaderMatrix( proj );
	cb.mInvProj = ShaderMatrix( invProj );
	cb.texelSizeX = 1.0f / (float)m_AOSize.width;
	cb.texelSizeY = 1.0f / (float)m_AOSize.height;
	cb.fRadius = m_fRadius;
	cb.fBias = m_fBias;
	cb.fIntensity = m_fIntensity;
	cb.fScale = 1.0f;

	// Map shaderDetail → GTAO tap budget.  Total taps per pixel is
	// (slices × steps × 2 directions).  HIGH = 2×4×2 = 16 = original.
	switch ( DisplayDevice::sm_nShaderDetail )
	{
	case DisplayDevice::SHADER_DETAIL_LOW:		cb.nGTAOSlices = 1; cb.nGTAOSteps = 2; break;	// 4 taps
	case DisplayDevice::SHADER_DETAIL_MEDIUM:	cb.nGTAOSlices = 2; cb.nGTAOSteps = 2; break;	// 8 taps
	case DisplayDevice::SHADER_DETAIL_HIGH:		cb.nGTAOSlices = 2; cb.nGTAOSteps = 4; break;	// 16 taps (original)
	case DisplayDevice::SHADER_DETAIL_EXTREME:	cb.nGTAOSlices = 3; cb.nGTAOSteps = 4; break;	// 24 taps
	default:									cb.nGTAOSlices = 2; cb.nGTAOSteps = 4; break;
	}

	// --- Step 1: Compute SSAO (depth buffer → AO[0]) ---
	// Transition depth to SRV
	TransitionResource( cl, pDev->m_pDepthStencil.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	TransitionResource( cl, m_pAOTextures[0].Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );

	// Upload CB
	UploadRingBuffer::Allocation cbAlloc = pDev->allocateCB( sizeof(CBSSAO) );
	memcpy( cbAlloc.cpuAddress, &cb, sizeof(cb) );
	cl->SetGraphicsRootConstantBufferView( 0, cbAlloc.gpuAddress );

	// Bind depth SRV at t0 (only t0 needed for SSAO pass)
	// allocSRVSlots returns the base of a bounded `count`-slot range, wrapping at heap end.
	UINT depthSlot = pDev->allocSRVSlots( 2 );
	dev->CopyDescriptorsSimple( 1,
		pDev->m_SRVHeap.GetCPUHandle( depthSlot ),
		pDev->m_SRVStagingHeap.GetCPUHandle( pDev->m_nDepthSRVIndex ),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	// t1 unused in SSAO pass — copy depth as placeholder
	dev->CopyDescriptorsSimple( 1,
		pDev->m_SRVHeap.GetCPUHandle( depthSlot + 1 ),
		pDev->m_SRVStagingHeap.GetCPUHandle( pDev->m_nDepthSRVIndex ),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	cl->SetGraphicsRootDescriptorTable( 1, pDev->m_SRVHeap.GetGPUHandle( depthSlot ) );

	// Set AO[0] as render target
	D3D12_CPU_DESCRIPTOR_HANDLE aoRTV0 = pDev->m_RTVHeap.GetCPUHandle( m_nAORTVIndex[0] );
	cl->OMSetRenderTargets( 1, &aoRTV0, FALSE, nullptr );
	float clearWhite[4] = { 1, 1, 1, 1 };
	cl->ClearRenderTargetView( aoRTV0, clearWhite, 0, nullptr );

	D3D12_VIEWPORT aoVP = { 0, 0, (float)m_AOSize.width, (float)m_AOSize.height, 0, 1 };
	D3D12_RECT aoScissor = { 0, 0, (LONG)m_AOSize.width, (LONG)m_AOSize.height };
	cl->RSSetViewports( 1, &aoVP );
	cl->RSSetScissorRects( 1, &aoScissor );

	cl->SetPipelineState( m_pSSAOPSO.Get() );
	drawFullscreenTriangle( pDev );

	// --- Step 2: Bilateral blur (AO[0] → AO[1]) ---
	TransitionResource( cl, m_pAOTextures[0].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	TransitionResource( cl, m_pAOTextures[1].Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );

	// Update texel size for blur pass (still half-res)
	cbAlloc = pDev->allocateCB( sizeof(CBSSAO) );
	memcpy( cbAlloc.cpuAddress, &cb, sizeof(cb) );
	cl->SetGraphicsRootConstantBufferView( 0, cbAlloc.gpuAddress );

	// Bind AO[0] at t0 and depth at t1
	UINT blurSlot = pDev->allocSRVSlots( 2 );
	dev->CopyDescriptorsSimple( 1,
		pDev->m_SRVHeap.GetCPUHandle( blurSlot ),
		pDev->m_SRVStagingHeap.GetCPUHandle( m_nAOSRVIndex[0] ),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	dev->CopyDescriptorsSimple( 1,
		pDev->m_SRVHeap.GetCPUHandle( blurSlot + 1 ),
		pDev->m_SRVStagingHeap.GetCPUHandle( pDev->m_nDepthSRVIndex ),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	cl->SetGraphicsRootDescriptorTable( 1, pDev->m_SRVHeap.GetGPUHandle( blurSlot ) );

	D3D12_CPU_DESCRIPTOR_HANDLE aoRTV1 = pDev->m_RTVHeap.GetCPUHandle( m_nAORTVIndex[1] );
	cl->OMSetRenderTargets( 1, &aoRTV1, FALSE, nullptr );

	cl->SetPipelineState( m_pBlurPSO.Get() );
	drawFullscreenTriangle( pDev );

	// --- Step 3: Apply AO to scene RT (multiplicative blend) ---
	TransitionResource( cl, m_pAOTextures[1].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );

	// Transition depth back to DEPTH_WRITE before restoring render target
	TransitionResource( cl, pDev->m_pDepthStencil.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE );

	// Bind AO[1] (blurred) at t0
	UINT applySlot = pDev->allocSRVSlots( 2 );
	dev->CopyDescriptorsSimple( 1,
		pDev->m_SRVHeap.GetCPUHandle( applySlot ),
		pDev->m_SRVStagingHeap.GetCPUHandle( m_nAOSRVIndex[1] ),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	dev->CopyDescriptorsSimple( 1,
		pDev->m_SRVHeap.GetCPUHandle( applySlot + 1 ),
		pDev->m_SRVStagingHeap.GetCPUHandle( m_nAOSRVIndex[1] ),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	cl->SetGraphicsRootDescriptorTable( 1, pDev->m_SRVHeap.GetGPUHandle( applySlot ) );

	// Set scene RT as render target (full res)
	D3D12_CPU_DESCRIPTOR_HANDLE sceneRTV = pDev->m_RTVHeap.GetCPUHandle( pDev->m_nSceneRTVIndex );
	cl->OMSetRenderTargets( 1, &sceneRTV, FALSE, nullptr );

	D3D12_VIEWPORT sceneVP = { 0, 0, (float)rw.width(), (float)rw.height(), 0, 1 };
	D3D12_RECT sceneScissor = { 0, 0, (LONG)rw.width(), (LONG)rw.height() };
	cl->RSSetViewports( 1, &sceneVP );
	cl->RSSetScissorRects( 1, &sceneScissor );

	// Upload apply constants
	cb.fScale = 1.0f;
	cbAlloc = pDev->allocateCB( sizeof(CBSSAO) );
	memcpy( cbAlloc.cpuAddress, &cb, sizeof(cb) );
	cl->SetGraphicsRootConstantBufferView( 0, cbAlloc.gpuAddress );

	cl->SetPipelineState( m_pApplyPSO.Get() );
	drawFullscreenTriangle( pDev );

	// --- Restore main pipeline state ---
	// Root-sig/heap swaps cleared the GPU's root bindings; drop the device-side
	// redundant-bind cache so the next material draw re-issues its bindings.
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

void DisplayEffectSSAOD3D12::release()
{
	m_pAOTextures[0].Reset();
	m_pAOTextures[1].Reset();
	m_pSSAOPSO.Reset();
	m_pBlurPSO.Reset();
	m_pApplyPSO.Reset();
	m_pSSAORootSig.Reset();
	m_pVSBlob.Reset();
	m_pPSSSAO.Reset();
	m_pPSBlur.Reset();
	m_pPSApply.Reset();
	// RTV / SRV indices intentionally left allocated — destructor frees them.

	m_bInitialized = false;
	m_bFailed = false;
}

//---------------------------------------------------------------------------------------------------
// Return RTV/SRV slots to the device's heaps.  Destructor-only; see HDR.

void DisplayEffectSSAOD3D12::freeOwnedDescriptors()
{
	if ( m_pCachedDevice == NULL )
		return;

	for ( int i = 0; i < 2; ++i )
	{
		if ( m_nAORTVIndex[i] != UINT(-1) )
		{
			m_pCachedDevice->m_RTVHeap.Free( m_nAORTVIndex[i] );
			m_nAORTVIndex[i] = UINT(-1);
		}
		if ( m_nAOSRVIndex[i] != UINT(-1) )
		{
			m_pCachedDevice->m_SRVStagingHeap.Free( m_nAOSRVIndex[i] );
			m_nAOSRVIndex[i] = UINT(-1);
		}
	}

	m_pCachedDevice = NULL;
}

void DisplayEffectSSAOD3D12::onDeviceShutdown()
{
	freeOwnedDescriptors();
}

//---------------------------------------------------------------------------------------------------
// EOF
