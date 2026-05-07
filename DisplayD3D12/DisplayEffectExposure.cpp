/*
	DisplayEffectExposure.cpp - D3D12 version
	Auto-exposure — 64-tap geometric-mean luminance read + EMA-blended
	1x1 R32F exposure multiplier.  Two 1x1 R32F RTs ping-ponged each frame
	(frame N reads from RT[N&1], writes RT[(N+1)&1]) so the read and write
	never overlap in state.  Publishes the freshly-written RT's staging
	SRV index into DisplayDeviceD3D12::m_nCurrentExposureSRVIndex so
	applyFXAA can bind it at t1 for pre-tonemap scaling.
	(c)2024 Palestar
*/

#include "DisplayEffectExposure.h"
#include "Debug/Trace.h"
#include "Standard/Time.h"

//---------------------------------------------------------------------------------------------------

IMPLEMENT_FACTORY( DisplayEffectExposureD3D12, DisplayEffect );

//---------------------------------------------------------------------------------------------------

// CB layout — must match CBExposure in Exposure.hlsl
struct CBExposure
{
	float	fAdaptRate;
	float	fKey;
	float	fMinExposure;
	float	fMaxExposure;

	float	fTime;
	float	pad0;
	float	pad1;
	float	pad2;
};

//---------------------------------------------------------------------------------------------------

DisplayEffectExposureD3D12::DisplayEffectExposureD3D12() :
	m_fAdaptRate( 1.5f ),		// per-second — dark→light settles in ~0.7s
	m_fKey( 0.30f ),			// Chunk-1-follow-up — bumped from canonical 0.18 to lift the scene after sRGB-correct lighting compounded mid-tones darker.
	m_fMinExposure( 0.10f ),
	m_fMaxExposure( 10.0f ),	// Chunk-1-follow-up — extra headroom for dim scenes where the linear-correct multiplier needs to climb further than the legacy 6.0 cap allowed.
	m_nFrameIdx( 0 ),
	m_fLastTickSec( 0.0 ),
	m_bInitialized( false ),
	m_bFailed( false )
{
	for ( int i = 0; i < 2; ++i )
	{
		m_nExposureRTVIndex[i] = UINT(-1);
		m_nExposureSRVIndex[i] = UINT(-1);
	}
}

DisplayEffectExposureD3D12::~DisplayEffectExposureD3D12()
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
			TRACE( "Exposure shader compile error (%s): %s", pEntry, (const char *)errors->GetBufferPointer() );
		return false;
	}
	return true;
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectExposureD3D12::initExposure( DisplayDeviceD3D12 * pDevice )
{
	if ( m_bFailed )
		return false;
	if ( m_bInitialized )
		return true;

	release();
	m_bFailed = true;

	// --- Compile shaders ---
	CharString sPath = DisplayDevice::sm_sShadersPath + "Shaders/Exposure.hlsl";
	wchar_t wszPath[MAX_PATH];
	MultiByteToWideChar( CP_ACP, 0, sPath, -1, wszPath, MAX_PATH );

	if ( !compileShaderEntry( wszPath, "vs_main",          "vs_5_1", m_pVSBlob ) )   { TRACE( "Exposure: Failed VS" );           return false; }
	if ( !compileShaderEntry( wszPath, "PS_AdaptExposure", "ps_5_1", m_pPSAdapt ) )  { TRACE( "Exposure: Failed PS_AdaptExposure" ); return false; }

	// --- Root signature: CBV at b0, 2-SRV table at t0/t1, sampler at s0 ---
	D3D12_ROOT_PARAMETER params[3] = {};

	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[0].Descriptor.ShaderRegister = 0;
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 2;	// t0 scene, t1 prev exposure
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
		if ( err ) TRACE( "Exposure root sig error: %s", (const char *)err->GetBufferPointer() );
		return false;
	}
	hr = pDevice->getDevice()->CreateRootSignature( 0, sig->GetBufferPointer(), sig->GetBufferSize(),
		IID_PPV_ARGS(&m_pRootSig) );
	if ( FAILED(hr) ) { TRACE( "Exposure: Failed root sig" ); return false; }

	// --- Adapt PSO (no blend, writes 1x1 R32F) ---
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_pRootSig.Get();
	psoDesc.VS = { m_pVSBlob->GetBufferPointer(), m_pVSBlob->GetBufferSize() };
	psoDesc.PS = { m_pPSAdapt->GetBufferPointer(), m_pPSAdapt->GetBufferSize() };
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	psoDesc.RasterizerState.DepthClipEnable = FALSE;
	psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	psoDesc.DepthStencilState.DepthEnable = FALSE;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R32_FLOAT;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleMask = UINT_MAX;

	hr = pDevice->getDevice()->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pAdaptPSO) );
	if ( FAILED(hr) ) { TRACE( "Exposure: Failed PSO" ); return false; }

	// --- Ping-pong 1x1 R32F RTs ---
	D3D12_RESOURCE_DESC rtDesc = {};
	rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rtDesc.Width  = 1;
	rtDesc.Height = 1;
	rtDesc.DepthOrArraySize = 1;
	rtDesc.MipLevels = 1;
	rtDesc.Format = DXGI_FORMAT_R32_FLOAT;
	rtDesc.SampleDesc.Count = 1;
	rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_R32_FLOAT;
	clearValue.Color[0] = 1.0f;		// neutral exposure — if anyone clears, they get 1.0

	for ( int i = 0; i < 2; ++i )
	{
		hr = pDevice->getDevice()->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE,
			&rtDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
			IID_PPV_ARGS(&m_pExposureRT[i]) );
		if ( FAILED(hr) ) { TRACE( "Exposure: Failed 1x1 RT %d (hr=0x%08X)", i, hr ); return false; }
		if ( !m_pExposureRT[i] ) { TRACE( "Exposure: 1x1 RT %d null after S_OK", i ); return false; }

		// Guard against heap exhaustion — match HDR/SSAO pattern.  Without this
		// check, GetCPUHandle(UINT(-1)) computes base + (size_t)-1*stride, a wild
		// address that CreateRenderTargetView writes to deep inside D3D12Core.dll.
		// That presents as a crash with no diagnostic; the TRACE turns it into a
		// loggable failure that tells us heap pressure caused it.
		if ( m_nExposureRTVIndex[i] == UINT(-1) )
			m_nExposureRTVIndex[i] = pDevice->m_RTVHeap.Allocate();
		if ( m_nExposureRTVIndex[i] == UINT(-1) )
		{
			TRACE( "Exposure: Failed to allocate RTV %d (RTV heap allocated=%u)",
				i, pDevice->m_RTVHeap.GetNumAllocated() );
			return false;
		}
		pDevice->getDevice()->CreateRenderTargetView( m_pExposureRT[i].Get(), nullptr,
			pDevice->m_RTVHeap.GetCPUHandle( m_nExposureRTVIndex[i] ) );

		if ( m_nExposureSRVIndex[i] == UINT(-1) )
			m_nExposureSRVIndex[i] = pDevice->m_SRVStagingHeap.Allocate();
		if ( m_nExposureSRVIndex[i] == UINT(-1) )
		{
			TRACE( "Exposure: Failed to allocate SRV %d (SRV staging allocated=%u)",
				i, pDevice->m_SRVStagingHeap.GetNumAllocated() );
			return false;
		}

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		pDevice->getDevice()->CreateShaderResourceView( m_pExposureRT[i].Get(), &srvDesc,
			pDevice->m_SRVStagingHeap.GetCPUHandle( m_nExposureSRVIndex[i] ) );
	}

	m_nFrameIdx = 0;
	m_fLastTickSec = 0.0;
	m_bInitialized = true;
	m_bFailed = false;

	TRACE( "Exposure initialized: key=%.3f, range=[%.2f, %.2f], adapt=%.2f/s",
		m_fKey, m_fMinExposure, m_fMaxExposure, m_fAdaptRate );
	return true;
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectExposureD3D12::preRender( DisplayDevice * )
{
	return true;
}

//---------------------------------------------------------------------------------------------------

bool DisplayEffectExposureD3D12::postRender( DisplayDevice * pDevice )
{
	DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)pDevice;
	if ( !pDev || !pDev->isCommandListOpen() )
		return false;
	if ( !pDev->m_bFXAAEnabled || !pDev->m_pSceneRT )
		return true;
	if ( DisplayDevice::sm_bUseFixedFunction )
		return true;

	if ( !m_bInitialized && !initExposure( pDev ) )
		return true;

	// Per-frame dt for EMA rate conversion.  Clamp to reasonable range — at
	// startup / pause / debugger break dt can be huge, which would snap
	// exposure in one step and kill the smoothing.
	double nowSec = (double)Time::milliseconds() * 0.001;
	float dt = (m_fLastTickSec > 0.0) ? (float)(nowSec - m_fLastTickSec) : 1.0f / 60.0f;
	m_fLastTickSec = nowSec;
	dt = Clamp<float>( dt, 0.001f, 0.100f );
	float perCallRate = Clamp<float>( 1.0f - exp( -m_fAdaptRate * dt ), 0.0f, 1.0f );

	int iPrev = m_nFrameIdx & 1;
	int iCurr = iPrev ^ 1;

	ID3D12GraphicsCommandList * cl = pDev->getCommandList();
	ID3D12Device * dev = pDev->getDevice();

	pDev->invalidateBoundSRVTable();
	cl->SetGraphicsRootSignature( m_pRootSig.Get() );
	ID3D12DescriptorHeap * heaps[] = { pDev->m_SRVHeap.Get(), pDev->m_SamplerHeap.Get() };
	cl->SetDescriptorHeaps( _countof(heaps), heaps );
	cl->SetGraphicsRootDescriptorTable( 2, pDev->m_SamplerHeap.GetGPUHandle( 2 ) );	// linear clamp

	// Transition scene to PSR (it may already be PSR if we run after another
	// effect that read it and didn't transition back).
	if ( pDev->m_bSceneRTisRT )
	{
		TransitionResource( cl, pDev->m_pSceneRT.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		pDev->m_bSceneRTisRT = false;
	}

	// Prev exposure RT stays PSR (it was last frame's write target, transitioned
	// to PSR at end of last frame).  Curr exposure RT needs PSR→RT now.
	TransitionResource( cl, m_pExposureRT[iCurr].Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );

	// Bind CB
	CBExposure cb = {};
	cb.fAdaptRate   = perCallRate;
	cb.fKey         = m_fKey;
	cb.fMinExposure = m_fMinExposure;
	cb.fMaxExposure = m_fMaxExposure;
	cb.fTime        = (float)nowSec;
	UploadRingBuffer::Allocation cbAlloc = pDev->allocateCB( sizeof(CBExposure) );
	memcpy( cbAlloc.cpuAddress, &cb, sizeof(cb) );
	cl->SetGraphicsRootConstantBufferView( 0, cbAlloc.gpuAddress );

	// Bind 2-SRV table: t0 scene, t1 prev exposure
	UINT slot = pDev->allocSRVSlots( 2 );
	dev->CopyDescriptorsSimple( 1,
		pDev->m_SRVHeap.GetCPUHandle( slot ),
		pDev->m_SRVStagingHeap.GetCPUHandle( pDev->m_nSceneSRVIndex ),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	dev->CopyDescriptorsSimple( 1,
		pDev->m_SRVHeap.GetCPUHandle( slot + 1 ),
		pDev->m_SRVStagingHeap.GetCPUHandle( m_nExposureSRVIndex[iPrev] ),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	cl->SetGraphicsRootDescriptorTable( 1, pDev->m_SRVHeap.GetGPUHandle( slot ) );

	// Bind curr exposure RT, 1x1 viewport, draw
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = pDev->m_RTVHeap.GetCPUHandle( m_nExposureRTVIndex[iCurr] );
	cl->OMSetRenderTargets( 1, &rtv, FALSE, nullptr );
	D3D12_VIEWPORT vp = { 0, 0, 1, 1, 0, 1 };
	D3D12_RECT sc = { 0, 0, 1, 1 };
	cl->RSSetViewports( 1, &vp );
	cl->RSSetScissorRects( 1, &sc );

	cl->SetPipelineState( m_pAdaptPSO.Get() );
	cl->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	cl->IASetVertexBuffers( 0, 0, nullptr );
	cl->DrawInstanced( 3, 1, 0, 0 );

	// Transition curr back to PSR so applyFXAA (and next frame's read) can sample it
	TransitionResource( cl, m_pExposureRT[iCurr].Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );

	// Publish the fresh SRV staging index to the device so applyFXAA binds it at t1
	pDev->m_nCurrentExposureSRVIndex = m_nExposureSRVIndex[iCurr];

	++m_nFrameIdx;

	// --- Restore main pipeline state ---
	cl->SetGraphicsRootSignature( pDev->getRootSignature() );
	cl->SetDescriptorHeaps( _countof(heaps), heaps );
	pDev->invalidateBoundSRVTable();
	cl->SetGraphicsRootDescriptorTable( 4, pDev->m_SRVHeap.GetGPUHandle( 0 ) );
	cl->SetGraphicsRootDescriptorTable( 5, pDev->m_SamplerHeap.GetGPUHandle( 0 ) );
	// Restore scene RT as current target for subsequent effects
	D3D12_CPU_DESCRIPTOR_HANDLE sceneRTV = pDev->m_RTVHeap.GetCPUHandle( pDev->m_nSceneRTVIndex );
	if ( !pDev->m_bSceneRTisRT )
	{
		TransitionResource( cl, pDev->m_pSceneRT.Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );
		pDev->m_bSceneRTisRT = true;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = pDev->m_DSVHeap.GetCPUHandle( 0 );
	cl->OMSetRenderTargets( 1, &sceneRTV, FALSE, &dsvHandle );

	RectInt rw = pDev->renderWindow();
	D3D12_VIEWPORT svp = { 0, 0, (float)rw.width(), (float)rw.height(), 0, 1 };
	D3D12_RECT ssc   = { 0, 0, (LONG)rw.width(), (LONG)rw.height() };
	cl->RSSetViewports( 1, &svp );
	cl->RSSetScissorRects( 1, &ssc );

	return true;
}

//---------------------------------------------------------------------------------------------------

void DisplayEffectExposureD3D12::release()
{
	for ( int i = 0; i < 2; ++i )
		m_pExposureRT[i].Reset();
	m_pAdaptPSO.Reset();
	m_pRootSig.Reset();
	m_pVSBlob.Reset();
	m_pPSAdapt.Reset();
	m_bInitialized = false;
	m_bFailed = false;
}

//---------------------------------------------------------------------------------------------------
// EOF
