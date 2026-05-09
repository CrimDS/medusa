/*
	DisplayEffectLimbGlow.cpp - D3D12 version
	Cinematic limb-glow effect.  For each foreground celestial occluder, the
	shader renders a halo at the silhouette edge biased toward the sun-facing
	side; intensity ramps with how close the sun's screen UV is to the disc.
	Composited additively into the HDR scene RT before bloom.  Clear sky
	produces zero output; far-side bodies excluded by view-Z.
	(c)2024-2026 Palestar
*/

#include "DisplayEffectLimbGlow.h"
#include "Debug/Trace.h"
#include "Standard/Constant.h"
#include <DirectXMath.h>

//---------------------------------------------------------------------------------------------------

using namespace DirectX;

//---------------------------------------------------------------------------------------------------

IMPLEMENT_FACTORY( DisplayEffectLimbGlowD3D12, DisplayEffect );

//---------------------------------------------------------------------------------------------------

// CB layout — must match CBLimbGlow in LimbGlow.hlsl exactly.
// 16-byte aligned in 4-element float groups.
struct CBLimbGlow
{
	float	sunU;
	float	sunV;
	float	fSunVisible;
	float	fWeight;

	float	fExposure;
	float	fSunViewZ;			// sun's view-space Z (camera-forward distance, world units) — used to classify foreground/background occluders and to depth-gate sky pixels
	float	fProjNear;			// projection near-plane distance, world units
	float	fProjFar;			// projection far-plane distance, world units

	float	fProjM11;			// projection matrix [0][0] — converts NDC.x to tangent-plane.x
	float	fProjM22;			// projection matrix [1][1] — converts NDC.y to tangent-plane.y
	int		nNumOccluders;
	float	fPad0;

	// Celestial occluders packed as TANGENT-PLANE silhouette discs.  A
	// sphere at view-space (X,Y,Z) with radius R projects to a circle at
	// (X/Z, Y/Z) with radius R/Z on the z=1 image plane.  Stored as
	//   xy = disc centre (tangent-plane coords)
	//   z  = disc radius (tangent-plane units)
	//   w  = original view-space Z (filter: only 0 < w < sunViewZ
	//        contributes; w == 0 is sentinel for skip)
	// Packed as float[32*4] rather than DirectX::XMFLOAT4 to keep the CB
	// definition self-contained and avoid alignment surprises across the
	// CPU/GPU boundary.
	float	vOccluders[32 * 4];
};

//---------------------------------------------------------------------------------------------------

DisplayEffectLimbGlowD3D12::DisplayEffectLimbGlowD3D12() :
	m_fWeight( 0.55f ),		// rim glow intensity scale; tunable at runtime via Constants.ini
	m_fExposure( 0.75f ),	// final scale; tunable at runtime via Constants.ini
	m_nGlowRTVIndex( UINT(-1) ),
	m_nGlowSRVIndex( UINT(-1) ),
	m_LastSize( 0, 0 ),
	m_GlowSize( 0, 0 ),
	m_LastShaderDetail( -1 ),
	m_bInitialized( false ),
	m_bFailed( false )
{
}

DisplayEffectLimbGlowD3D12::~DisplayEffectLimbGlowD3D12()
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
			TRACE( "LimbGlowshader compile error (%s): %s", pEntry, (const char *)errors->GetBufferPointer() );
		return false;
	}
	return true;
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectLimbGlowD3D12::initLimbGlow( DisplayDeviceD3D12 * pDevice )
{
	if ( m_bFailed )
		return false;

	RectInt rw = pDevice->renderWindow();
	UINT width  = (UINT)rw.width();
	UINT height = (UINT)rw.height();
	if ( width == 0 || height == 0 )
		return false;

	SizeInt currentSize( width, height );
	if ( m_bInitialized && m_LastSize == currentSize && m_LastShaderDetail == DisplayDevice::sm_nShaderDetail )
		return true;

	release();
	m_bFailed = true;

	m_LastSize         = currentSize;
	m_LastShaderDetail = DisplayDevice::sm_nShaderDetail;

	// LOW / MEDIUM use a half-res rim RT; HIGH / EXTREME use full-res.
	const bool bFullRes = ( DisplayDevice::sm_nShaderDetail >= DisplayDevice::SHADER_DETAIL_HIGH );
	const int  rtScale  = bFullRes ? 1 : 2;
	m_GlowSize = SizeInt(
		Max<int>( (int)width  / rtScale, 4 ),
		Max<int>( (int)height / rtScale, 4 ) );

	// --- Compile shaders ---
	CharString sPath = DisplayDevice::sm_sShadersPath + "Shaders/LimbGlow.hlsl";
	wchar_t wszPath[MAX_PATH];
	MultiByteToWideChar( CP_ACP, 0, sPath, -1, wszPath, MAX_PATH );

	if ( !compileShaderEntry( wszPath, "vs_main",      "vs_5_1", m_pVSBlob ) )     { TRACE( "LimbGlow: Failed VS" );          return false; }
	if ( !compileShaderEntry( wszPath, "PS_LimbGlow",  "ps_5_1", m_pPSGlow ) )     { TRACE( "LimbGlow: Failed PS_LimbGlow" ); return false; }
	if ( !compileShaderEntry( wszPath, "PS_Composite", "ps_5_1", m_pPSComposite ) ){ TRACE( "LimbGlow: Failed PS_Composite" );return false; }

	// --- Root signature (mirror DisplayEffectHDR pattern) ---
	D3D12_ROOT_PARAMETER params[3] = {};

	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].Descriptor.ShaderRegister = 0;
	params[0].Descriptor.RegisterSpace = 0;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 2;		// t0 = scene RT (in glow pass) / glow RT (in composite pass), t1 = depth
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
		if ( err ) TRACE( "LimbGlowroot sig error: %s", (const char *)err->GetBufferPointer() );
		return false;
	}
	hr = pDevice->getDevice()->CreateRootSignature( 0, sig->GetBufferPointer(), sig->GetBufferSize(),
		IID_PPV_ARGS(&m_pRootSig) );
	if ( FAILED(hr) ) { TRACE( "LimbGlow: Failed root sig" ); return false; }

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

	// Glow PSO (writes half-res rim glow RT, no blend — clears each frame
	// and overwrites with the per-occluder rim accumulation).
	psoDesc.PS = { m_pPSGlow->GetBufferPointer(), m_pPSGlow->GetBufferSize() };
	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pGlowPSO) );
	if ( FAILED(hr) ) { TRACE( "LimbGlow: Failed glow PSO" ); return false; }

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
	if ( FAILED(hr) ) { TRACE( "LimbGlow: Failed composite PSO" ); return false; }

	// --- Rim glow RT (half-res, HDR float) ---
	D3D12_RESOURCE_DESC rtDesc = {};
	rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rtDesc.Width  = (UINT64)m_GlowSize.width;
	rtDesc.Height = (UINT)  m_GlowSize.height;
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
		IID_PPV_ARGS(&m_pGlowRT) );
	if ( FAILED(hr) ) { TRACE( "LimbGlow: Failed glow RT creation %dx%d (hr=0x%08X)", m_GlowSize.width, m_GlowSize.height, hr ); return false; }
	if ( !m_pGlowRT ) { TRACE( "LimbGlow: glow RT null after S_OK" ); return false; }

	// Heap-exhaustion guard: GetCPUHandle(UINT(-1)) produces a wild
	// pointer that crashes inside D3D12Core.  Match HDR / SSAO / Exposure.
	if ( m_nGlowRTVIndex == UINT(-1) )
		m_nGlowRTVIndex = pDevice->m_RTVHeap.Allocate();
	if ( m_nGlowRTVIndex == UINT(-1) )
	{
		TRACE( "LimbGlow: Failed to allocate RTV (RTV heap allocated=%u)",
			pDevice->m_RTVHeap.GetNumAllocated() );
		return false;
	}
	pDevice->getDevice()->CreateRenderTargetView( m_pGlowRT.Get(), nullptr,
		pDevice->m_RTVHeap.GetCPUHandle( m_nGlowRTVIndex ) );

	if ( m_nGlowSRVIndex == UINT(-1) )
		m_nGlowSRVIndex = pDevice->m_SRVStagingHeap.Allocate();
	if ( m_nGlowSRVIndex == UINT(-1) )
	{
		TRACE( "LimbGlow: Failed to allocate SRV staging (heap allocated=%u)",
			pDevice->m_SRVStagingHeap.GetNumAllocated() );
		return false;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DisplayDeviceD3D12::SCENE_RT_FORMAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	srvDesc.Texture2D.MipLevels = 1;
	pDevice->getDevice()->CreateShaderResourceView( m_pGlowRT.Get(), &srvDesc,
		pDevice->m_SRVStagingHeap.GetCPUHandle( m_nGlowSRVIndex ) );

	m_bInitialized = true;
	m_bFailed = false;

	TRACE( "LimbGlow initialized: screen=%dx%d, glow=%dx%d",
		width, height, m_GlowSize.width, m_GlowSize.height );
	return true;
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectLimbGlowD3D12::preRender( DisplayDevice * )
{
	return true;
}

//---------------------------------------------------------------------------------------------------

void DisplayEffectLimbGlowD3D12::drawFullscreenTriangle( DisplayDeviceD3D12 * pDevice )
{
	ID3D12GraphicsCommandList * cl = pDevice->getCommandList();
	cl->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	cl->IASetVertexBuffers( 0, 0, nullptr );
	cl->DrawInstanced( 3, 1, 0, 0 );
}

//---------------------------------------------------------------------------------------------------
// Project the current sun-candidate world position (submitted during the
// scene render by NounStar::render → DisplayDevice::submitSunCandidate) to
// a screen-space UV + view-space Z.  Returns true if a sun was submitted
// this frame.  sunVisible=1 when the projection lands in front of the
// camera and within a generous screen margin; 0 when behind the camera or
// far off-screen (the shader uses fSunVisible to short-circuit to black).
// sunViewZ is the sun's camera-forward distance, used by the shader to
// classify foreground/background occluders.
//---------------------------------------------------------------------------------------------------

static bool projectSunToScreen( DisplayDeviceD3D12 * pDevice,
	float & sunU, float & sunV, float & sunVisible, float & sunViewZ )
{
	sunU = 0.5f; sunV = 0.5f; sunVisible = 0.0f; sunViewZ = 0.0f;

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

	// View-space Z — used by the shader to classify occluders as
	// foreground (vZ < sunViewZ) or background (vZ ≥ sunViewZ).
	XMVECTOR sunView = XMVector4Transform( sunHomog, viewMat );
	sunViewZ = XMVectorGetZ( sunView );

	// Reject when the sun's projection is far off-screen.  The rim glow
	// depends on the sun's tangent-plane position vs each occluder's
	// silhouette, so a wildly off-screen sun would still produce valid
	// (though invisible) rim contributions; the bound is mostly a sanity
	// check against degenerate projections.
	if ( sunU < -1.5f || sunU > 2.5f || sunV < -1.5f || sunV > 2.5f )
		return true;

	sunVisible = 1.0f;
	return true;
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectLimbGlowD3D12::postRender( DisplayDevice * pDevice )
{
	DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)pDevice;
	if ( !pDev || !pDev->isCommandListOpen() )
		return false;

	if ( !pDev->m_bFXAAEnabled || !pDev->m_pSceneRT )
		return true;
	if ( DisplayDevice::sm_bUseFixedFunction )
		return true;

	if ( !m_bInitialized && !initLimbGlow( pDev ) )
		return true;

	// Depth SRV must exist — both PS_LimbGlow and PS_Composite read depth at
	// t1 (foreground reject + full-res silhouette gate respectively).
	if ( pDev->m_nDepthSRVIndex == UINT(-1) )
		return true;

	// Resolve the nearest submitted star's screen-space UV + view-space Z.
	// No star → effect skipped; behind camera / far off-screen → shader
	// short-circuits via fSunVisible=0.
	float sunU, sunV, sunVis, sunViewZ;
	if ( !projectSunToScreen( pDev, sunU, sunV, sunVis, sunViewZ ) )
		return true;	// no star submitted this frame, nothing to do

	ID3D12GraphicsCommandList * cl = pDev->getCommandList();
	ID3D12Device * dev = pDev->getDevice();

	pDev->invalidateBoundSRVTable();
	cl->SetGraphicsRootSignature( m_pRootSig.Get() );
	ID3D12DescriptorHeap * heaps[] = { pDev->m_SRVHeap.Get(), pDev->m_SamplerHeap.Get() };
	cl->SetDescriptorHeaps( _countof(heaps), heaps );
	cl->SetGraphicsRootDescriptorTable( 2, pDev->m_SamplerHeap.GetGPUHandle( 2 ) );	// linear clamp

	auto uploadCB = [&]( const CBLimbGlow & cb )
	{
		UploadRingBuffer::Allocation cbAlloc = pDev->allocateCB( sizeof(CBLimbGlow) );
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

	// --- Step 1: render rim glow into half-res RT.  PS_LimbGlow walks
	// the foreground occluder list and accumulates a halo contribution at
	// each sky pixel near a foreground silhouette; foreground pixels are
	// rejected via the depth buffer at t1. ---
	if ( pDev->m_bSceneRTisRT )
	{
		TransitionResource( cl, pDev->m_pSceneRT.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		pDev->m_bSceneRTisRT = false;
	}
	TransitionResource( cl, m_pGlowRT.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );

	// Depth → PSR for the foreground-reject lookup in PS_LimbGlow.  Restored
	// to DEPTH_WRITE at the end of postRender before the DSV is rebound.
	TransitionResource( cl, pDev->m_pDepthStencil.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );

	CBLimbGlow cb = {};
	cb.sunU = sunU;
	cb.sunV = sunV;
	cb.fSunVisible = sunVis;
	cb.fWeight    = m_fWeight;
	cb.fExposure  = m_fExposure;
	cb.fSunViewZ  = sunViewZ;
	cb.fProjNear  = pDev->m_Proj.m_fFront;
	cb.fProjFar   = pDev->m_Proj.m_fBack;

	// Projection matrix _11 / _22 diagonals — used by the shader to
	// convert pixel UV into tangent-plane (z=1) coords for the per-pixel
	// rim test against vOccluders.
	XMMATRIX projMat = pDev->getProjMatrix();
	XMFLOAT4X4 projF;
	XMStoreFloat4x4( &projF, projMat );
	cb.fProjM11 = projF.m[0][0];
	cb.fProjM22 = projF.m[1][1];

	// Pack occluders as TANGENT-PLANE silhouette discs (see CBLimbGlow).
	// Behind-camera occluders get sentinel (w == 0) so the shader skips
	// them; foreground vs background filtering by view-Z is done in the
	// shader (only 0 < w < fSunViewZ contributes to rim glow).
	//
	// Per-pixel occluder loop is the dominant cost; cap by shader tier.
	XMMATRIX viewMat = pDev->getViewMatrix();
	int occluderCap = 32;
	switch ( DisplayDevice::sm_nShaderDetail )
	{
	case DisplayDevice::SHADER_DETAIL_LOW:		occluderCap = 8;  break;
	case DisplayDevice::SHADER_DETAIL_MEDIUM:	occluderCap = 16; break;
	default:									occluderCap = 32; break;
	}
	const int occCount = pDev->getOccluderCount();
	cb.nNumOccluders = ( occCount < occluderCap ) ? occCount : occluderCap;
	cb.fPad0 = 0.0f;

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
			cb.vOccluders[i*4 + 3] = vZ;				// view-space Z, used for foreground/background filter in shader
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

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = pDev->m_RTVHeap.GetCPUHandle( m_nGlowRTVIndex );
	cl->OMSetRenderTargets( 1, &rtv, FALSE, nullptr );
	D3D12_VIEWPORT vp = { 0, 0, (float)m_GlowSize.width, (float)m_GlowSize.height, 0, 1 };
	D3D12_RECT sc   = { 0, 0, (LONG)m_GlowSize.width, (LONG)m_GlowSize.height };
	cl->RSSetViewports( 1, &vp );
	cl->RSSetScissorRects( 1, &sc );
	float clearColor[4] = { 0, 0, 0, 0 };
	cl->ClearRenderTargetView( rtv, clearColor, 0, nullptr );
	cl->SetPipelineState( m_pGlowPSO.Get() );
	drawFullscreenTriangle( pDev );

	// --- Step 2: rim-glow RT → scene RT (full-res, ONE+ONE additive) ---
	// Transition the rim-glow RT to a shader resource and the scene RT
	// back to a render target.  Depth stays in PSR state from step 1 —
	// the composite shader re-tests it at full-res so half-res bilinear
	// filtering doesn't bleed the rim onto adjacent foreground bodies.
	TransitionResource( cl, m_pGlowRT.Get(),
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

	bindSRVs( m_nGlowSRVIndex, pDev->m_nDepthSRVIndex );
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

void DisplayEffectLimbGlowD3D12::release()
{
	m_pGlowRT.Reset();
	m_pGlowPSO.Reset();
	m_pCompositePSO.Reset();
	m_pRootSig.Reset();
	m_pVSBlob.Reset();
	m_pPSGlow.Reset();
	m_pPSComposite.Reset();
	// RTV / SRV staging indices left allocated — same resize recovery pattern
	// as DisplayEffectHDR.
	m_bInitialized = false;
	m_bFailed = false;
}

//---------------------------------------------------------------------------------------------------
// EOF
