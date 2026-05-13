/*
	DisplayEffectHDR.cpp - D3D12 version
	Karis / Unreal-style bloom: progressive mip-chain downsample (13-tap
	filtered, partial-Karis firefly suppression) + 3x3 tent upsample with
	additive blending, then additive composite into the HDR scene RT.
	Operates on m_pSceneRT (FXAA intermediate RT, R11G11B10_FLOAT) after
	materials render.
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
	m_nMipCount( 6 ),			// initial value; overwritten from sm_nShaderDetail at initBloom time
	m_fBloomScale( 0.6f ),
	m_fBrightThreshold( 0.65f ),
	m_bFullResMip0( false ),	// overwritten from sm_nShaderDetail at initBloom time
	m_LastSize( 0, 0 ),
	m_LastShaderDetail( -1 ),
	m_bInitialized( false ),
	m_bBloomFailed( false ),
	m_pCachedDevice( NULL )
{
	for ( int i = 0; i < MAX_MIPS; ++i )
	{
		m_nMipRTVIndex[i] = UINT(-1);
		m_nMipSRVIndex[i] = UINT(-1);
		m_MipSizes[i] = SizeInt( 0, 0 );
	}
}

DisplayEffectHDRD3D12::~DisplayEffectHDRD3D12()
{
	// Return descriptor slots to the device's heaps BEFORE release() drops
	// the D3D resources.  Without this the slots leak across effect
	// re-instantiation (e.g. scene reload on faction switch / shipyard) —
	// roughly +6 RTV + +6 SRV per reload at EXTREME, exhausting the heap
	// after a few cycles.  release() itself intentionally does not touch
	// slot indices so the resize-recovery path can reuse them.
	freeOwnedDescriptors();
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

	// Stash the device for the destructor's freeOwnedDescriptors() — see ~Effect
	// comment.  Set here rather than in the ctor because the effect is created
	// via the factory without knowing its device, and initBloom is the first
	// call that has a device pointer.
	m_pCachedDevice = pDevice;

	RectInt rw = pDevice->renderWindow();
	UINT width  = (UINT)rw.width();
	UINT height = (UINT)rw.height();
	if ( width == 0 || height == 0 )
		return false;

	SizeInt currentSize( width, height );
	if ( m_bInitialized && m_LastSize == currentSize && m_LastShaderDetail == DisplayDevice::sm_nShaderDetail )
		return true;		// already initialized at this size + tier

	// Release old resources
	release();
	m_bBloomFailed = true;		// assume failure until we succeed

	m_LastSize         = currentSize;
	m_LastShaderDetail = DisplayDevice::sm_nShaderDetail;

	// Bloom mip-chain depth + mip[0] resolution by shader-detail tier.
	// More mips = wider, smoother halo at ~25% additional bandwidth per extra mip.
	// m_bFullResMip0 = true pulls mip[0] from half-res to scene-RT resolution,
	// killing the visible "plateau" pixelation around bright sources.
	//
	// NOTE: HIGH/EXTREME currently use half-res mip[0] (m_bFullResMip0=false).
	// Full-res was tried at HIGH/EXTREME but exposed sprite-billboard quad
	// boundaries on explosion fireball sprites: the half-res chain implicitly
	// smeared the texture-quad corners enough to hide them, full-res preserves
	// the squared quad geometry through the bloom additive composite.  Until
	// the underlying sprite textures are re-authored with proper radial alpha
	// falloff (per reference_wob_texture_repair memory), keep the chain at
	// half-res mip[0].  The full-res code path remains live for future use.
	switch ( DisplayDevice::sm_nShaderDetail )
	{
	case DisplayDevice::SHADER_DETAIL_LOW:
		m_nMipCount    = 4;
		m_bFullResMip0 = false;		// fillrate/VRAM budget for low-end GPUs
		break;
	case DisplayDevice::SHADER_DETAIL_MEDIUM:
		m_nMipCount    = 5;
		m_bFullResMip0 = false;
		break;
	case DisplayDevice::SHADER_DETAIL_HIGH:
		m_nMipCount    = 6;
		m_bFullResMip0 = false;		// reverted from full-res — see note above
		break;
	case DisplayDevice::SHADER_DETAIL_EXTREME:
		m_nMipCount    = 7;
		m_bFullResMip0 = false;		// reverted from full-res — see note above
		break;
	default:
		m_nMipCount    = 6;
		m_bFullResMip0 = false;
		break;
	}

	// Clamp mip count so the smallest mip is at least 4x4 px — below that the
	// 13-tap downsample kernel's ±2-texel reach goes out of bounds and the
	// Karis filter loses its anti-firefly property.
	// mipShift offsets index→shift: mip[i] = src >> (i + mipShift), so the
	// smallest mip is src >> (nActiveMips-1 + mipShift).  When mip[0] is
	// full-res (mipShift=0) this reaches the same physical floor a tier
	// deeper than the half-res chain — exactly the +1 mip the switch above
	// allocates for HIGH/EXTREME.
	const int mipShift = m_bFullResMip0 ? 0 : 1;
	int nActiveMips = Clamp<int>( m_nMipCount, 2, MAX_MIPS );
	while ( nActiveMips > 2 )
	{
		int wMin = (int)width  >> ( nActiveMips - 1 + mipShift );
		int hMin = (int)height >> ( nActiveMips - 1 + mipShift );
		if ( wMin >= 4 && hMin >= 4 )
			break;
		--nActiveMips;
	}
	m_nMipCount = nActiveMips;

	// Read bloomScale from user config — same key the D3D9 path used and the
	// in-game options slider writes (ViewOptions.cpp:585).  100 = full
	// intensity (1.0); user can dial down for subtler bloom.
#ifdef _DEBUG
	Settings settings( "ClientD" );
#else
	Settings settings( "Client" );
#endif
	const int nScalePct = settings.get( "bloomScale", 100 );
	m_fBloomScale = Clamp<float>( (float)nScalePct / 100.0f, 0.0f, 1.0f );

	// --- Compile PostProcess.hlsl with different entry points ---
	CharString sPath = DisplayDevice::sm_sShadersPath + "Shaders/PostProcess.hlsl";
	wchar_t wszPath[MAX_PATH];
	MultiByteToWideChar( CP_ACP, 0, sPath, -1, wszPath, MAX_PATH );

	if ( !compileShaderEntry( wszPath, "vs_main",        "vs_5_1", m_pVSBlob ) )        { TRACE( "Bloom: Failed to compile VS" );            return false; }
	if ( !compileShaderEntry( wszPath, "PS_BrightPass",  "ps_5_1", m_pPSBrightPass ) )  { TRACE( "Bloom: Failed to compile PS_BrightPass" );  return false; }
	if ( !compileShaderEntry( wszPath, "PS_Downsample",  "ps_5_1", m_pPSDownsample ) )  { TRACE( "Bloom: Failed to compile PS_Downsample" );  return false; }
	if ( !compileShaderEntry( wszPath, "PS_Upsample",    "ps_5_1", m_pPSUpsample ) )    { TRACE( "Bloom: Failed to compile PS_Upsample" );    return false; }
	if ( !compileShaderEntry( wszPath, "PS_Scale",       "ps_5_1", m_pPSScale ) )       { TRACE( "Bloom: Failed to compile PS_Scale" );       return false; }

	// --- Create bloom root signature ---
	// [0] CBV at b0  [1] SRV table (1 srv @ t0)  [2] Sampler table (1 sampler @ s0)
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
	// Base PSO desc shared by all bloom passes.  Bloom mips are now the same
	// format as the scene RT (HDR float) so the composite PSO does not need
	// to swap formats before writing back.
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
	psoDesc.RTVFormats[0] = DisplayDeviceD3D12::SCENE_RT_FORMAT;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleMask = UINT_MAX;

	// Bright-pass PSO (no blend, writes mip[0])
	psoDesc.PS = { m_pPSBrightPass->GetBufferPointer(), m_pPSBrightPass->GetBufferSize() };
	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pBrightPassPSO) );
	if ( FAILED(hr) ) { TRACE( "Bloom: Failed to create BrightPass PSO" ); return false; }

	// Downsample PSO (no blend, overwrites dest mip)
	psoDesc.PS = { m_pPSDownsample->GetBufferPointer(), m_pPSDownsample->GetBufferSize() };
	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pDownsamplePSO) );
	if ( FAILED(hr) ) { TRACE( "Bloom: Failed to create Downsample PSO" ); return false; }

	// Upsample PSO — ONE+ONE additive so each upsample level ACCUMULATES onto
	// the larger mip it targets.  This is what gives the Karis chain its
	// wide, smooth halo: each mip contributes its own spatial band.
	psoDesc.PS = { m_pPSUpsample->GetBufferPointer(), m_pPSUpsample->GetBufferSize() };
	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend     = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlend    = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].BlendOp      = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pUpsamplePSO) );
	if ( FAILED(hr) ) { TRACE( "Bloom: Failed to create Upsample PSO" ); return false; }

	// Final composite PSO (ONE+ONE additive, writes bloom mip[0] onto scene RT
	// scaled by the slider).
	psoDesc.PS = { m_pPSScale->GetBufferPointer(), m_pPSScale->GetBufferSize() };
	// (blend state unchanged from upsample PSO)
	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pAdditivePSO) );
	if ( FAILED(hr) ) { TRACE( "Bloom: Failed to create Additive PSO" ); return false; }

	// --- Create bloom mip chain ---
	// mip[0] resolution depends on tier: half-res (mipShift=1, LOW/MEDIUM) or
	// full-res (mipShift=0, HIGH/EXTREME).  Full-res mip[0] eliminates the
	// visible 2x2-pixel plateaus that the half-res variant produces around
	// bright sources, because the tightest bloom contribution now matches
	// the scene RT's Nyquist limit instead of operating at half pixel density.
	// mip[i] = scene >> (i + mipShift); each mip gets its own committed
	// resource, RTV, and SRV staging slot — independent so we can ping
	// between any pair.
	D3D12_RESOURCE_DESC rtDesc = {};
	rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rtDesc.DepthOrArraySize = 1;
	rtDesc.MipLevels = 1;
	rtDesc.Format = DisplayDeviceD3D12::SCENE_RT_FORMAT;
	rtDesc.SampleDesc.Count = 1;
	rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DisplayDeviceD3D12::SCENE_RT_FORMAT;

	for ( int i = 0; i < m_nMipCount; ++i )
	{
		int mipW = (int)width  >> ( i + mipShift );
		int mipH = (int)height >> ( i + mipShift );
		if ( mipW < 1 ) mipW = 1;
		if ( mipH < 1 ) mipH = 1;
		m_MipSizes[i] = SizeInt( mipW, mipH );

		rtDesc.Width  = (UINT64)mipW;
		rtDesc.Height = (UINT)mipH;

		hr = pDevice->getDevice()->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE,
			&rtDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
			IID_PPV_ARGS(&m_pMipRTs[i]) );
		if ( FAILED(hr) )
		{
			TRACE( "Bloom: Failed to create mip RT %d (%dx%d)", i, mipW, mipH );
			return false;
		}

		// Allocate RTV in the main RTV heap — reuse existing index if already allocated
		if ( m_nMipRTVIndex[i] == UINT(-1) )
			m_nMipRTVIndex[i] = pDevice->m_RTVHeap.Allocate();
		if ( m_nMipRTVIndex[i] == UINT(-1) )
		{
			TRACE( "Bloom: Failed to allocate RTV for mip %d", i );
			return false;
		}
		pDevice->getDevice()->CreateRenderTargetView( m_pMipRTs[i].Get(), nullptr,
			pDevice->m_RTVHeap.GetCPUHandle( m_nMipRTVIndex[i] ) );

		// Allocate SRV in staging heap
		if ( m_nMipSRVIndex[i] == UINT(-1) )
			m_nMipSRVIndex[i] = pDevice->m_SRVStagingHeap.Allocate();
		if ( m_nMipSRVIndex[i] == UINT(-1) )
		{
			TRACE( "Bloom: Failed to allocate SRV for mip %d", i );
			return false;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DisplayDeviceD3D12::SCENE_RT_FORMAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		pDevice->getDevice()->CreateShaderResourceView( m_pMipRTs[i].Get(), &srvDesc,
			pDevice->m_SRVStagingHeap.GetCPUHandle( m_nMipSRVIndex[i] ) );
	}

	m_bInitialized = true;
	m_bBloomFailed = false;

	TRACE( "Bloom (Karis mip chain) initialized: screen=%dx%d, mips=%d (mip0=%dx%d %s, mip%d=%dx%d), scale=%.2f, threshold=%.2f",
		width, height, m_nMipCount,
		m_MipSizes[0].width, m_MipSizes[0].height,
		m_bFullResMip0 ? "FULL-RES" : "half-res",
		m_nMipCount - 1, m_MipSizes[m_nMipCount - 1].width, m_MipSizes[m_nMipCount - 1].height,
		m_fBloomScale, m_fBrightThreshold );

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

	// Bloom requires the HDR scene RT (allocated by createFXAA).  Gates on the
	// scene-RT flag, not the AA mode — bloom is valid regardless of which AA
	// path runs at present (or none at all).
	if ( !pDev->m_bSceneRTEnabled || !pDev->m_pSceneRT )
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

	// Sampler slot 2 = linear CLAMP.  The material aniso/WRAP sampler at slot 0
	// would wrap bright edge pixels across the screen, producing streaks at
	// the screen border under the 13-tap downsample reach.
	cl->SetGraphicsRootDescriptorTable( 2, pDev->m_SamplerHeap.GetGPUHandle( 2 ) );

	// Small helper lambda (captured locals) to bind SRV staging slot into the
	// shader-visible heap at an allocated shader slot.  Avoids five copies of
	// the same three-liner below.
	auto bindSourceSRV = [&]( UINT stagingIdx )
	{
		UINT srvSlot = pDev->allocSRVSlots( 1 );
		dev->CopyDescriptorsSimple( 1,
			pDev->m_SRVHeap.GetCPUHandle( srvSlot ),
			pDev->m_SRVStagingHeap.GetCPUHandle( stagingIdx ),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		cl->SetGraphicsRootDescriptorTable( 1, pDev->m_SRVHeap.GetGPUHandle( srvSlot ) );
	};

	auto setMipRT = [&]( int mipIdx, D3D12_VIEWPORT & vpOut, D3D12_RECT & scOut )
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtv = pDev->m_RTVHeap.GetCPUHandle( m_nMipRTVIndex[mipIdx] );
		cl->OMSetRenderTargets( 1, &rtv, FALSE, nullptr );
		vpOut = { 0, 0, (float)m_MipSizes[mipIdx].width, (float)m_MipSizes[mipIdx].height, 0, 1 };
		scOut = { 0, 0, (LONG)m_MipSizes[mipIdx].width, (LONG)m_MipSizes[mipIdx].height };
		cl->RSSetViewports( 1, &vpOut );
		cl->RSSetScissorRects( 1, &scOut );
	};

	auto uploadCB = [&]( const CBPostProcess & cb )
	{
		UploadRingBuffer::Allocation cbAlloc = pDev->allocateCB( sizeof(CBPostProcess) );
		memcpy( cbAlloc.cpuAddress, &cb, sizeof(cb) );
		cl->SetGraphicsRootConstantBufferView( 0, cbAlloc.gpuAddress );
	};

	D3D12_VIEWPORT vp; D3D12_RECT sc;

	// --- Step 1: Bright pass (scene RT → mip[0]) ---
	if ( pDev->m_bSceneRTisRT )
	{
		TransitionResource( cl, pDev->m_pSceneRT.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		pDev->m_bSceneRTisRT = false;
	}
	TransitionResource( cl, m_pMipRTs[0].Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );

	CBPostProcess cbBright = {};
	cbBright.texelSizeX = 1.0f / (float)m_MipSizes[0].width;
	cbBright.texelSizeY = 1.0f / (float)m_MipSizes[0].height;
	cbBright.fScale = 1.0f;
	cbBright.fBrightThreshold = m_fBrightThreshold;
	uploadCB( cbBright );

	bindSourceSRV( pDev->m_nSceneSRVIndex );
	setMipRT( 0, vp, sc );
	float clearColor[4] = { 0, 0, 0, 0 };
	cl->ClearRenderTargetView( pDev->m_RTVHeap.GetCPUHandle( m_nMipRTVIndex[0] ), clearColor, 0, nullptr );
	cl->SetPipelineState( m_pBrightPassPSO.Get() );
	drawFullscreenTriangle( pDev );

	// --- Step 2: Downsample chain, mip[i-1] → mip[i] for i = 1..N-1 ---
	CBPostProcess cbPass = {};
	cbPass.fScale = 1.0f;
	cbPass.fBrightThreshold = 0.0f;
	for ( int i = 1; i < m_nMipCount; ++i )
	{
		TransitionResource( cl, m_pMipRTs[i - 1].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		TransitionResource( cl, m_pMipRTs[i].Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );

		cbPass.texelSizeX = 1.0f / (float)m_MipSizes[i].width;
		cbPass.texelSizeY = 1.0f / (float)m_MipSizes[i].height;
		uploadCB( cbPass );

		bindSourceSRV( m_nMipSRVIndex[i - 1] );
		setMipRT( i, vp, sc );
		cl->ClearRenderTargetView( pDev->m_RTVHeap.GetCPUHandle( m_nMipRTVIndex[i] ), clearColor, 0, nullptr );
		cl->SetPipelineState( m_pDownsamplePSO.Get() );
		drawFullscreenTriangle( pDev );
	}

	// --- Step 3: Upsample chain, mip[j+1] → mip[j] for j = N-2..0 (additive) ---
	// After downsample loop: mip[N-1] is RT, all others are PSR.
	// Flip mip[N-1] to PSR now so it can be sampled in the first upsample.
	for ( int j = m_nMipCount - 2; j >= 0; --j )
	{
		TransitionResource( cl, m_pMipRTs[j + 1].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		TransitionResource( cl, m_pMipRTs[j].Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );

		cbPass.texelSizeX = 1.0f / (float)m_MipSizes[j].width;
		cbPass.texelSizeY = 1.0f / (float)m_MipSizes[j].height;
		uploadCB( cbPass );

		bindSourceSRV( m_nMipSRVIndex[j + 1] );
		setMipRT( j, vp, sc );
		// NO clear — additive blend layers this upsample onto the existing
		// downsample result already in mip[j].
		cl->SetPipelineState( m_pUpsamplePSO.Get() );
		drawFullscreenTriangle( pDev );
	}

	// --- Step 4: Additive composite mip[0] → scene RT (scaled by slider) ---
	TransitionResource( cl, m_pMipRTs[0].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	if ( !pDev->m_bSceneRTisRT )
	{
		TransitionResource( cl, pDev->m_pSceneRT.Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );
		pDev->m_bSceneRTisRT = true;
	}

	bindSourceSRV( m_nMipSRVIndex[0] );

	// Set scene RT as render target (full screen viewport)
	D3D12_CPU_DESCRIPTOR_HANDLE sceneRTV = pDev->m_RTVHeap.GetCPUHandle( pDev->m_nSceneRTVIndex );
	cl->OMSetRenderTargets( 1, &sceneRTV, FALSE, nullptr );

	RectInt rw = pDev->renderWindow();
	D3D12_VIEWPORT sceneVP = { 0, 0, (float)rw.width(), (float)rw.height(), 0, 1 };
	D3D12_RECT sceneScissor = { 0, 0, (LONG)rw.width(), (LONG)rw.height() };
	cl->RSSetViewports( 1, &sceneVP );
	cl->RSSetScissorRects( 1, &sceneScissor );

	// Re-read the bloomScale setting every frame — same pattern as D3D9
	// (DisplayD3D/DisplayEffectHDR.cpp:133).  Slider updates bloom live
	// without recreating the effect.
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
	uploadCB( cbComposite );

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
	for ( int i = 0; i < MAX_MIPS; ++i )
	{
		m_pMipRTs[i].Reset();
		m_MipSizes[i] = SizeInt( 0, 0 );
		// RTV / SRV indices intentionally left allocated — the resize-recovery
		// path calls release() → re-init and expects to reuse the same slots.
		// The destructor's freeOwnedDescriptors() returns slots to the heap
		// when the effect is being destroyed for real (e.g. scene reload).
	}

	m_pBrightPassPSO.Reset();
	m_pDownsamplePSO.Reset();
	m_pUpsamplePSO.Reset();
	m_pAdditivePSO.Reset();
	m_pBloomRootSig.Reset();
	m_pVSBlob.Reset();
	m_pPSBrightPass.Reset();
	m_pPSDownsample.Reset();
	m_pPSUpsample.Reset();
	m_pPSScale.Reset();

	m_bInitialized = false;
	m_bBloomFailed = false;
}

//---------------------------------------------------------------------------------------------------
// Return RTV/SRV slots to the device's heaps and mark them unallocated.
// Called from the destructor only — see ~Effect comment.  Safe to call with
// m_pCachedDevice null (no-op).  Idempotent: a second call sees all indices
// at UINT(-1) and does nothing.

void DisplayEffectHDRD3D12::freeOwnedDescriptors()
{
	if ( m_pCachedDevice == NULL )
		return;

	for ( int i = 0; i < MAX_MIPS; ++i )
	{
		if ( m_nMipRTVIndex[i] != UINT(-1) )
		{
			m_pCachedDevice->m_RTVHeap.Free( m_nMipRTVIndex[i] );
			m_nMipRTVIndex[i] = UINT(-1);
		}
		if ( m_nMipSRVIndex[i] != UINT(-1) )
		{
			m_pCachedDevice->m_SRVStagingHeap.Free( m_nMipSRVIndex[i] );
			m_nMipSRVIndex[i] = UINT(-1);
		}
	}

	m_pCachedDevice = NULL;
}

void DisplayEffectHDRD3D12::onDeviceShutdown()
{
	// Device is about to destroy its descriptor heaps — drop our slots back
	// to it now while the heap is still valid.  freeOwnedDescriptors() nulls
	// out m_pCachedDevice as its last step, so a later destructor no-ops.
	freeOwnedDescriptors();
}

//---------------------------------------------------------------------------------------------------
// EOF
