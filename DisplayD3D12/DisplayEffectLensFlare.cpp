/*
	DisplayEffectLensFlare.cpp - D3D12 version
	Anamorphic lens flare: wide horizontal streak + subtler vertical
	secondary + tight central halo, rendered per visible star (top-N
	from DisplayDevice::getStar) with apparent-size shape and intensity
	scaling — distant stars produce tiny dim flares, mid-distance stars
	produce full streaks, close stars collapse the flare so the surface
	dominates.  Composited additively into the un-bloomed scene RT
	before HDR so bloom amplifies the streak.  Gated per-star by a
	depth-buffer sample at the star's screen UV — disappears when a
	foreground body occludes that star; LimbGlow takes over in that
	case.
	(c)2026 Palestar
*/

#include "DisplayEffectLensFlare.h"
#include "Debug/Trace.h"
#include "Standard/Constant.h"
#include <DirectXMath.h>

using namespace DirectX;

//---------------------------------------------------------------------------------------------------

IMPLEMENT_FACTORY( DisplayEffectLensFlareD3D12, DisplayEffect );

//---------------------------------------------------------------------------------------------------

// CB layout — must match CBLensFlare in LensFlare.hlsl exactly.  16-byte
// aligned in 4-element float groups.
//
// The shader iterates vStars[] and additively accumulates a flare per
// visible star, with apparent-size fade (small/distant ⇒ tiny dim flare,
// mid-distance ⇒ peak streak, close ⇒ collapses).  Per-star packed format:
//   x = screen UV.x     [0,1]
//   y = screen UV.y     [0,1]
//   z = view-space Z    (camera-forward distance, world units; 0 = sentinel "skip")
//   w = world radius    (drives angular-size computation in shader)
struct CBLensFlare
{
	float	fIntensity;
	float	fProjNear;
	float	fProjFar;
	int		nNumStars;

	float	vStars[ DisplayDevice::MAX_STARS * 4 ];
};

//---------------------------------------------------------------------------------------------------

DisplayEffectLensFlareD3D12::DisplayEffectLensFlareD3D12() :
	m_fIntensity( 1.0f ),	// pre-bloom intensity; HDR bloom amplifies by ~2-3× through the mip chain
	m_LastSize( 0, 0 ),
	m_bInitialized( false ),
	m_bFailed( false )
{
}

DisplayEffectLensFlareD3D12::~DisplayEffectLensFlareD3D12()
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
			TRACE( "LensFlare shader compile error (%s): %s", pEntry, (const char *)errors->GetBufferPointer() );
		return false;
	}
	return true;
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectLensFlareD3D12::initLensFlare( DisplayDeviceD3D12 * pDevice )
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

	// --- Compile shaders ---
	CharString sPath = DisplayDevice::sm_sShadersPath + "Shaders/LensFlare.hlsl";
	wchar_t wszPath[MAX_PATH];
	MultiByteToWideChar( CP_ACP, 0, sPath, -1, wszPath, MAX_PATH );

	if ( !compileShaderEntry( wszPath, "vs_main",      "vs_5_1", m_pVSBlob ) )  { TRACE( "LensFlare: Failed VS" );           return false; }
	if ( !compileShaderEntry( wszPath, "PS_LensFlare", "ps_5_1", m_pPSFlare ) ) { TRACE( "LensFlare: Failed PS_LensFlare" );  return false; }

	// --- Root signature: CBV (b0) + 2-SRV table (t0 unused, t1 depth) +
	// sampler table (linear-clamp).  Mirrors LimbGlow's root-sig shape so
	// the descriptor-heap setup at the call site can be identical.
	D3D12_ROOT_PARAMETER params[3] = {};

	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].Descriptor.ShaderRegister = 0;
	params[0].Descriptor.RegisterSpace = 0;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 2;		// t0 unused (don't-care bind), t1 depth
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
		if ( err ) TRACE( "LensFlare root sig error: %s", (const char *)err->GetBufferPointer() );
		return false;
	}
	hr = pDevice->getDevice()->CreateRootSignature( 0, sig->GetBufferPointer(), sig->GetBufferSize(),
		IID_PPV_ARGS(&m_pRootSig) );
	if ( FAILED(hr) ) { TRACE( "LensFlare: Failed root sig" ); return false; }

	// --- PSO: full-screen triangle, additive blend onto scene RT ---
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_pRootSig.Get();
	psoDesc.VS = { m_pVSBlob->GetBufferPointer(), m_pVSBlob->GetBufferSize() };
	psoDesc.PS = { m_pPSFlare->GetBufferPointer(), m_pPSFlare->GetBufferSize() };
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

	psoDesc.BlendState.RenderTarget[0].BlendEnable    = TRUE;
	psoDesc.BlendState.RenderTarget[0].SrcBlend       = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlend      = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].BlendOp        = D3D12_BLEND_OP_ADD;
	psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha  = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	psoDesc.BlendState.RenderTarget[0].BlendOpAlpha   = D3D12_BLEND_OP_ADD;

	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pFlarePSO) );
	if ( FAILED(hr) ) { TRACE( "LensFlare: Failed PSO" ); return false; }

	m_bInitialized = true;
	m_bFailed = false;

	TRACE( "LensFlare initialized: screen=%dx%d", width, height );
	return true;
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectLensFlareD3D12::preRender( DisplayDevice * )
{
	return true;
}

//---------------------------------------------------------------------------------------------------

void DisplayEffectLensFlareD3D12::drawFullscreenTriangle( DisplayDeviceD3D12 * pDevice )
{
	ID3D12GraphicsCommandList * cl = pDevice->getCommandList();
	cl->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	cl->IASetVertexBuffers( 0, 0, nullptr );
	cl->DrawInstanced( 3, 1, 0, 0 );
}

//---------------------------------------------------------------------------------------------------
// Project a single star world position into screen-space packed format.
// Writes (UV.x, UV.y, viewZ, radius) into out4.  viewZ=0 is the sentinel
// the shader uses to skip a slot (behind camera or off-screen).
//---------------------------------------------------------------------------------------------------

static void projectStar( const XMMATRIX & viewMat, const XMMATRIX & viewProj,
	const Vector3 & worldPos, float radius, float * out4 )
{
	out4[ 0 ] = 0.0f; out4[ 1 ] = 0.0f; out4[ 2 ] = 0.0f; out4[ 3 ] = 0.0f;

	XMVECTOR pHomog = XMVectorSet( worldPos.x, worldPos.y, worldPos.z, 1.0f );
	XMVECTOR clip   = XMVector4Transform( pHomog, viewProj );

	float w = XMVectorGetW( clip );
	if ( w <= 0.0f )
		return;	// behind camera ⇒ leave viewZ=0 sentinel

	float ndcX = XMVectorGetX( clip ) / w;
	float ndcY = XMVectorGetY( clip ) / w;
	float u    =  ndcX * 0.5f + 0.5f;
	float v    = -ndcY * 0.5f + 0.5f;

	if ( u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f )
		return;	// off-screen ⇒ shader can't depth-sample, skip

	XMVECTOR pView = XMVector4Transform( pHomog, viewMat );
	float    viewZ = XMVectorGetZ( pView );
	if ( viewZ <= 0.0f )
		return;	// guard against grazing-clip edge cases

	out4[ 0 ] = u;
	out4[ 1 ] = v;
	out4[ 2 ] = viewZ;
	out4[ 3 ] = radius;
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectLensFlareD3D12::postRender( DisplayDevice * pDevice )
{
	DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)pDevice;
	if ( !pDev || !pDev->isCommandListOpen() )
		return false;

	if ( !pDev->m_bFXAAEnabled || !pDev->m_pSceneRT )
		return true;
	if ( DisplayDevice::sm_bUseFixedFunction )
		return true;

	if ( !m_bInitialized && !initLensFlare( pDev ) )
		return true;

	// Depth SRV must exist — the shader samples it at each star's UV for
	// the per-star foreground-occlusion gate.
	if ( pDev->m_nDepthSRVIndex == UINT(-1) )
		return true;

	const int starCount = pDev->getStarCount();
	if ( starCount <= 0 )
		return true;	// no stars submitted this frame

	XMMATRIX viewMat  = pDev->getViewMatrix();
	XMMATRIX projMat  = pDev->getProjMatrix();
	XMMATRIX viewProj = XMMatrixMultiply( viewMat, projMat );

	// Pre-project all stars to screen-space packed format; we'll skip the
	// draw entirely if NONE of them landed on-screen.
	float vStars[ DisplayDevice::MAX_STARS * 4 ] = {};
	int   onScreenCount = 0;
	for ( int i = 0; i < starCount; ++i )
	{
		const DisplayDevice::StarInfo & s = pDev->getStar( i );
		float * out4 = &vStars[ i * 4 ];
		projectStar( viewMat, viewProj, s.worldPos, s.radius, out4 );
		if ( out4[ 2 ] > 0.0f )		// viewZ sentinel — non-zero ⇒ on-screen
			++onScreenCount;
	}
	if ( onScreenCount <= 0 )
		return true;	// every star behind camera or off-screen, nothing to flare

	ID3D12GraphicsCommandList * cl = pDev->getCommandList();
	ID3D12Device * dev = pDev->getDevice();

	pDev->invalidateBoundSRVTable();
	cl->SetGraphicsRootSignature( m_pRootSig.Get() );
	ID3D12DescriptorHeap * heaps[] = { pDev->m_SRVHeap.Get(), pDev->m_SamplerHeap.Get() };
	cl->SetDescriptorHeaps( _countof(heaps), heaps );
	cl->SetGraphicsRootDescriptorTable( 2, pDev->m_SamplerHeap.GetGPUHandle( 2 ) );	// linear clamp

	// Transitions: scene RT must be RT, depth must be PSR.  Either may
	// already be in the correct state (LimbGlow runs just before us in the
	// effect order and leaves them set up similarly), so the boolean
	// guards short-circuit unnecessary barriers.
	if ( !pDev->m_bSceneRTisRT )
	{
		TransitionResource( cl, pDev->m_pSceneRT.Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );
		pDev->m_bSceneRTisRT = true;
	}
	TransitionResource( cl, pDev->m_pDepthStencil.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );

	// Bind SRVs — t0 is don't-care (shader doesn't reference it), t1 is depth.
	UINT slot = pDev->allocSRVSlots( 2 );
	dev->CopyDescriptorsSimple( 1,
		pDev->m_SRVHeap.GetCPUHandle( slot ),
		pDev->m_SRVStagingHeap.GetCPUHandle( pDev->m_nSceneSRVIndex ),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	dev->CopyDescriptorsSimple( 1,
		pDev->m_SRVHeap.GetCPUHandle( slot + 1 ),
		pDev->m_SRVStagingHeap.GetCPUHandle( pDev->m_nDepthSRVIndex ),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	cl->SetGraphicsRootDescriptorTable( 1, pDev->m_SRVHeap.GetGPUHandle( slot ) );

	// Upload CB.
	CBLensFlare cb = {};
	cb.fIntensity   = m_fIntensity;
	cb.fProjNear    = pDev->m_Proj.m_fFront;
	cb.fProjFar     = pDev->m_Proj.m_fBack;
	cb.nNumStars    = starCount;
	memcpy( cb.vStars, vStars, sizeof(vStars) );

	UploadRingBuffer::Allocation cbAlloc = pDev->allocateCB( sizeof(CBLensFlare) );
	memcpy( cbAlloc.cpuAddress, &cb, sizeof(cb) );
	cl->SetGraphicsRootConstantBufferView( 0, cbAlloc.gpuAddress );

	// Bind scene RT and full-res viewport.
	D3D12_CPU_DESCRIPTOR_HANDLE sceneRTV = pDev->m_RTVHeap.GetCPUHandle( pDev->m_nSceneRTVIndex );
	cl->OMSetRenderTargets( 1, &sceneRTV, FALSE, nullptr );

	RectInt rw = pDev->renderWindow();
	D3D12_VIEWPORT vp = { 0, 0, (float)rw.width(), (float)rw.height(), 0, 1 };
	D3D12_RECT sc    = { 0, 0, (LONG)rw.width(), (LONG)rw.height() };
	cl->RSSetViewports( 1, &vp );
	cl->RSSetScissorRects( 1, &sc );

	cl->SetPipelineState( m_pFlarePSO.Get() );
	drawFullscreenTriangle( pDev );

	// Restore depth back to DEPTH_WRITE for the next OMSetRenderTargets
	// (DSV bind) and any subsequent post effects.
	TransitionResource( cl, pDev->m_pDepthStencil.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE );

	// Restore main pipeline state.
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

void DisplayEffectLensFlareD3D12::release()
{
	m_pFlarePSO.Reset();
	m_pRootSig.Reset();
	m_pVSBlob.Reset();
	m_pPSFlare.Reset();
	m_bInitialized = false;
	m_bFailed = false;
}

//---------------------------------------------------------------------------------------------------
// EOF
