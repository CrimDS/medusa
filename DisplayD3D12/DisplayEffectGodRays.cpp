/*
	DisplayEffectGodRays.cpp - D3D12 version
	Volumetric light scattering — 48-tap radial blur composited additively
	into the HDR scene RT.  The ray ORIGIN is the screen-space projection of
	the nearest star's world position, read from DisplayDevice's sun
	candidate slot (populated each frame by NounStar::render during scene
	rendering).  Off-screen / behind-camera stars produce fSunVisible=0
	which short-circuits the shader to black — no rays without a visible
	star.
	(c)2024 Palestar
*/

#include "DisplayEffectGodRays.h"
#include "Debug/Trace.h"
#include <DirectXMath.h>

using namespace DirectX;

//---------------------------------------------------------------------------------------------------

IMPLEMENT_FACTORY( DisplayEffectGodRaysD3D12, DisplayEffect );

//---------------------------------------------------------------------------------------------------

// CB layout — must match CBGodRays in GodRays.hlsl exactly
struct CBGodRays
{
	float	sunU;
	float	sunV;
	float	fSunVisible;
	float	fDensity;

	float	fWeight;
	float	fDecay;
	float	fExposure;
	float	fEclipseStrength;	// PS_Eclipse multiplicative darken — 0 disables, 1 = occluders at sunUV go fully black

	float	texelSizeX;
	float	texelSizeY;
	float	fProjNear;			// projection near-plane distance, world units
	float	fProjFar;			// projection far-plane distance, world units

	// Sun's view-space position (xyz in world units, w unused).
	// .z is the sky/foreground threshold in view-Z; .xy unused now (we
	// project to UV CPU-side and pass sunU/V in the screen-space slot
	// above).
	float	sunViewX;
	float	sunViewY;
	float	sunViewZ;
	float	fPadA;

	// Projection matrix _11 / _22 diagonals — used by the shader to
	// convert pixel UV into tangent-plane (z=1) coordinates so the
	// screen-space silhouette test against vOccluders is a true 2D
	// circle test (a sphere projects to a circle in tangent-plane space).
	float	fProjM11;
	float	fProjM22;
	float	fPadB;
	float	fPadC;

	// Chunk 4.5 — celestial occluders packed as TANGENT-PLANE silhouette
	// discs.  Tangent-plane is the z=1 image plane in view space; a
	// sphere at view-space (X,Y,Z) with radius R projects to a perfect
	// circle at (X/Z, Y/Z) with radius R/Z there.  Stored as
	//   xy = tangent-plane centre   (analogous to "screen position")
	//   z  = tangent-plane radius   (the disc's silhouette radius)
	//   w  = original view-space Z  (for gating: we only care about
	//        occluders between camera and sun, i.e. 0 < w < sunViewZ)
	// PS_Composite does a 2D ray-circle test against these along the
	// line from input.vUV → sunUV (both also converted to tangent
	// plane), so a "sky" pixel whose sightline-to-sun crosses any
	// planet's silhouette gets its rays killed.
	float	vOccluders[32 * 4];
	int		nNumOccluders;
	int		nGodRaysSamples;	// PS_GodRays march sample count — set per-frame from DisplayDevice::sm_nShaderDetail
	int		fPadE;
	int		fPadF;
};

//---------------------------------------------------------------------------------------------------

DisplayEffectGodRaysD3D12::DisplayEffectGodRaysD3D12() :
	m_fDensity( 0.85f ),
	m_fWeight( 0.55f ),		// bumped from 0.45 for more visible rays
	m_fDecay( 0.97f ),		// slightly longer falloff (0.96 → 0.97) so rays extend further from sun
	m_fExposure( 0.75f ),	// 1.20 was way too hot; 0.50 was the prior baseline. 0.75 is a gentle +50% over baseline.
	m_fEclipseStrength( 0.0f ),	// disabled by default. Was needed when god rays ran BEFORE HDR (bloom would smear glow onto occluders). Now god rays runs AFTER HDR (see WorldContext::render comment), so bloom never sees the rays and the eclipse darken is unnecessary. Code retained — set non-zero to enable.
	m_nRaysRTVIndex( UINT(-1) ),
	m_nRaysSRVIndex( UINT(-1) ),
	m_LastSize( 0, 0 ),
	m_RaysSize( 0, 0 ),
	m_bInitialized( false ),
	m_bFailed( false )
{
}

DisplayEffectGodRaysD3D12::~DisplayEffectGodRaysD3D12()
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
			TRACE( "GodRays shader compile error (%s): %s", pEntry, (const char *)errors->GetBufferPointer() );
		return false;
	}
	return true;
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectGodRaysD3D12::initGodRays( DisplayDeviceD3D12 * pDevice )
{
	if ( m_bFailed )
		return false;

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
	// Half-res rays RT.  Dropped back from full-res after the full-res
	// 8MB allocation stalled the main thread in NtGdiDdDDICreateAllocation
	// at launch under VRAM fragmentation — memory climbed to ~4GB before
	// the app became responsive.  The actual quality lever is sample count
	// + jitter + sun-proximity mask (96 samples, half-amplitude IGN, 6.0
	// falloff rate from sunUV); resolution gives diminishing returns past
	// half-res when those are tuned.  Half-res = 2MB at 1080p, fits in
	// fragmented VRAM without paging / eviction stalls.
	m_RaysSize = SizeInt( Max<int>( (int)width / 2, 4 ), Max<int>( (int)height / 2, 4 ) );

	// --- Compile shaders ---
	CharString sPath = DisplayDevice::sm_sShadersPath + "Shaders/GodRays.hlsl";
	wchar_t wszPath[MAX_PATH];
	MultiByteToWideChar( CP_ACP, 0, sPath, -1, wszPath, MAX_PATH );

	if ( !compileShaderEntry( wszPath, "vs_main",     "vs_5_1", m_pVSBlob ) )      { TRACE( "GodRays: Failed VS" );        return false; }
	if ( !compileShaderEntry( wszPath, "PS_GodRays",  "ps_5_1", m_pPSRays ) )      { TRACE( "GodRays: Failed PS_GodRays" ); return false; }
	if ( !compileShaderEntry( wszPath, "PS_Eclipse",  "ps_5_1", m_pPSEclipse ) )   { TRACE( "GodRays: Failed PS_Eclipse" ); return false; }
	if ( !compileShaderEntry( wszPath, "PS_Composite","ps_5_1", m_pPSComposite ) ) { TRACE( "GodRays: Failed PS_Composite" );return false; }

	// --- Root signature (mirror DisplayEffectHDR pattern) ---
	D3D12_ROOT_PARAMETER params[3] = {};

	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].Descriptor.ShaderRegister = 0;
	params[0].Descriptor.RegisterSpace = 0;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 2;		// t0 = scene/rays RT, t1 = depth (rays pass foreground reject)
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
		if ( err ) TRACE( "GodRays root sig error: %s", (const char *)err->GetBufferPointer() );
		return false;
	}
	hr = pDevice->getDevice()->CreateRootSignature( 0, sig->GetBufferPointer(), sig->GetBufferSize(),
		IID_PPV_ARGS(&m_pRootSig) );
	if ( FAILED(hr) ) { TRACE( "GodRays: Failed root sig" ); return false; }

	// --- PSOs ---
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_pRootSig.Get();
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

	// Rays PSO (writes quarter-res rays RT, no blend)
	psoDesc.PS = { m_pPSRays->GetBufferPointer(), m_pPSRays->GetBufferSize() };
	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pRaysPSO) );
	if ( FAILED(hr) ) { TRACE( "GodRays: Failed rays PSO" ); return false; }

	// Eclipse PSO (DST_new = DST * SRC, multiplicative darken on scene RT).
	// Output of PS_Eclipse IS the multiplier — values in [0,1], where 1.0
	// leaves the destination unchanged.  Blend equation:
	//   DST = DST * SRC_COLOR + 0 * SRC = DST * SRC.
	psoDesc.PS = { m_pPSEclipse->GetBufferPointer(), m_pPSEclipse->GetBufferSize() };
	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend      = D3D12_BLEND_ZERO;
	psoDesc.BlendState.RenderTarget[0].DestBlend     = D3D12_BLEND_SRC_COLOR;
	psoDesc.BlendState.RenderTarget[0].BlendOp       = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ZERO;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pEclipsePSO) );
	if ( FAILED(hr) ) { TRACE( "GodRays: Failed eclipse PSO" ); return false; }

	// Composite PSO (ONE+ONE additive into scene RT)
	psoDesc.PS = { m_pPSComposite->GetBufferPointer(), m_pPSComposite->GetBufferSize() };
	psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend      = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlend     = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].BlendOp       = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;
	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pCompositePSO) );
	if ( FAILED(hr) ) { TRACE( "GodRays: Failed composite PSO" ); return false; }

	// --- Rays RT (quarter-res, HDR float) ---
	D3D12_RESOURCE_DESC rtDesc = {};
	rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rtDesc.Width  = (UINT64)m_RaysSize.width;
	rtDesc.Height = (UINT)  m_RaysSize.height;
	rtDesc.DepthOrArraySize = 1;
	rtDesc.MipLevels = 1;
	rtDesc.Format = DisplayDeviceD3D12::SCENE_RT_FORMAT;
	rtDesc.SampleDesc.Count = 1;
	rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DisplayDeviceD3D12::SCENE_RT_FORMAT;

	hr = pDevice->getDevice()->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE,
		&rtDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
		IID_PPV_ARGS(&m_pRaysRT) );
	if ( FAILED(hr) ) { TRACE( "GodRays: Failed rays RT creation %dx%d", m_RaysSize.width, m_RaysSize.height ); return false; }

	if ( m_nRaysRTVIndex == UINT(-1) )
		m_nRaysRTVIndex = pDevice->m_RTVHeap.Allocate();
	pDevice->getDevice()->CreateRenderTargetView( m_pRaysRT.Get(), nullptr,
		pDevice->m_RTVHeap.GetCPUHandle( m_nRaysRTVIndex ) );

	if ( m_nRaysSRVIndex == UINT(-1) )
		m_nRaysSRVIndex = pDevice->m_SRVStagingHeap.Allocate();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DisplayDeviceD3D12::SCENE_RT_FORMAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	pDevice->getDevice()->CreateShaderResourceView( m_pRaysRT.Get(), &srvDesc,
		pDevice->m_SRVStagingHeap.GetCPUHandle( m_nRaysSRVIndex ) );

	m_bInitialized = true;
	m_bFailed = false;

	TRACE( "GodRays initialized: screen=%dx%d, rays=%dx%d",
		width, height, m_RaysSize.width, m_RaysSize.height );
	return true;
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectGodRaysD3D12::preRender( DisplayDevice * )
{
	return true;
}

//---------------------------------------------------------------------------------------------------

void DisplayEffectGodRaysD3D12::drawFullscreenTriangle( DisplayDeviceD3D12 * pDevice )
{
	ID3D12GraphicsCommandList * cl = pDevice->getCommandList();
	cl->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	cl->IASetVertexBuffers( 0, 0, nullptr );
	cl->DrawInstanced( 3, 1, 0, 0 );
}

//---------------------------------------------------------------------------------------------------
// Project the current sun-candidate world position (populated during scene
// rendering by NounStar::render via DisplayDevice::submitSunCandidate) to a
// screen-space UV.  Returns true if a sun was submitted this frame AND its
// projection lands in front of the camera.  sunVisible=1 when the projection
// is within a reasonable screen margin; 0 when off-screen or far off-screen
// (beyond that, marching 48 rays from every pixel toward a nonsense UV
// would sample garbage).
//---------------------------------------------------------------------------------------------------

static bool projectSunToScreen( DisplayDeviceD3D12 * pDevice,
	float & sunU, float & sunV, float & sunVisible,
	float & sunViewX, float & sunViewY, float & sunViewZ )
{
	sunU = 0.5f; sunV = 0.5f; sunVisible = 0.0f;
	sunViewX = 0.0f; sunViewY = 0.0f; sunViewZ = 0.0f;

	Vector3 sunWorld;
	if ( !pDevice->getSunCandidate( sunWorld ) )
		return false;	// no star submitted this frame

	XMMATRIX viewMat = pDevice->getViewMatrix();
	XMMATRIX projMat = pDevice->getProjMatrix();

	// Project sun's world centre to clip space for screen UV.
	XMVECTOR sunHomog = XMVectorSet( sunWorld.x, sunWorld.y, sunWorld.z, 1.0f );
	XMMATRIX viewProj = XMMatrixMultiply( viewMat, projMat );
	XMVECTOR clip = XMVector4Transform( sunHomog, viewProj );

	float w = XMVectorGetW( clip );
	if ( w <= 0.0f )		// star is behind the camera
		return true;		// return true so effect runs; sunVisible=0 short-circuits in shader

	float ndcX = XMVectorGetX( clip ) / w;
	float ndcY = XMVectorGetY( clip ) / w;
	sunU =  ndcX * 0.5f + 0.5f;
	sunV = -ndcY * 0.5f + 0.5f;

	// Full view-space sun position (Chunk 4.5) — needed for the ray-sphere
	// occluder test in the composite pass, plus the existing depth/sky
	// classification (which uses Z only).
	XMVECTOR sunView = XMVector4Transform( sunHomog, viewMat );
	sunViewX = XMVectorGetX( sunView );
	sunViewY = XMVectorGetY( sunView );
	sunViewZ = XMVectorGetZ( sunView );

	// Accept rays even when the sun is slightly off-screen (they still
	// converge on the implied point and produce pleasing edge streaks).
	// Reject only when the projection is so far off that 48-tap radial
	// marches would walk into unrelated UVs and produce noise.
	if ( sunU < -1.5f || sunU > 2.5f || sunV < -1.5f || sunV > 2.5f )
		return true;

	sunVisible = 1.0f;
	return true;
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectGodRaysD3D12::postRender( DisplayDevice * pDevice )
{
	DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)pDevice;
	if ( !pDev || !pDev->isCommandListOpen() )
		return false;

	if ( !pDev->m_bFXAAEnabled || !pDev->m_pSceneRT )
		return true;
	if ( DisplayDevice::sm_bUseFixedFunction )
		return true;

	if ( !m_bInitialized && !initGodRays( pDev ) )
		return true;

	// Depth SRV must exist — god rays' foreground reject reads depth at t1.
	// Without it the shader would fetch whatever is bound as the neutral
	// staging descriptor and produce garbage.
	if ( pDev->m_nDepthSRVIndex == UINT(-1) )
		return true;

	// Resolve the nearest submitted star's screen-space UV + view-space
	// position.  No star → no rays; behind camera / far off-screen →
	// shader short-circuits via fSunVisible=0.
	float sunU, sunV, sunVis, sunViewX, sunViewY, sunViewZ;
	if ( !projectSunToScreen( pDev, sunU, sunV, sunVis, sunViewX, sunViewY, sunViewZ ) )
		return true;	// no star submitted this frame, nothing to do

	ID3D12GraphicsCommandList * cl = pDev->getCommandList();
	ID3D12Device * dev = pDev->getDevice();

	pDev->invalidateBoundSRVTable();
	cl->SetGraphicsRootSignature( m_pRootSig.Get() );
	ID3D12DescriptorHeap * heaps[] = { pDev->m_SRVHeap.Get(), pDev->m_SamplerHeap.Get() };
	cl->SetDescriptorHeaps( _countof(heaps), heaps );
	cl->SetGraphicsRootDescriptorTable( 2, pDev->m_SamplerHeap.GetGPUHandle( 2 ) );	// linear clamp

	auto uploadCB = [&]( const CBGodRays & cb )
	{
		UploadRingBuffer::Allocation cbAlloc = pDev->allocateCB( sizeof(CBGodRays) );
		memcpy( cbAlloc.cpuAddress, &cb, sizeof(cb) );
		cl->SetGraphicsRootConstantBufferView( 0, cbAlloc.gpuAddress );
	};

	auto bindSRVs = [&]( UINT t0idx, UINT t1idx )
	{
		UINT slot = pDev->allocSRVSlots( 2 );
		dev->CopyDescriptorsSimple( 1,
			pDev->m_SRVHeap.GetCPUHandle( slot ),
			pDev->m_SRVStagingHeap.GetCPUHandle( t0idx ),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		dev->CopyDescriptorsSimple( 1,
			pDev->m_SRVHeap.GetCPUHandle( slot + 1 ),
			pDev->m_SRVStagingHeap.GetCPUHandle( t1idx ),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		cl->SetGraphicsRootDescriptorTable( 1, pDev->m_SRVHeap.GetGPUHandle( slot ) );
	};

	// --- Step 1: Scene RT → rays RT (half-res, radial blur + brightpass + depth reject) ---
	if ( pDev->m_bSceneRTisRT )
	{
		TransitionResource( cl, pDev->m_pSceneRT.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		pDev->m_bSceneRTisRT = false;
	}
	TransitionResource( cl, m_pRaysRT.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );

	// Depth → PSR for the foreground-reject lookup in PS_GodRays.  Restored
	// to DEPTH_WRITE at the end of postRender before the DSV is rebound.
	TransitionResource( cl, pDev->m_pDepthStencil.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );

	CBGodRays cb = {};
	cb.sunU = sunU;
	cb.sunV = sunV;
	cb.fSunVisible = sunVis;
	cb.fDensity   = m_fDensity;
	cb.fWeight    = m_fWeight;
	cb.fDecay     = m_fDecay;
	cb.fExposure  = m_fExposure;
	cb.fEclipseStrength = m_fEclipseStrength;
	cb.texelSizeX = 1.0f / (float)m_RaysSize.width;
	cb.texelSizeY = 1.0f / (float)m_RaysSize.height;
	cb.fProjNear  = pDev->m_Proj.m_fFront;
	cb.fProjFar   = pDev->m_Proj.m_fBack;
	cb.sunViewX   = sunViewX;
	cb.sunViewY   = sunViewY;
	cb.sunViewZ   = sunViewZ;

	// Projection matrix _11 / _22 diagonals — read directly from the
	// device's projection matrix.  XMMATRIX.r[0].m128_f32[0] = m11,
	// .r[1].m128_f32[1] = m22.  These let the shader reconstruct view-X
	// and view-Y from screen UV + view-Z without needing the full inverse
	// projection matrix (cheaper CB, simpler shader).
	XMMATRIX projMat = pDev->getProjMatrix();
	XMFLOAT4X4 projF;
	XMStoreFloat4x4( &projF, projMat );
	cb.fProjM11 = projF.m[0][0];
	cb.fProjM22 = projF.m[1][1];

	// Pack occluders as TANGENT-PLANE silhouette discs.  See CBGodRays
	// definition for the format.  Behind-camera and behind-sun occluders
	// get a sentinel w<=0 so the shader skips them.
	//
	// We use the FULL submitted list — no 3D-shadow filtering.  An
	// obscured planet (one in another's 3D sun-shadow) still has a
	// visible silhouette on screen, and its tangent-plane disc is what
	// kills rays at its on-screen rim.  Skipping those would create a
	// parallax bug: a small body visually well-separated from its
	// obscurer in the camera's view would lose silhouette shadow even
	// though geometrically (from the sun's POV) it sits in the larger
	// body's shadow column.
	XMMATRIX viewMat = pDev->getViewMatrix();
	const int occCount = pDev->getOccluderCount();
	cb.nNumOccluders = (occCount > 32) ? 32 : occCount;

	// Map the global shaderDetail knob to PS_GodRays' march sample count.
	// 96 (EXTREME) is the original/Mitchell-conservative value; 64 (HIGH)
	// halves the depth-sample bandwidth with minimal visible change thanks
	// to the jitter dither; 24 (LOW) is for low-end iGPUs where rays were
	// the dominant frame cost.
	switch ( DisplayDevice::sm_nShaderDetail )
	{
	case DisplayDevice::SHADER_DETAIL_LOW:		cb.nGodRaysSamples = 24; break;
	case DisplayDevice::SHADER_DETAIL_MEDIUM:	cb.nGodRaysSamples = 48; break;
	case DisplayDevice::SHADER_DETAIL_HIGH:		cb.nGodRaysSamples = 64; break;
	case DisplayDevice::SHADER_DETAIL_EXTREME:	cb.nGodRaysSamples = 96; break;
	default:									cb.nGodRaysSamples = 64; break;
	}
	for ( int i = 0; i < cb.nNumOccluders; ++i )
	{
		const DisplayDevice::OccluderInfo & o = pDev->getOccluder( i );
		XMVECTOR oWorld = XMVectorSet( o.worldPos.x, o.worldPos.y, o.worldPos.z, 1.0f );
		XMVECTOR oView  = XMVector4Transform( oWorld, viewMat );
		float vX = XMVectorGetX( oView );
		float vY = XMVectorGetY( oView );
		float vZ = XMVectorGetZ( oView );
		if ( vZ > 0.001f )
		{
			cb.vOccluders[i*4 + 0] = vX / vZ;			// tangent-plane centre x
			cb.vOccluders[i*4 + 1] = vY / vZ;			// tangent-plane centre y
			cb.vOccluders[i*4 + 2] = o.radius / vZ;		// tangent-plane radius
			// Encode bShadowed in the sign of the view-Z gate value.
			// Shader uses abs(w) for the actual view-Z and (w<0) as the
			// shadowed flag.  Sentinel (skip) is now w == 0.
			cb.vOccluders[i*4 + 3] = o.bShadowed ? -vZ : vZ;
		}
		else
		{
			// Behind / on the camera plane — sentinel skip (w == 0).
			cb.vOccluders[i*4 + 0] = 0.0f;
			cb.vOccluders[i*4 + 1] = 0.0f;
			cb.vOccluders[i*4 + 2] = 0.0f;
			cb.vOccluders[i*4 + 3] = 0.0f;
		}
	}
	for ( int i = cb.nNumOccluders; i < 32; ++i )
	{
		cb.vOccluders[i*4 + 0] = 0.0f;
		cb.vOccluders[i*4 + 1] = 0.0f;
		cb.vOccluders[i*4 + 2] = 0.0f;
		cb.vOccluders[i*4 + 3] = 0.0f;	// sentinel (skip)
	}

	uploadCB( cb );

	bindSRVs( pDev->m_nSceneSRVIndex, pDev->m_nDepthSRVIndex );

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = pDev->m_RTVHeap.GetCPUHandle( m_nRaysRTVIndex );
	cl->OMSetRenderTargets( 1, &rtv, FALSE, nullptr );
	D3D12_VIEWPORT vp = { 0, 0, (float)m_RaysSize.width, (float)m_RaysSize.height, 0, 1 };
	D3D12_RECT sc   = { 0, 0, (LONG)m_RaysSize.width, (LONG)m_RaysSize.height };
	cl->RSSetViewports( 1, &vp );
	cl->RSSetScissorRects( 1, &sc );
	float clearColor[4] = { 0, 0, 0, 0 };
	cl->ClearRenderTargetView( rtv, clearColor, 0, nullptr );
	cl->SetPipelineState( m_pRaysPSO.Get() );
	drawFullscreenTriangle( pDev );

	// --- Step 2: Eclipse darken pass on scene RT (DST *= SRC).
	// Foreground occluders near the sun get multiplicatively darkened so
	// they read as silhouettes against the additive ray glow that follows.
	// Without this, the additive rays brighten the surrounding sky and HDR
	// bloom (post-god-rays) bleeds that brightness back across occluder
	// edges — occluders end up looking semi-transparent / washed-out.
	//
	// Eclipse runs at FULL res on the scene RT (not the half-res rays RT),
	// since we're modifying the actual scene pixels.  Reads depth (still
	// in PSR state from step 1) at t1; t0 is a don't-care, bound to the
	// rays SRV to satisfy the 2-SRV root table.
	TransitionResource( cl, m_pRaysRT.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	if ( !pDev->m_bSceneRTisRT )
	{
		TransitionResource( cl, pDev->m_pSceneRT.Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );
		pDev->m_bSceneRTisRT = true;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE sceneRTV = pDev->m_RTVHeap.GetCPUHandle( pDev->m_nSceneRTVIndex );
	cl->OMSetRenderTargets( 1, &sceneRTV, FALSE, nullptr );

	RectInt rw = pDev->renderWindow();
	D3D12_VIEWPORT sceneVP = { 0, 0, (float)rw.width(), (float)rw.height(), 0, 1 };
	D3D12_RECT sceneScissor = { 0, 0, (LONG)rw.width(), (LONG)rw.height() };
	cl->RSSetViewports( 1, &sceneVP );
	cl->RSSetScissorRects( 1, &sceneScissor );

	if ( m_fEclipseStrength > 0.0f )
	{
		bindSRVs( m_nRaysSRVIndex, pDev->m_nDepthSRVIndex );
		uploadCB( cb );
		cl->SetPipelineState( m_pEclipsePSO.Get() );
		drawFullscreenTriangle( pDev );
	}

	// --- Step 3: Rays RT → scene RT (full-res, ONE+ONE additive) ---
	// Composite reads rays at t0 and depth at t1.  Depth is needed so the
	// shader can gate by destination-pixel depth — rays composite onto sky
	// pixels only, never onto foreground occluders (otherwise distant
	// stars / moons paint rays ON TOP of nearer planets and ships).
	// Depth is still in PIXEL_SHADER_RESOURCE state from step 1, so no
	// transition needed here.
	bindSRVs( m_nRaysSRVIndex, pDev->m_nDepthSRVIndex );

	// CB unchanged from prior pass; composite reads fExposure, fSunDepth,
	// fSunVisible.
	cl->SetPipelineState( m_pCompositePSO.Get() );
	drawFullscreenTriangle( pDev );

	// Depth back to DEPTH_WRITE — required before the next OMSetRenderTargets
	// binds the DSV (and before subsequent effects like SSAO re-transition it).
	TransitionResource( cl, pDev->m_pDepthStencil.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE );

	// --- Restore main pipeline state ---
	cl->SetGraphicsRootSignature( pDev->getRootSignature() );
	cl->SetDescriptorHeaps( _countof(heaps), heaps );
	pDev->invalidateBoundSRVTable();
	cl->SetGraphicsRootDescriptorTable( 4, pDev->m_SRVHeap.GetGPUHandle( 0 ) );
	cl->SetGraphicsRootDescriptorTable( 5, pDev->m_SamplerHeap.GetGPUHandle( 0 ) );
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = pDev->m_DSVHeap.GetCPUHandle( 0 );
	cl->OMSetRenderTargets( 1, &sceneRTV, FALSE, &dsvHandle );

	return true;
}

//---------------------------------------------------------------------------------------------------

void DisplayEffectGodRaysD3D12::release()
{
	m_pRaysRT.Reset();
	m_pRaysPSO.Reset();
	m_pEclipsePSO.Reset();
	m_pCompositePSO.Reset();
	m_pRootSig.Reset();
	m_pVSBlob.Reset();
	m_pPSRays.Reset();
	m_pPSEclipse.Reset();
	m_pPSComposite.Reset();
	// RTV / SRV staging indices left allocated — same resize recovery pattern
	// as DisplayEffectHDR.
	m_bInitialized = false;
	m_bFailed = false;
}

//---------------------------------------------------------------------------------------------------
// EOF
