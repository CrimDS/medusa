/*
	DisplayDeviceD3D12.cpp
	DirectX 12 Display Device Implementation
	(c)2024 Palestar
*/

//#define PROFILE_OFF

#include "Debug/Trace.h"
#include "Debug/Assert.h"
#include "Debug/Profile.h"
#include "File/FileDisk.h"
#include "File/Path.h"
#include "Math/Constants.h"
#include "Math/Plane.h"
#include "Math/SphericalHull.h"
#include "Standard/Limits.h"
#include "Standard/AutoLock.h"
#include "Standard/ThreadPool.h"
#include "Render3D/RenderContext.h"
#include "Render3D/Material.h"
#include "Draw/ImageCodec.h"

#include "Display/Types.h"
#include "DisplayDeviceD3D12.h"

#include <stdint.h>
#include <algorithm>
#include <climits>
#include <cstdio>
#include <string>
#include <vector>
#include <DirectXPackedVector.h>	// XMConvertFloatToHalf for PBR IBL bake
#include "PrimitiveFactory.h"
#include "PrimitiveSurfaceD3D12.h"
#include "PrimitiveMaterialD3D12.h"
#include "PrimitiveWindowD3D12.h"
#include "DisplayEffectHDR.h"
#include "DisplayEffectBlur.h"
#include "DisplayEffectSSAO.h"
#include "DisplayEffectLensFlare.h"
#include "DisplayEffectLimbGlow.h"
#include "DisplayEffectExposure.h"

#include <math.h>

// Forward declarations for texture helpers defined in PrimitiveSurfaceD3D12.cpp
int  GetNativePixelBytes( ColorFormat::Format eFormat );
int  GetBytesPerPixel( DXGI_FORMAT format );
bool IsBlockCompressed( DXGI_FORMAT format );
DXGI_FORMAT GetSRVFormat( DXGI_FORMAT fmt, bool bSRGB );
bool IsSRGBColourTexture( PrimitiveSurface::Type eType );

//---------------------------------------------------------------------------------------------------

DisplayDeviceD3D12::ModeList	DisplayDeviceD3D12::sm_ModeList;
DisplayDeviceD3D12::DeviceList	DisplayDeviceD3D12::sm_DeviceList;

// sRGB-correct pipeline (Chunk 1).  When true:
//  - Colour textures (DIFFUSE / LIGHTMAP / DARKMAP / DECALMAP) are sampled
//    through an _SRGB SRV view, hardware-decoding to linear at sample.
//    Bump / normal / gloss / shader textures stay UNORM (linear data).
//  - Author-time sRGB CB colours (vMatDiffuse/Specular/Ambient/Emissive,
//    vLightDiffuse/Specular, vGlobalAmbient) are linearised at upload
//    via srgbToLinear() so they meet linear texture samples in the same
//    space, making lighting math energy-conserving in linear domain.
//  - Output gamma encode is NOT applied here — see createRenderTargets
//    for why.  The existing ACES Narkowicz tonemap (FXAA.hlsl:66-74) is
//    fitted to the full RRT+ODT.SDR.sRGB curve, so its output is already
//    sRGB-encoded for direct display.  An _SRGB swap-chain RTV would
//    double-encode and wash everything out.
// sRGB-correct pipeline (Chunk 1).  When true, colour textures are sampled
// through _SRGB SRV views (hardware-decoded to linear), CB colour values
// are linearised at upload, and lighting math runs in linear space.
// Toggle to false to restore the legacy gamma-wrong path as an A/B
// comparison switch.
bool g_bSRGBPipeline = true;

// Light-intensity scale (Chunk 1 follow-up).  Compensates for the typical
// sRGB-linearisation darkening of authored light colours.  1.8 was the
// end-of-Chunk-1 value the user accepted as a baseline.
float g_fLightIntensityScale = 1.8f;

//---------------------------------------------------------------------------------------------------

const float DEFAULT_SHADOW_RADIUS = 5000.0f;
const int	DEFAULT_SHADOW_MAP_SIZE = 4096;
static const float CASCADE_SPLIT_RATIOS[NUM_SHADOW_CASCADES] = { 0.08f, 0.24f, 0.60f, 1.0f };

//---------------------------------------------------------------------------------------------------

IMPLEMENT_FACTORY( DisplayDeviceD3D12, DisplayDevice );

DisplayDeviceD3D12::DisplayDeviceD3D12() :
	m_dwLockingThread( 0 ),
	m_nLockCount( 0 ),
	m_bBeginScene( false ),
	m_bCommandListOpen( false ),
	m_nSRVFrameOffset( 8 ),
	m_nSRVTextureBase( 8 ),
	m_nTextureStage( 0 ),
	m_TextureP2( false ),
	m_TextureSquare( false ),
	m_TextureMaxSize( 16384, 16384 ),
	m_TextureMinSize( 1, 1 ),
	m_eFilterMode( FILTER_ANISOTROPIC ),
	m_HWND( NULL ),
	m_bHardware( true ),
	m_bWindowed( false ),
	m_eFSAA( FSAA_NONE ),
	m_ClientRectangle( 0, 0, -1, -1 ),
	m_bMinimized( false ),
	m_eFillMode( FILL_SOLID ),
	m_nFrameIndex( 0 ),
	m_hFenceEvent( NULL ),
	m_cAmbientLight( BLACK ),
	m_fShadowDepthRange( 0.0f ),
	m_bShadowMapReady( false ),
	m_bShadowMapSupported( true ),
	m_bShadowPass( false ),
	m_nMaxShadowLights( 0 ),
	m_nShadowMapPass( 0 ),
	m_bUsingFixedFunction( false ),
	m_nCurrentBlend( 0 ),
	m_bCurrentDoubleSided( false ),
	m_bCurrentForceDepthWrite( false ),
	m_bRenderingShadowMap( false ),
	m_bShadowMapInRTState( false ),
	m_bFirstShadowPass( true ),
	m_vShadowFocus( 0, 0, 1.0f ),
	m_fShadowRadius( DEFAULT_SHADOW_RADIUS ),
	m_szShadowMap( DEFAULT_SHADOW_MAP_SIZE, DEFAULT_SHADOW_MAP_SIZE ),
	m_nSceneRTVIndex( UINT(-1) ),
	m_nSceneSRVIndex( UINT(-1) ),
	m_bSceneRTEnabled( true ),
	m_eAAMode( AA_FXAA ),
	m_bSceneRTisRT( false ),
	m_bRenderingPostAA( false ),
	m_nSMAAEdgeRTVIndex( UINT(-1) ),
	m_nSMAAEdgeSRVIndex( UINT(-1) ),
	m_nSMAAWeightsRTVIndex( UINT(-1) ),
	m_nSMAAWeightsSRVIndex( UINT(-1) ),
	m_LastSMAASize( 0, 0 ),
	m_bSMAAAvailable( false ),
	m_nDefaultExposureSRVIndex( UINT(-1) ),
	m_nDefaultExposureRTVIndex( UINT(-1) ),
	m_bDefaultExposureInitialized( false ),
	m_nCurrentExposureSRVIndex( UINT(-1) ),
	m_nShadowMapDSVIndex( UINT(-1) ),
	m_nShadowMapSRVStagingIndex( UINT(-1) ),
	m_nBRDFLUTSRVStagingIndex( UINT(-1) ),
	m_nEnvCubeSRVStagingIndex( UINT(-1) ),
	m_bPBRIBLReady( false ),
	m_nLastBakeTick( 0 ),
	m_nDepthSRVIndex( UINT(-1) ),
	m_nUploadFenceValue( 0 ),
	m_hUploadFenceEvent( NULL ),
	m_pCurrentPSO( nullptr ),
	m_SRVSlotEpoch( 1 ),
	m_eDepthStencilState( D3D12_RESOURCE_STATE_DEPTH_WRITE ),
	m_LightCBRingHead( 0 ),
	m_nDSVIndex( UINT(-1) ),
	m_nLastBoundSRVBase( 0 ),
	m_bLastBoundSRVValid( false ),
	m_nSRVBindCalls( 0 ),
	m_nSRVBindSkipped( 0 ),
	m_nMatCBUploads( 0 ),
	m_nMatCBSkipped( 0 ),
	m_nLightCBUploads( 0 ),
	m_nLightCBSkipped( 0 ),
	m_nObjCBUploads( 0 ),
	m_nObjCBSkipped( 0 ),
	m_nSRVCopies( 0 ),
	m_nSRVCopiesSkipped( 0 ),
	m_bLastMatCBValid( false ),
	m_nLastMatCBGpuVA( 0 ),
	m_bLastLightCBValid( false ),
	m_nLastLightCBGpuVA( 0 ),
	m_bLastObjCBValid( false ),
	m_nLastObjCBGpuVA( 0 )
{
	memset( m_nRTVIndices, 0xff, sizeof(m_nRTVIndices) );
	memset( m_LightCBRingValid, 0, sizeof(m_LightCBRingValid) );
	memset( m_LightCBRingVA,    0, sizeof(m_LightCBRingVA) );
	TRACE( "DisplayDeviceD3D12 created!" );

	memset( m_nFenceValues, 0, sizeof(m_nFenceValues) );
	memset( m_nAllocatorFence, 0, sizeof(m_nAllocatorFence) );
	memset( m_vLastBakeSunDir, 0, sizeof(m_vLastBakeSunDir) );
	memset( m_vLastBakeSunRGB, 0, sizeof(m_vLastBakeSunRGB) );
	memset( m_vLastBakeSkyRGB, 0, sizeof(m_vLastBakeSkyRGB) );

	m_mView = XMMatrixIdentity();
	m_mProj = XMMatrixIdentity();
	m_mCurrentWorld = XMMatrixIdentity();

	lock();
	enumerateModes();
	unlock();

	// Register supported effects
	registerEffect( "HDR", DisplayEffectHDRD3D12::staticFactory() );
	registerEffect( "BLUR", DisplayEffectBlurD3D12::staticFactory() );
	registerEffect( "SSAO", DisplayEffectSSAOD3D12::staticFactory() );
	registerEffect( "LIMBGLOW", DisplayEffectLimbGlowD3D12::staticFactory() );
	registerEffect( "LENSFLARE", DisplayEffectLensFlareD3D12::staticFactory() );
	registerEffect( "EXPOSURE", DisplayEffectExposureD3D12::staticFactory() );
}

DisplayDeviceD3D12::~DisplayDeviceD3D12()
{
	TRACE( "DisplayDeviceD3D12 destroyed!" );
	release();
}

//------------------------------------------------------------------------------------------------
// Accessors
//------------------------------------------------------------------------------------------------

bool DisplayDeviceD3D12::isLocked() const
{
	return ::GetCurrentThreadId() == m_dwLockingThread;
}

int DisplayDeviceD3D12::modeCount() const
{
	return sm_ModeList.size();
}

const DisplayDevice::Mode * DisplayDeviceD3D12::mode( int n ) const
{
	return &sm_ModeList[ n ];
}

ColorFormat * DisplayDeviceD3D12::primaryFormat() const
{
	return m_pFormat;
}

int DisplayDeviceD3D12::surfaceFormatCount() const
{
	return m_TextureFormats.size();
}

ColorFormat::Format DisplayDeviceD3D12::surfaceFormat( int n ) const
{
	return m_TextureFormats[ n ];
}

const DisplayDevice::Mode * DisplayDeviceD3D12::activeMode() const
{
	return &m_Mode;
}

bool DisplayDeviceD3D12::windowed() const
{
	return m_bWindowed;
}

RectInt DisplayDeviceD3D12::clientWindow() const
{
	return m_ClientRectangle;
}

RectInt DisplayDeviceD3D12::renderWindow() const
{
	// Use the tracked client rectangle in both modes.  Borderless fullscreen
	// sizes the window to the monitor bounds, which may not match m_Mode.screenSize
	// (the user-configured "mode" is independent of the monitor's actual resolution).
	// Callers need the actual render surface size, so always report m_ClientRectangle.
	if ( m_ClientRectangle.width() > 0 && m_ClientRectangle.height() > 0 )
		return RectInt( PointInt(0,0), m_ClientRectangle.size() );
	return RectInt( PointInt(0,0), m_Mode.screenSize );
}

bool DisplayDeviceD3D12::textureP2() const { return m_TextureP2; }
bool DisplayDeviceD3D12::textureSquare() const { return m_TextureSquare; }
SizeInt DisplayDeviceD3D12::textureMaxSize() const { return m_TextureMaxSize; }
SizeInt DisplayDeviceD3D12::textureMinSize() const { return m_TextureMinSize; }
float DisplayDeviceD3D12::pixelShaderVersion() const { return 5.1f; }
float DisplayDeviceD3D12::vertexShaderVersion() const { return 5.1f; }
DisplayDevice::FilterMode DisplayDeviceD3D12::filterMode() const { return m_eFilterMode; }

dword DisplayDeviceD3D12::totalVideoMemory() const
{
	if ( m_pAdapter )
	{
		DXGI_ADAPTER_DESC1 desc;
		m_pAdapter->GetDesc1( &desc );
		return (dword)(desc.DedicatedVideoMemory / 1024);
	}
	return 0;
}

dword DisplayDeviceD3D12::freeVideoMemory() const
{
	return totalVideoMemory();	// DX12 doesn't provide free memory directly
}

//------------------------------------------------------------------------------------------------
// Lock / Unlock
//------------------------------------------------------------------------------------------------

void DisplayDeviceD3D12::lock()
{
	m_Lock.lock();
	if ( m_nLockCount == 0 )
		m_dwLockingThread = ::GetCurrentThreadId();
	++m_nLockCount;
}

void DisplayDeviceD3D12::unlock()
{
	if ( --m_nLockCount <= 0 )
		m_dwLockingThread = 0;
	m_Lock.unlock();
}

//------------------------------------------------------------------------------------------------
// Initialize / Release
//------------------------------------------------------------------------------------------------

bool DisplayDeviceD3D12::initialize( void * hWnd, const Mode * pMode, bool bWindowed, bool bHardware, FSAA eFSAA )
{
	if ( pMode == NULL && !bWindowed )
		return false;

	lock();
	release();

	m_HWND = static_cast<HWND>( hWnd );
	m_bHardware = bHardware;
	m_eFSAA = eFSAA;

	// Translate FSAA enum → AA mode for the post-process AA dispatch.  FSAA
	// historically picked hardware MSAA sample count (D3D9); under D3D12 the
	// same setting now selects the post-process AA path:
	//   FSAA_NONE        → AA_NONE (tonemap only)
	//   FSAA_NONMASKABLE → AA_FXAA (single-pass FXAA)
	//   FSAA_2..16       → AA_SMAA (3-pass MLAA-style edge AA; quality preset
	//                      comes from sm_nShaderDetail, applied at PSO compile
	//                      time inside createSMAA)
	// Unknown values default to AA_FXAA so a config that pre-dates the AA
	// mapping behaves identically to before.
	if ( eFSAA == FSAA_NONE )
		m_eAAMode = AA_NONE;
	else if ( eFSAA == FSAA_NONMASKABLE )
		m_eAAMode = AA_FXAA;
	else
		m_eAAMode = AA_SMAA;

	if ( pMode != NULL )
	{
		m_Mode = *pMode;
		m_pFormat = ColorFormat::allocateFormat( pMode->colorFormat );
	}
	else
	{
		if ( !bWindowed )
		{
			TRACE( "Mode required for non-windowed modes!" );
			unlock();
			return false;
		}
		m_pFormat = ColorFormat::allocateFormat( ColorFormat::RGB888 );
	}

	m_bWindowed = bWindowed;
	m_ClientPlacement.length = sizeof( m_ClientPlacement );
	GetWindowPlacement( m_HWND, &m_ClientPlacement );

	if ( !initializeD3D12() )
	{
		unlock();
		return false;
	}

	sm_DeviceList.push( this );
	unlock();

	return true;
}

bool DisplayDeviceD3D12::setMode( const Mode * pMode, bool bWindowed )
{
	if ( pMode == NULL && !bWindowed )
		return false;

	const bool wasWindowed = m_bWindowed;
	if ( pMode != NULL )
		m_Mode = *pMode;
	m_bWindowed = bWindowed;

	// ALT+ENTER arrives via WM_SYSKEYDOWN in PlatformWin::winProc and can hit
	// mid-frame — command list may be open with backbuffer RTV references
	// recorded.  ResizeBuffers requires zero outstanding backbuffer refs
	// including those inside an open command list.
	//
	// flushCommandList closes AND executes the list.  The render commands
	// target a backbuffer that's about to be discarded — that rendering is
	// visually lost, which is fine (one throwaway frame).  Crucially,
	// texture-upload copies queued in the same list by flushPendingUploads
	// (CopyTextureRegion into D3DPOOL_DEFAULT textures) execute before
	// waitForGPU.  A bare Close() without Execute() was dropping those
	// uploads — surface resources survived but with no pixel data, so
	// first-frame-after-resize sampling returned zero (black-screen at
	// ~1/3 launches; specific-texture-missing the rest of the time).
	if ( m_bCommandListOpen )
		flushCommandList();
	waitForGPU();

	for ( UINT i = 0; i < FRAME_COUNT; i++ )
		m_pRenderTargets[i].Reset();
	m_pDepthStencil.Reset();

	// Choose target backbuffer size.  Borderless-fullscreen sizes to the
	// monitor the window is on; windowed sizes to whatever the HWND's client
	// rect becomes after the style/placement changes below.
	UINT targetW = m_Mode.screenSize.width;
	UINT targetH = m_Mode.screenSize.height;

	if ( IsWindow( m_HWND ) )
	{
		if ( !bWindowed )
		{
			// Going to (or staying in) borderless fullscreen.
			// On the first transition out of windowed, capture placement so
			// we can restore it later.
			if ( wasWindowed )
			{
				m_ClientPlacement.length = sizeof( m_ClientPlacement );
				GetWindowPlacement( m_HWND, &m_ClientPlacement );
			}

			HMONITOR hMon = MonitorFromWindow( m_HWND, MONITOR_DEFAULTTONEAREST );
			MONITORINFO mi = {};
			mi.cbSize = sizeof( mi );
			if ( GetMonitorInfo( hMon, &mi ) )
			{
				LONG style = GetWindowLong( m_HWND, GWL_STYLE );
				SetWindowLong( m_HWND, GWL_STYLE, ( style & ~WS_OVERLAPPEDWINDOW ) | WS_POPUP );
				SetWindowPos( m_HWND, HWND_TOP,
					mi.rcMonitor.left, mi.rcMonitor.top,
					mi.rcMonitor.right - mi.rcMonitor.left,
					mi.rcMonitor.bottom - mi.rcMonitor.top,
					SWP_NOZORDER | SWP_FRAMECHANGED );

				targetW = (UINT)( mi.rcMonitor.right - mi.rcMonitor.left );
				targetH = (UINT)( mi.rcMonitor.bottom - mi.rcMonitor.top );

				// Sync m_ClientRectangle so renderWindow() reports the actual
				// render surface.  updateClientArea's GetClientRect path is
				// gated on m_bWindowed and won't update this in FS mode.
				m_ClientRectangle = RectInt(
					mi.rcMonitor.left, mi.rcMonitor.top,
					mi.rcMonitor.right - 1, mi.rcMonitor.bottom - 1 );
			}
		}
		else
		{
			// Returning to (or staying in) windowed mode.
			LONG style = GetWindowLong( m_HWND, GWL_STYLE );
			const LONG newStyle = ( style & ~WS_POPUP ) | WS_OVERLAPPEDWINDOW;
			SetWindowLong( m_HWND, GWL_STYLE, newStyle );

			if ( !wasWindowed )
			{
				// Coming out of fullscreen — restore saved windowed placement.
				if ( m_ClientPlacement.length == sizeof( m_ClientPlacement ) )
					SetWindowPlacement( m_HWND, &m_ClientPlacement );
				SetWindowPos( m_HWND, HWND_TOP, 0, 0, 0, 0,
					SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED );
			}
			else if ( pMode != NULL )
			{
				// Staying windowed but the user picked a new mode — resize
				// the window so the CLIENT area matches the requested mode.
				// AdjustWindowRect adds border/titlebar overhead so the outer
				// rect we pass to SetWindowPos produces the right inner size.
				RECT rect = { 0, 0,
					(LONG)m_Mode.screenSize.width,
					(LONG)m_Mode.screenSize.height };
				AdjustWindowRect( &rect, newStyle, FALSE );
				SetWindowPos( m_HWND, HWND_TOP, 0, 0,
					rect.right - rect.left,
					rect.bottom - rect.top,
					SWP_NOZORDER | SWP_NOMOVE | SWP_FRAMECHANGED );
			}
			else
			{
				// No mode change and already windowed — just refresh the frame.
				SetWindowPos( m_HWND, HWND_TOP, 0, 0, 0, 0,
					SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED );
			}

			RECT rc;
			GetClientRect( m_HWND, &rc );
			if ( rc.right > rc.left && rc.bottom > rc.top )
			{
				targetW = (UINT)( rc.right - rc.left );
				targetH = (UINT)( rc.bottom - rc.top );

				// Sync m_ClientRectangle now, BEFORE createFXAA runs below.
				// updateClientArea at the end of setMode would also refresh
				// this, but it runs after createFXAA — so without this the
				// scene RT is sized from the stale rect, and next frame
				// renders into the upper-left corner of an oversized scene RT.
				POINT tl = { rc.left, rc.top };
				POINT br = { rc.right, rc.bottom };
				ClientToScreen( m_HWND, &tl );
				ClientToScreen( m_HWND, &br );
				m_ClientRectangle = RectInt( tl.x, tl.y, br.x - 1, br.y - 1 );
			}
		}
	}

	if ( targetW < 1 ) targetW = 1;
	if ( targetH < 1 ) targetH = 1;

	if ( m_pSwapChain )
	{
		HRESULT hr = m_pSwapChain->ResizeBuffers( FRAME_COUNT,
			targetW, targetH,
			DXGI_FORMAT_R8G8B8A8_UNORM, 0 );
		if ( FAILED(hr) )
		{
			HRESULT removed = m_pDevice ? m_pDevice->GetDeviceRemovedReason() : S_OK;
			TRACE( "setMode: ResizeBuffers failed hr=0x%08x, DeviceRemovedReason=0x%08x  %ux%u",
				hr, removed, targetW, targetH );
			return false;
		}
		m_nFrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();
	}

	createRenderTargets();
	createDepthStencil();

	// Scene RT is sized to the client; recreate alongside the backbuffers
	// or the next frame will sample a wrong-dimension scene RT.  (createFXAA
	// allocates the shared scene RT in addition to the FXAA-specific PSO.)
	if ( m_bSceneRTEnabled )
		createFXAA();

	updateClientArea( false );

	return true;
}

void DisplayDeviceD3D12::setFilterMode( FilterMode eMode )
{
	m_eFilterMode = eMode;
}

void DisplayDeviceD3D12::release()
{
	lock();

	// Drop cached PrimitiveSurfaces (textures) BEFORE we remove ourselves
	// from sm_DeviceList — that way the surfaces' release paths can still
	// see this device alive and defer-release their D3D12 resources through
	// the proper queue.  Without this flush, the global Material::sm_SurfaceHash
	// retains surfaces whose m_Texture was Detach()'d to NULL during the
	// previous device's teardown, and the next scene reuses those zombie
	// surfaces — manifesting as all-red rendering on scene re-open.
	Material::flushSurfaceCache();

	sm_DeviceList.removeSearch( this );
	abortScene();
	m_TextureFormats.release();
	m_pFormat = NULL;

	freeD3D12();
	unlock();
}

void DisplayDeviceD3D12::flushVideoMemory() const
{
	// No-op in DX12
}

//------------------------------------------------------------------------------------------------
// Rendering
//------------------------------------------------------------------------------------------------

void DisplayDeviceD3D12::clear( Color nColor )
{
	if ( !m_bCommandListOpen )
		return;

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RTVHeap.GetCPUHandle( m_nFrameIndex );
	float clearColor[4] = {
		nColor.m_R / 255.0f,
		nColor.m_G / 255.0f,
		nColor.m_B / 255.0f,
		nColor.m_A / 255.0f
	};
	m_pCommandList->ClearRenderTargetView( rtvHandle, clearColor, 0, nullptr );
}

void DisplayDeviceD3D12::clearZ( float fDepth )
{
	if ( !m_bCommandListOpen || !m_pDepthStencil )
		return;

	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_DSVHeap.GetCPUHandle( 0 );
	m_pCommandList->ClearDepthStencilView( dsvHandle, D3D12_CLEAR_FLAG_DEPTH, fDepth, 0, 0, nullptr );
}

void DisplayDeviceD3D12::setAmbient( Color nColor )
{
	m_cAmbientLight = nColor;
}

int DisplayDeviceD3D12::addDirectionalLight( int nPriority, Color nColor, const Vector3 & vDirection )
{
	if ( m_bShadowPass )
		return -1;

	LightInfo light = {};
	light.type = 3;		// directional

	light.dirX = vDirection.x;
	light.dirY = vDirection.y;
	light.dirZ = vDirection.z;

	const float inv = 1.0f / 255.0f;
	light.specR = light.r = nColor.r * inv;
	light.specG = light.g = nColor.g * inv;
	light.specB = light.b = nColor.b * inv;
	light.specA = light.a = 1.0f;

	m_Lights.insert( std::pair<int, LightInfo>( nPriority, light ) );
	return (int)m_Lights.size();
}

int DisplayDeviceD3D12::addPointLight( int nPriority, Color nColor, const Vector3 & vPosition, float fRadius )
{
	if ( m_bShadowPass )
		return -1;

	LightInfo light = {};
	light.type = 1;		// point

	const float inv = 1.0f / 255.0f;
	light.specR = light.r = nColor.r * inv;
	light.specG = light.g = nColor.g * inv;
	light.specB = light.b = nColor.b * inv;
	light.specA = light.a = 1.0f;

	light.posX = vPosition.x;
	light.posY = vPosition.y;
	light.posZ = vPosition.z;
	light.range = fRadius;

	light.att0 = 1.0f;
	light.att1 = fRadius > 0.0f ? 1.0f / fRadius : 0.0f;
	light.att2 = 0.0f;

	m_Lights.insert( std::pair<int, LightInfo>( nPriority, light ) );
	return (int)m_Lights.size();
}

void DisplayDeviceD3D12::clearLights()
{
	m_Lights.clear();
}

void DisplayDeviceD3D12::setFillMode( FillMode nMode )
{
	m_eFillMode = nMode;
}

void DisplayDeviceD3D12::setFog( FogMode nMode, float fBegin, float fEnd, Color nColor )
{
	// Fog is implemented in shaders in DX12
	// Store fog parameters for shader constant buffer
}

void DisplayDeviceD3D12::disableFog()
{
	// No-op, handled by shader constants
}

void DisplayDeviceD3D12::setProjection( const Matrix33 & mFrame, const Vector3 & vPosition,
	const RectInt & rWindow, float fFOV, float fFront, float fBack )
{
	m_Proj.m_mFrame = mFrame;
	m_Proj.m_vPosition = vPosition;
	m_Proj.m_rWindow = rWindow;
	m_Proj.m_fFOV = fFOV;
	m_Proj.m_fFront = fFront;
	m_Proj.m_fBack = fBack;

	updateProjection();
}

void DisplayDeviceD3D12::setShadowPass( int a_nMaxLights, const Vector3 & a_vFocus,
	float a_fShadowRadius, const SizeInt & a_szShadowMap )
{
	m_nMaxShadowLights = a_nMaxLights;
	m_vShadowFocus = a_vFocus;
	m_fShadowRadius = a_fShadowRadius;

	if ( m_szShadowMap != a_szShadowMap )
	{
		m_szShadowMap = a_szShadowMap;
		m_ShadowPassList.clear();
		m_bShadowMapReady = false;
		m_pShadowMapDepth.Reset();
	}
}

//---------------------------------------------------------------------------------------------------

bool DisplayDeviceD3D12::beginScene()
{
	PROFILE_START( "DisplayDeviceD3D12::beginScene" );

	// Don't open the command list when minimized — present() will still advance the frame
	if ( m_bMinimized )
	{
		PROFILE_END();
		return false;
	}

	m_pCurrentTransform = NULL;
	m_pCurrentMaterial = NULL;

	// Reset shadow state for each scene — prevents stale shadow data
	// when views that don't use shadows (e.g. engineering view) render
	m_bFirstShadowPass = true;
	m_nShadowMapPass = 0;
	m_ShadowPassList.clear();

	// Frame starts pre-FXAA: material draws target the scene RT (HDR float).
	// applyFXAA() flips this true after binding the swap chain so OVERLAY/UI draws
	// pick up the R8G8B8A8 PSO variant.
	m_bRenderingPostAA = false;

	// Clear the per-frame exposure pointer so a disabled/removed exposure
	// effect doesn't leak a stale SRV into applyFXAA.  If the effect runs,
	// its postRender sets this to the fresh ping-pong SRV.
	m_nCurrentExposureSRVIndex = UINT(-1);

	if ( !m_bCommandListOpen )
	{
		PROFILE_START( "beginScene:resetCommandList" );
		resetCommandList();
		PROFILE_END();

		// Transition the render target from PRESENT to RENDER_TARGET
		{
			PROFILE_START( "beginScene:transitionRT_to_RT" );
			TransitionResource( m_pCommandList.Get(), m_pRenderTargets[m_nFrameIndex].Get(),
				D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET );
			PROFILE_END();
		}

		// FLIP_DISCARD leaves back buffer contents undefined after Present.
		// Always clear both the render target and depth buffer at frame start
		// to prevent ghosting from previous frames.
		{
			PROFILE_START( "beginScene:clearRTandDepth" );
			D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RTVHeap.GetCPUHandle( m_nFrameIndex );
			float black[4] = { 0, 0, 0, 1 };
			m_pCommandList->ClearRenderTargetView( rtv, black, 0, nullptr );

			// Also clear the offscreen scene RT if the post-process pipeline is enabled
			if ( m_bSceneRTEnabled && m_pSceneRT )
			{
				if ( !m_bSceneRTisRT )
				{
					TransitionResource( m_pCommandList.Get(), m_pSceneRT.Get(),
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );
					m_bSceneRTisRT = true;
				}
				D3D12_CPU_DESCRIPTOR_HANDLE sceneRTV = m_RTVHeap.GetCPUHandle( m_nSceneRTVIndex );
				m_pCommandList->ClearRenderTargetView( sceneRTV, black, 0, nullptr );
			}

			if ( m_pDepthStencil )
			{
				// Post-FX may have left depth in PSR — transition back before clear,
				// which requires DEPTH_WRITE state.
				ensureDepthStencilState( m_pCommandList.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE );
				D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_DSVHeap.GetCPUHandle( 0 );
				m_pCommandList->ClearDepthStencilView( dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr );
			}
			PROFILE_END();
		}

		// Reset per-frame dynamic buffers only once per frame
		m_DynamicVB[m_nFrameIndex].Reset();
		m_DynamicCB[m_nFrameIndex].Reset();

		// Release primitives deferred from the previous use of this frame slot.
		// moveToNextFrame() already waited on the fence, so the GPU is done.
		// Lock guards against concurrent appends from worker threads (see
		// PrimitiveMaterialD3D12::clear()).
		{
			AutoLock lock( &m_DeferredPrimsLock );
			m_DeferredPrimitives[m_nFrameIndex].release();
			// Raw D3D12 resources deferred from primitives torn down on
			// non-main threads.  Each entry holds one AddRef — Release here
			// drops it.  After moveToNextFrame's fence wait, the GPU is
			// guaranteed past any draw that referenced these.
			Array< ID3D12Resource * > & res = m_DeferredResources[m_nFrameIndex];
			for ( int i = 0; i < res.size(); ++i )
				if ( res[i] ) res[i]->Release();
			res.release();
		}

		// Reset per-frame SRV ring allocator.  Per-frame slab means frame N
		// uses slots [N*MAX, (N+1)*MAX); the first 8 slots of each slab hold
		// the permanent null SRVs (created at device init, never overwritten
		// because allocations always start at slab-base + 8).
		const UINT slabBase = (UINT)m_nFrameIndex * MAX_SRV_DESCRIPTORS;
		m_nSRVFrameOffset.store( slabBase + 8, std::memory_order_release );
		m_nSRVTextureBase = slabBase + 8;

		// Reset redundant-bind tracking for the new frame.  The command list is
		// fresh, so any previously-tracked bindings are invalid.
		m_bLastBoundSRVValid = false;
		m_bLastMatCBValid = false;
		m_bLastLightCBValid = false;
		m_bLastObjCBValid = false;
		m_nLastMatCBGpuVA = 0;
		m_nLastLightCBGpuVA = 0;
		m_nLastObjCBGpuVA = 0;

		// Reset per-frame counters used by the ALT+P profiler overlay
		m_nSRVBindCalls = 0;
		m_nSRVBindSkipped = 0;
		m_nMatCBUploads = 0;
		m_nMatCBSkipped = 0;
		m_nLightCBUploads = 0;
		m_nLightCBSkipped = 0;
		m_nObjCBUploads = 0;
		m_nObjCBSkipped = 0;
		m_nSRVCopies = 0;
		m_nSRVCopiesSkipped = 0;

		// Per-slot cache of which staging index currently lives in each SRV slot.
		// Sized to cover the full heap (all per-frame slabs) so indexing by
		// absolute slot works.  Blanket-invalidate each frame by bumping
		// m_SRVSlotEpoch — see encoding comment on the member declaration.
		// On resize we zero the array so the (epoch, idx) pair never spuriously
		// matches uninitialised garbage.
		const int fullHeapSize = (int)MAX_SRV_DESCRIPTORS * FRAME_COUNT;
		if ( m_SRVSlotStagingIndex.size() != fullHeapSize )
		{
			m_SRVSlotStagingIndex.allocate( fullHeapSize );
			for ( int i = 0; i < m_SRVSlotStagingIndex.size(); ++i )
				m_SRVSlotStagingIndex[i] = 0;
		}
		// Wrap-safe bump: when epoch hits UINT_MAX, reset the array and start
		// over at 1 so old entries can't ghost-match a freshly-wrapped epoch.
		if ( ++m_SRVSlotEpoch == 0 )
		{
			for ( int i = 0; i < m_SRVSlotStagingIndex.size(); ++i )
				m_SRVSlotStagingIndex[i] = 0;
			m_SRVSlotEpoch = 1;
		}

		// Build screen-space ortho matrix for TL (pre-transformed) vertices.
		// mProj is perspective for 3D; TL shaders use mProjOrtho instead.
		// Use renderWindow() (0-based) because TL vertices are in client-local
		// coordinates, not screen coordinates.
		{
			RectInt rw = renderWindow();
			XMMATRIX ortho = XMMatrixOrthographicOffCenterLH(
				(float)rw.left,
				(float)(rw.right + 1),
				(float)(rw.bottom + 1),
				(float)rw.top,
				0.0f, 1.0f );
			m_CBPerFrame.mProjOrtho = ShaderMatrix( ortho );
		}

		// Set root signature, descriptor heaps, and default CBV/SRV/Sampler bindings
		bindMainRootDefaults();
	}
	// else: sub-render call — command list is already open, reuse it

	// Set render targets (must be set for each sub-render as shadow passes change them).
	// When the offscreen scene RT pipeline is enabled, render to the intermediate scene RT
	// instead of the swap chain — the AA pass at present() resolves it to the backbuffer.
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	if ( m_bSceneRTEnabled && m_pSceneRT )
	{
		rtvHandle = m_RTVHeap.GetCPUHandle( m_nSceneRTVIndex );
	}
	else
	{
		rtvHandle = m_RTVHeap.GetCPUHandle( m_nFrameIndex );
	}
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_DSVHeap.GetCPUHandle( 0 );
	m_pCommandList->OMSetRenderTargets( 1, &rtvHandle, FALSE, &dsvHandle );

	// Set viewport and scissor rect
	D3D12_VIEWPORT viewport = {};
	viewport.TopLeftX = (float)m_Proj.m_rWindow.left;
	viewport.TopLeftY = (float)m_Proj.m_rWindow.top;
	viewport.Width = (float)m_Proj.m_rWindow.width();
	viewport.Height = (float)m_Proj.m_rWindow.height();
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	m_pCommandList->RSSetViewports( 1, &viewport );

	D3D12_RECT scissor = { m_Proj.m_rWindow.left, m_Proj.m_rWindow.top,
		m_Proj.m_rWindow.right, m_Proj.m_rWindow.bottom };
	m_pCommandList->RSSetScissorRects( 1, &scissor );

	m_bBeginScene = true;
	PROFILE_END();	// "DisplayDeviceD3D12::beginScene"
	return true;
}

bool DisplayDeviceD3D12::beginShadowPass( Transform & a_LightTransform )
{
	if ( !m_bShadowMapSupported )
		return false;
	if ( m_bShadowPass )
		return false;

	if ( m_bFirstShadowPass )
	{
		m_bFirstShadowPass = false;
		// Limit to 1 shadow light — atlas holds cascades for one light
		int nLights = m_nMaxShadowLights < (int)m_Lights.size() ? m_nMaxShadowLights : (int)m_Lights.size();
		if ( nLights > 1 )
			nLights = 1;
		int nPasses = nLights * NUM_SHADOW_CASCADES;
		m_ShadowPassList.resize( nPasses );
		m_iCurrentShadowPass = m_ShadowPassList.begin();
		m_iCurrentShadowLight = m_Lights.begin();
	}

	if ( m_iCurrentShadowLight == m_Lights.end() )
		return false;
	if ( m_iCurrentShadowPass == m_ShadowPassList.end() )
		return false;

	if ( !readyShadowMap() )
		return false;

	LightInfo & light = m_iCurrentShadowLight->second;

	Vector3 vLightViewPosition( Vector3::ZERO );
	Vector3 vLightViewDirection( Vector3::ZERO );

	float fMidZ = (m_Proj.m_fBack - m_Proj.m_fFront) * 0.5f;
	Vector3 vWorldFocus( m_Proj.m_vPosition + (m_Proj.m_mFrame % m_vShadowFocus) );
	float fShadowScale = 0.0f;
	bool bOrthoProj = false;

	if ( light.type == 3 )	// directional
	{
		vLightViewDirection = Vector3( light.dirX, light.dirY, light.dirZ );
		vLightViewPosition = vWorldFocus - (vLightViewDirection * fMidZ);
		fShadowScale = light.r * light.g * light.b;
		bOrthoProj = true;
	}
	else if ( light.type == 1 )	// point
	{
		vLightViewPosition = Vector3( light.posX, light.posY, light.posZ );
		vLightViewDirection = vWorldFocus - vLightViewPosition;
		float fDistance = vLightViewDirection.magnitude();
		vLightViewDirection *= 1.0f / fDistance;

		if ( fDistance < light.range )
		{
			if ( fDistance > 0.0f )
				fShadowScale = 1.0f - (fDistance / light.range);
			else
				fShadowScale = 1.0f;
		}
	}

	if ( fShadowScale <= 0.05f )
		return false;

	// Clip light to shadow focus area
	SphericalHull sphere( m_vShadowFocus, m_fShadowRadius );
	Vector3 vClippedLightPos( m_Proj.m_mFrame * (vLightViewPosition - m_Proj.m_vPosition) );
	sphere.intersect( vClippedLightPos, m_vShadowFocus - vClippedLightPos, vClippedLightPos );
	vLightViewPosition = m_Proj.m_vPosition + (m_Proj.m_mFrame % vClippedLightPos);

	int cascadeIndex = m_nShadowMapPass % NUM_SHADOW_CASCADES;

	ShadowPass & pass = *m_iCurrentShadowPass;
	pass.m_LightTransform.m_mFrame = Matrix33( vLightViewDirection );
	pass.m_LightTransform.m_vTranslate = vLightViewPosition;
	pass.m_bOrthoProj = bOrthoProj;
	pass.m_nCascadeIndex = cascadeIndex;
	pass.m_Primitives.release();

	a_LightTransform = pass.m_LightTransform;
	m_bShadowPass = true;
	m_pCurrentTransform = NULL;
	m_pCurrentMaterial = NULL;

	return true;
}

bool DisplayDeviceD3D12::endShadowPass()
{
	if ( !m_bShadowPass )
		return false;

	m_bShadowPass = false;

	ShadowPass & pass = *m_iCurrentShadowPass;
	int cascadeIndex = pass.m_nCascadeIndex;

	// Save current projection
	Projection savedProjection( m_Proj );

	// Set the projection for the light
	int quadSize = m_szShadowMap.width / 2;
	m_Proj.m_rWindow = RectInt( PointInt(0, 0), SizeInt(quadSize, quadSize) );
	m_Proj.m_mFrame = pass.m_LightTransform.m_mFrame;
	m_Proj.m_vPosition = pass.m_LightTransform.m_vTranslate;

	// Compute distance from light to shadow focus
	Vector3 vWorldFocus( savedProjection.m_vPosition + (savedProjection.m_mFrame % m_vShadowFocus) );
	float fLightToFocus = (vWorldFocus - pass.m_LightTransform.m_vTranslate).magnitude();

	// Build view matrix from light's position and frame
	m_Proj.m_fFront = 1.0f;
	m_Proj.m_fBack = fLightToFocus + m_fShadowRadius * 4.0f;
	updateProjection();

	// Cascade-specific ortho projection — each cascade covers a different radius
	float fCascadeRadius = m_fShadowRadius * CASCADE_SPLIT_RATIOS[cascadeIndex];
	float fOrthoSize = fCascadeRadius * 2.0f;
	float fShadowNear = Max( 1.0f, fLightToFocus - m_fShadowRadius * 4.0f );
	float fShadowFar = fLightToFocus + m_fShadowRadius * 4.0f;
	m_mProj = XMMatrixOrthographicLH( fOrthoSize, fOrthoSize, fShadowNear, fShadowFar );

	XMStoreFloat4x4( &pass.m_LightView, m_mView );
	XMStoreFloat4x4( &pass.m_LightProj, m_mProj );
	pass.m_fShadowDepthRange = fShadowFar - fShadowNear;
	m_fShadowDepthRange = pass.m_fShadowDepthRange;

	// Render shadow map cascade to atlas quadrant
	if ( m_pShadowMapDepth && m_bCommandListOpen )
	{
		ShaderD3D12::Ref savedMatShader = m_pMatShader;
		m_pMatShader = m_pShadowMapShader;
		UINT savedBlend = m_nCurrentBlend;
		bool savedDoubleSided = m_bCurrentDoubleSided;
		m_nCurrentBlend = 0;
		m_bCurrentDoubleSided = false;
		m_bRenderingShadowMap = true;

		D3D12_CPU_DESCRIPTOR_HANDLE smRTV = m_RTVHeap.GetCPUHandle( m_nShadowMapDSVIndex );

		// Transition to RT and clear on first cascade only
		if ( cascadeIndex == 0 )
		{
			TransitionResource( m_pCommandList.Get(), m_pShadowMapDepth.Get(),
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );
			m_bShadowMapInRTState = true;

			float clearColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
			m_pCommandList->ClearRenderTargetView( smRTV, clearColor, 0, nullptr );
		}

		m_pCommandList->OMSetRenderTargets( 1, &smRTV, FALSE, nullptr );

		// Set viewport to this cascade's atlas quadrant
		int qx = cascadeIndex % 2;
		int qy = cascadeIndex / 2;
		D3D12_VIEWPORT vp = {};
		vp.TopLeftX = (float)(qx * quadSize);
		vp.TopLeftY = (float)(qy * quadSize);
		vp.Width = (float)quadSize;
		vp.Height = (float)quadSize;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;
		D3D12_RECT scissor = { (LONG)(qx * quadSize), (LONG)(qy * quadSize),
			(LONG)((qx + 1) * quadSize), (LONG)((qy + 1) * quadSize) };

		bindPerFrameCB();
		m_pCommandList->RSSetViewports( 1, &vp );
		m_pCommandList->RSSetScissorRects( 1, &scissor );

		{
			// Per-cascade label so ALT+P shows which cascade is the runaway.
			// Cascades cover progressively wider areas (CASCADE_SPLIT_RATIOS
			// = { 0.08, 0.24, 0.60, 1.0 }) so cascade 3 typically catches the
			// most geometry.  Lookup-table because the profiler interns names
			// by content — we need stable string-literal pointers, not
			// sprintf'd buffers.
			static const char * const kShadowCascadeNames[ NUM_SHADOW_CASCADES ] = {
				"shadowPass:cascade_0",
				"shadowPass:cascade_1",
				"shadowPass:cascade_2",
				"shadowPass:cascade_3",
			};
			const int idx = ( cascadeIndex >= 0 && cascadeIndex < NUM_SHADOW_CASCADES ) ? cascadeIndex : 0;
			PROFILE_START( kShadowCascadeNames[ idx ] );
			for ( int i = 0; i < pass.m_Primitives.size(); ++i )
				pass.m_Primitives[i]->execute();
			PROFILE_END();
		}

		// Transition back to SRV on last cascade
		if ( cascadeIndex == NUM_SHADOW_CASCADES - 1 )
		{
			TransitionResource( m_pCommandList.Get(), m_pShadowMapDepth.Get(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
			m_bShadowMapInRTState = false;
		}

		// Restore state
		m_bRenderingShadowMap = false;
		m_pMatShader = savedMatShader;
		m_nCurrentBlend = savedBlend;
		m_bCurrentDoubleSided = savedDoubleSided;
	}
	pass.m_Primitives.release();

	// Restore projection
	m_Proj = savedProjection;
	updateProjection();

	// Restore main render target and viewport
	if ( m_bCommandListOpen )
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
		if ( m_bSceneRTEnabled && m_pSceneRT )
			rtvHandle = m_RTVHeap.GetCPUHandle( m_nSceneRTVIndex );
		else
			rtvHandle = m_RTVHeap.GetCPUHandle( m_nFrameIndex );
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_DSVHeap.GetCPUHandle( 0 );
		m_pCommandList->OMSetRenderTargets( 1, &rtvHandle, FALSE, &dsvHandle );

		D3D12_VIEWPORT viewport = {};
		viewport.TopLeftX = (float)m_Proj.m_rWindow.left;
		viewport.TopLeftY = (float)m_Proj.m_rWindow.top;
		viewport.Width = (float)m_Proj.m_rWindow.width();
		viewport.Height = (float)m_Proj.m_rWindow.height();
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		m_pCommandList->RSSetViewports( 1, &viewport );

		D3D12_RECT scissor = { m_Proj.m_rWindow.left, m_Proj.m_rWindow.top,
			m_Proj.m_rWindow.right, m_Proj.m_rWindow.bottom };
		m_pCommandList->RSSetScissorRects( 1, &scissor );

		// Re-bind per-frame CB with the restored camera matrices
		bindPerFrameCB();
	}

	++m_iCurrentShadowPass;
	++m_nShadowMapPass;
	// Advance to next light after all cascades for the current light are done
	if ( m_nShadowMapPass % NUM_SHADOW_CASCADES == 0 )
		++m_iCurrentShadowLight;

	return true;
}

bool DisplayDeviceD3D12::endScene()
{
	if ( m_bMinimized )
	{
		abortScene();
		return false;
	}

	if ( !m_bCommandListOpen )
	{
		abortScene();
		return false;
	}

	// Safety: ensure shadow map is back in PSR state before scene rendering
	if ( m_bShadowMapInRTState && m_pShadowMapDepth )
	{
		TransitionResource( m_pCommandList.Get(), m_pShadowMapDepth.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		m_bShadowMapInRTState = false;
	}

	// Bind per-frame constant buffer with final lighting data
	bindPerFrameCB();

	// Pre-render effects (e.g. redirect rendering to an offscreen target)
	{
		PROFILE_START( "endScene:effect_preRender" );
		for ( EffectList::iterator iEffect = m_EffectList.begin();
			iEffect != m_EffectList.end(); )
		{
			DisplayEffect * pEffect = *iEffect;
			if ( !pEffect->preRender( this ) )
			{
				TRACE( "ERROR: Effect preRender() failed." );
				m_EffectList.erase( iEffect++ );
			}
			else
				++iEffect;
		}
		PROFILE_END();
	}

	// Execute material stacks for 3D passes only — OVERLAY is deferred until
	// after post-processing so UI text stays crisp (FXAA would blur it).
	// One profile entry per pass so the ALT+P tree shows where the GPU work
	// actually goes (background clear + skybox vs solid geometry vs particles).
	static const char * const kPassNames[ OVERLAY ] = {
		"endScene:exec_BACKGROUND",		// stars, skybox, planet shells
		"endScene:exec_PRIMARY",		// opaque ships, stations, planets
		"endScene:exec_SECONDARY",		// translucent particles, beams, trails
	};
	for ( int i = 0; i < OVERLAY; ++i )
	{
		Array< PrimitiveMaterial::Ref > & materials = m_Stack[i];
		if ( materials.size() == 0 )
		{
			materials.release();
			continue;
		}

		// SECONDARY pass benefits from grouping same-shader materials so the
		// PSO state thrash drops between adjacent draws.  Stable insertion
		// sort by shader() pointer — names are stable string literals so
		// pointer comparison groups equal-shader materials.
		if ( i == SECONDARY && materials.size() > 1 )
		{
			const int n = materials.size();
			for ( int a = 1; a < n; ++a )
			{
				PrimitiveMaterial::Ref vRef = materials[ a ];
				const char * vKey = vRef->shader();
				int b = a;
				while ( b > 0 && (uintptr_t)materials[ b - 1 ]->shader() > (uintptr_t)vKey )
				{
					materials[ b ] = materials[ b - 1 ];
					--b;
				}
				materials[ b ] = vRef;
			}
		}

		PROFILE_START( kPassNames[ i ] );
		for ( int j = 0; j < materials.size(); ++j )
		{
			PrimitiveMaterial * pMaterial = materials[j];
			pMaterial->execute();
			pMaterial->clear();
		}
		PROFILE_END();
		materials.release();
	}

	// Post-render effects (e.g. bloom, blur — processed in reverse order)
	{
		PROFILE_START( "endScene:effect_postRender" );
		for ( EffectList::reverse_iterator iEffect = m_EffectList.rbegin();
			iEffect != m_EffectList.rend(); ++iEffect )
		{
			DisplayEffect * pEffect = *iEffect;
			if ( !pEffect->postRender( this ) )
				TRACE( "ERROR: Effect postRender() returned false!" );
		}
		m_EffectList.clear();
		PROFILE_END();
	}

	m_bBeginScene = false;
	return true;
}

void DisplayDeviceD3D12::abortScene()
{
	// Safety: ensure shadow map is back in PSR state
	if ( m_bShadowMapInRTState && m_pShadowMapDepth && m_bCommandListOpen )
	{
		TransitionResource( m_pCommandList.Get(), m_pShadowMapDepth.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		m_bShadowMapInRTState = false;
	}

	for ( int i = 0; i < PASS_COUNT; i++ )
	{
		Array< PrimitiveMaterial::Ref > & materials = m_Stack[i];
		for ( int j = 0; j < materials.size(); j++ )
			materials[j]->clear();
		materials.release();
	}

	m_bBeginScene = false;
}

void DisplayDeviceD3D12::present()
{
	PROFILE_START( "DisplayDeviceD3D12::present" );

	if ( m_bCommandListOpen )
	{
		// Apply the AA pass: resolve scene RT → swap chain back buffer.
		// Each apply* function binds the back buffer as the current RTV, leaves
		// the scene RT in the PIXEL_SHADER_RESOURCE state, and sets
		// m_bRenderingPostAA=true so subsequent OVERLAY draws know to rebind
		// the main root signature.  AA_SMAA is wired in a later phase.
		if ( m_bSceneRTEnabled && m_pSceneRT )
		{
			switch ( m_eAAMode )
			{
			case AA_NONE:
				PROFILE_START( "present:applyTonemap" );
				applyTonemap();
				PROFILE_END();
				break;
			case AA_FXAA:
				PROFILE_START( "present:applyFXAA" );
				applyFXAA();
				PROFILE_END();
				break;
			case AA_SMAA:
				if ( m_bSMAAAvailable )
				{
					PROFILE_START( "present:applySMAA" );
					applySMAA();
					PROFILE_END();
				}
				else
				{
					// SMAA failed to init (shader compile, RT alloc, PSO).
					// Fall back to FXAA so the frame still resolves to the
					// backbuffer instead of producing a black screen.
					PROFILE_START( "present:applyFXAA(smaa-fallback)" );
					applyFXAA();
					PROFILE_END();
				}
				break;
			}
		}

		// Render the OVERLAY pass on top of the post-processed image, so UI stays crisp.
		// When an AA pass ran, it bound the back buffer as the RTV and swapped to
		// its own narrow root signature; when no AA pass ran (m_bRenderingPostAA
		// stays false), the back buffer is still bound from beginScene and the
		// main root sig is still in effect.
		//
		// applyFXAA() (and later applySMAA / applyTonemap) swaps to m_pFXAARootSig,
		// which only has 3 parameters — materials assume the main root signature
		// (6 parameters) and would crash on SetGraphicsRootDescriptorTable(4, ...)
		// otherwise.  Only rebind when an AA pass actually ran.
		{
			PROFILE_START( "present:OVERLAY_execute" );
			Array< PrimitiveMaterial::Ref > & materials = m_Stack[ OVERLAY ];
			const int nOverlayCount = materials.size();	// captured before release() for the profiler message below

			if ( nOverlayCount > 0 && m_bRenderingPostAA )
				bindMainRootDefaults();

			{
				PROFILE_START( "present:OVERLAY_exec_loop" );
				for ( int j = 0; j < materials.size(); ++j )
				{
					PrimitiveMaterial * pMaterial = materials[j];
					pMaterial->execute();
					pMaterial->clear();
				}
				PROFILE_END();	// "present:OVERLAY_exec_loop"
			}
			materials.release();
#ifndef PROFILE_OFF
			// Surface the per-frame overlay material count in the ALT+P overlay
			// so we can see whether 6% goes into few-big-materials or many-small-ones.
			PROFILE_LMESSAGE( 12, CharString().format(
				"OVERLAY materials/frame: %d", nOverlayCount ) );
#endif
			PROFILE_END();	// close "present:OVERLAY_execute"
		}

		// Transition render target to present state and close the command list
		{
			PROFILE_START( "present:transitionRT_to_present" );
			TransitionResource( m_pCommandList.Get(), m_pRenderTargets[m_nFrameIndex].Get(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT );
			PROFILE_END();
		}
		PROFILE_START( "present:flushCommandList" );
		flushCommandList();
		PROFILE_END();
	}

	// Present the frame
	if ( m_pSwapChain )
	{
		PROFILE_START( "present:SwapChain::Present" );
		UINT syncInterval = sm_bWaitVB ? 1 : 0;
		m_pSwapChain->Present( syncInterval, 0 );
		PROFILE_END();
	}

	// moveToNextFrame() rotates the per-frame allocator + signals/waits on the
	// fence.  This is where GPU back-pressure shows up if the GPU can't keep
	// up with the CPU — the wait blocks until the next frame's allocator is
	// safe to reuse.
	{
		PROFILE_START( "present:moveToNextFrame" );
		moveToNextFrame();
		PROFILE_END();
	}
	{
		PROFILE_START( "present:updateClientArea" );
		updateClientArea( true );
		PROFILE_END();
	}

	PROFILE_START( "present:drainInfoQueue" );
	drainInfoQueue();
	PROFILE_END();

	// Per-frame counters now surfaced via getRenderStats() — rendered as a
	// tabular block in the profiler overlay, no longer needs PROFILE_LMESSAGE.

	PROFILE_END();
}

//---------------------------------------------------------------------------------------------------

static void pushStat( Array<DisplayDevice::RenderStat> & out,
		const char * pName, dword nValue, const char * pValueLabel, dword nSkipped )
{
	DisplayDevice::RenderStat & s = out.push();
	s.pName        = pName;
	s.nValue       = nValue;
	s.pValueLabel  = pValueLabel;
	s.nSkipped     = nSkipped;
	const dword nTotal = nValue + nSkipped;
	s.fSkipPct     = ( nTotal > 0 ) ? (100.0f * (float)nSkipped / (float)nTotal) : 0.0f;
}

void DisplayDeviceD3D12::getRenderStats( Array<RenderStat> & a_Out ) const
{
	pushStat( a_Out, "SRV binds",  m_nSRVBindCalls,  "issued",   m_nSRVBindSkipped );
	pushStat( a_Out, "MatCB",      m_nMatCBUploads,  "uploaded", m_nMatCBSkipped );
	pushStat( a_Out, "LightCB",    m_nLightCBUploads,"uploaded", m_nLightCBSkipped );
	pushStat( a_Out, "SRV copies", m_nSRVCopies,     "issued",   m_nSRVCopiesSkipped );
	pushStat( a_Out, "ObjCB",      m_nObjCBUploads,  "uploaded", m_nObjCBSkipped );
}

//---------------------------------------------------------------------------------------------------

DevicePrimitive * DisplayDeviceD3D12::create( const PrimitiveKey & key )
{
	// BasePrimitiveFactory12::findFactory + pFactory->create() touch a shared
	// factory pool (List of free primitives).  Two worker threads racing here
	// corrupt the list → heap validation failure.  Serialize with the render
	// lock used by Material and push().  Main thread no-ops when parallel is
	// off since the lock is uncontended.
	AutoLock lock( &RenderContext::sm_StateLock );

	BasePrimitiveFactory12 * pFactory = BasePrimitiveFactory12::findFactory( key );
	if ( pFactory == NULL )
		throw PrimitiveFailure();
	DevicePrimitive * pPrimitive = pFactory->create();
	if ( pPrimitive == NULL )
		throw PrimitiveFailure();

	pPrimitive->setDevice( this );
	return pPrimitive;
}

bool DisplayDeviceD3D12::push( DevicePrimitive * pPrimitive )
{
	if ( pPrimitive == NULL )
		return false;

	pPrimitive->setDevice( this );

	// Route writes to per-worker scratch slots when running on a ThreadPool
	// worker (inside parallel preRender).  Main thread keeps writing to the
	// shared m_Stack / m_pCurrentMaterial / m_pCurrentTransform.  The main
	// thread is blocked in parallelFor() during worker activity, so there
	// is no contention between main and worker on the shared fields.
	const int workerIdx = ThreadPool::currentWorkerIndex();
	PrimitiveMaterial::Ref *		ppCurrentMaterial	= &m_pCurrentMaterial;
	PrimitiveSetTransform::Ref *	ppCurrentTransform	= &m_pCurrentTransform;
	Array< PrimitiveMaterial::Ref > * pStackArray		= m_Stack;
	if ( workerIdx >= 0 && workerIdx < m_WorkerStates.size() )
	{
		WorkerRenderState & ws = m_WorkerStates[ workerIdx ];
		ppCurrentMaterial	= &ws.m_pCurrentMaterial;
		ppCurrentTransform	= &ws.m_pCurrentTransform;
		pStackArray			= ws.m_Stack;
	}

	// The PrimitiveMaterial object is a process-wide cache: m_bPushed and
	// its children-list are shared by every thread that touches it.  When
	// we're on a worker, serialize the whole push() body through the same
	// lock Material::material() uses so the two stay consistent.  Main
	// thread never contends (it's blocked in parallelFor during worker
	// activity) and gets a nullptr AutoLock = zero-cost no-op.
	AutoLock sharedLock( workerIdx >= 0 ? (AbstractLock *)&RenderContext::sm_StateLock : (AbstractLock *)NULL );

	if ( pPrimitive->primitiveKey() == PrimitiveMaterial::staticPrimitiveKey() )
	{
		*ppCurrentMaterial = (PrimitiveMaterial *)pPrimitive;

		if ( !m_bShadowPass )
		{
			PrimitiveMaterialD3D12 * pMaterial = (PrimitiveMaterialD3D12 *)pPrimitive;

			// Track the minimum child index that has tried to push this
			// material during the current frame.  Serial pushes (main thread,
			// outside parallel zone dispatch) report -1 and are ignored — they
			// go to m_Stack directly and don't need positioning.  Worker-path
			// updates happen under sm_StateLock (see AutoLock above), so this
			// is a plain non-atomic min.
			const int childIdx = RenderContext::currentPreRenderChildIndex();
			if ( childIdx >= 0 && childIdx < pMaterial->m_nFirstClaimChild )
				pMaterial->m_nFirstClaimChild = childIdx;

			if ( !pMaterial->m_bPushed )
			{
				pStackArray[ pMaterial->m_nPass ].push( pMaterial );
				pMaterial->m_bPushed = true;
			}

			if ( (*ppCurrentTransform).valid() )
				pMaterial->addChild( *ppCurrentTransform );
		}
		else
		{
			// Shadow passes are not parallelised — keep using main-thread state.
			ShadowPass & pass = *m_iCurrentShadowPass;
			if ( m_pCurrentTransform.valid() && (pass.m_Primitives.size() == 0 || pass.m_Primitives.last() != m_pCurrentTransform) )
				pass.m_Primitives.push( (DevicePrimitive *)m_pCurrentTransform );
		}
	}
	else
	{
		if ( pPrimitive->primitiveKey() == PrimitiveSetTransform::staticPrimitiveKey() )
			*ppCurrentTransform = (PrimitiveSetTransform *)pPrimitive;

		if ( !m_bShadowPass )
		{
			if ( (*ppCurrentMaterial).valid() )
				(*ppCurrentMaterial)->addChild( pPrimitive );
		}
		else
		{
			// Shadow passes stay main-thread.
			ShadowPass & pass = *m_iCurrentShadowPass;
			if ( !m_pCurrentMaterial.valid() || m_pCurrentMaterial->pass() == PRIMARY )
				pass.m_Primitives.push( pPrimitive );
		}
	}

	return true;
}

void DisplayDeviceD3D12::ensureParallelWorkerSlots( int nWorkers )
{
	if ( m_WorkerStates.size() < nWorkers )
		m_WorkerStates.allocate( nWorkers );
}

bool DisplayDeviceD3D12::ensureWorkerD3D12Slots( int nWorkers )
{
	// Idempotent: already sized large enough → done.  Existing contexts
	// keep their allocators/CLs intact.
	if ( m_WorkerD3D12Contexts.size() >= nWorkers )
		return true;

	if ( !m_pDevice )
		return false;

	const int nOldSize = m_WorkerD3D12Contexts.size();
	m_WorkerD3D12Contexts.allocate( nWorkers );

	// Create allocators + CL for each newly-added worker slot.  Mirrors the
	// pattern in initializeD3D12 for the main allocators / m_pCommandList.
	for ( int w = nOldSize; w < nWorkers; ++w )
	{
		WorkerD3D12Context & ctx = m_WorkerD3D12Contexts[w];
		for ( UINT f = 0; f < FRAME_COUNT; ++f )
		{
			HRESULT hr = m_pDevice->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS( &ctx.m_pAllocator[f] ) );
			if ( FAILED( hr ) )
			{
				LogIfFailed( hr, "ensureWorkerD3D12Slots: CreateCommandAllocator" );
				return false;
			}
		}

		// Create the CL closed (matches main path).  Stage 3 will Reset()
		// it against the current frame's allocator at start-of-pass.
		HRESULT hr = m_pDevice->CreateCommandList(
			0, D3D12_COMMAND_LIST_TYPE_DIRECT,
			ctx.m_pAllocator[0].Get(), nullptr,
			IID_PPV_ARGS( &ctx.m_pCommandList ) );
		if ( FAILED( hr ) )
		{
			LogIfFailed( hr, "ensureWorkerD3D12Slots: CreateCommandList" );
			return false;
		}
		ctx.m_pCommandList->Close();
		ctx.m_bCommandListOpen = false;
	}

	return true;
}

void DisplayDeviceD3D12::releaseWorkerD3D12Resources()
{
	for ( int w = 0; w < m_WorkerD3D12Contexts.size(); ++w )
	{
		WorkerD3D12Context & ctx = m_WorkerD3D12Contexts[w];
		ctx.m_pCommandList.Reset();
		for ( UINT f = 0; f < FRAME_COUNT; ++f )
			ctx.m_pAllocator[f].Reset();
		ctx.m_pMatShader = NULL;
	}
	m_WorkerD3D12Contexts.release();
}

void DisplayDeviceD3D12::mergeWorkerStacks()
{
	// Concatenate each worker's per-pass primitive stack back onto the shared
	// m_Stack in traversal order — i.e. ordered by the lowest child index that
	// tried to push each material (PrimitiveMaterialD3D12::m_nFirstClaimChild,
	// tracked in push() under sm_StateLock).  Worker-grab order is non-
	// deterministic (ThreadPool uses atomic fetch-and-increment), so a naive
	// worker-index-order concat would cause SECONDARY-pass (transparency)
	// materials to appear in a frame-dependent order and flicker between
	// frames.  Stable-sorting by first-claim child index gives the same order
	// serial rendering produces, deterministic across frames.
	//
	// Called on the main thread after parallelFor returns, before endScene()
	// iterates m_Stack[pass].  Clears each worker's slot on the way out so
	// the next dispatch starts empty.
	for ( int pass = 0; pass < PASS_COUNT; ++pass )
	{
		struct Entry
		{
			PrimitiveMaterial::Ref	pMat;
			int						nChildIdx;
			int						nStableIdx;	// tie-break: preserve push order
		};
		std::vector< Entry > entries;
		int nStableCounter = 0;
		for ( int w = 0; w < m_WorkerStates.size(); ++w )
		{
			Array< PrimitiveMaterial::Ref > & src = m_WorkerStates[w].m_Stack[pass];
			for ( int i = 0; i < src.size(); ++i )
			{
				Entry e;
				e.pMat = src[i];
				// The cast is safe: every element of m_Stack[pass] came from
				// push() which only enters this branch with a D3D12 material.
				PrimitiveMaterialD3D12 * p12 = (PrimitiveMaterialD3D12 *)src[i].pointer();
				e.nChildIdx = p12 ? p12->m_nFirstClaimChild : INT_MAX;
				e.nStableIdx = nStableCounter++;
				entries.push_back( e );
			}
		}
		std::sort( entries.begin(), entries.end(),
			[]( const Entry & a, const Entry & b )
			{
				if ( a.nChildIdx != b.nChildIdx )
					return a.nChildIdx < b.nChildIdx;
				return a.nStableIdx < b.nStableIdx;	// stable within same child
			} );
		for ( size_t i = 0; i < entries.size(); ++i )
			m_Stack[pass].push( entries[i].pMat );
	}

	// Clear per-worker state now that the main m_Stack owns everything.
	for ( int w = 0; w < m_WorkerStates.size(); ++w )
	{
		WorkerRenderState & ws = m_WorkerStates[w];
		for ( int pass = 0; pass < PASS_COUNT; ++pass )
			ws.m_Stack[pass].release();
		ws.m_pCurrentMaterial = NULL;
		ws.m_pCurrentTransform = NULL;
	}
}

DisplayEffect::Ref DisplayDeviceD3D12::createEffect( const char * pName )
{
	EffectMap::iterator iFactory = m_EffectMap.find( pName );
	if ( iFactory == m_EffectMap.end() )
		return NULL;

	Factory * pFactory = iFactory->second;
	if ( !pFactory )
		return NULL;

	Widget::Ref pUncasted = pFactory->createWidget();
	if ( !pUncasted )
		return NULL;
	DisplayEffect::Ref pEffect = WidgetCast<DisplayEffect>( pUncasted );
	if ( !pEffect.valid() )
		return NULL;

	m_CreatedEffects.push_back( pEffect.pointer() );
	return pEffect;
}

bool DisplayDeviceD3D12::push( DisplayEffect * pEffect )
{
	m_EffectList.push_back( pEffect );
	return true;
}

bool DisplayDeviceD3D12::capture( const char * pFilename )
{
	// TODO: Implement screenshot capture for DX12
	return false;
}

//---------------------------------------------------------------------------------------------------
// Shader management
//---------------------------------------------------------------------------------------------------

ShaderD3D12::Ref DisplayDeviceD3D12::getShader( const char * pShaderName )
{
	ShaderMap::iterator iShader = m_ShaderMap.find( pShaderName );
	if ( iShader != m_ShaderMap.end() )
		return iShader->second;

	ShaderD3D12::Ref pShader = new ShaderD3D12();
	m_ShaderMap[ pShaderName ] = pShader;
	pShader->load( this, pShaderName );

	return pShader;
}

bool DisplayDeviceD3D12::releaseShader( const char * pShader )
{
	ShaderMap::iterator iShader = m_ShaderMap.find( pShader );
	if ( iShader == m_ShaderMap.end() )
		return false;

	iShader->second->release();
	m_ShaderMap.erase( iShader );
	return true;
}

void DisplayDeviceD3D12::releaseShaders()
{
	for ( ShaderMap::iterator iShader = m_ShaderMap.begin();
		iShader != m_ShaderMap.end(); ++iShader )
	{
		iShader->second->release();
	}
	m_ShaderMap.clear();
}

void DisplayDeviceD3D12::registerEffect( const char * pName, Factory * pFactory )
{
	m_EffectMap[ pName ] = pFactory;
}

//---------------------------------------------------------------------------------------------------
// PSO management
//---------------------------------------------------------------------------------------------------

ShaderD3D12 * DisplayDeviceD3D12::resolveFallbackShader( int inputLayout ) const
{
	switch ( inputLayout )
	{
	case PSOKey::IL_VERTEX:
		if ( m_pPassThroughShader.valid() )   return m_pPassThroughShader.pointer();
		break;
	case PSOKey::IL_VERTEXL:
		if ( m_pPassThroughLShader.valid() )  return m_pPassThroughLShader.pointer();
		break;
	case PSOKey::IL_VERTEXTL:
		if ( m_pPassThroughTLShader.valid() ) return m_pPassThroughTLShader.pointer();
		break;
	}
	return nullptr;
}

void DisplayDeviceD3D12::setPSO( ID3D12GraphicsCommandList * cl, ID3D12PipelineState * pPSO )
{
	if ( !cl || !pPSO )
		return;
	if ( m_pCurrentPSO == pPSO )
		return;
	cl->SetPipelineState( pPSO );
	m_pCurrentPSO = pPSO;
}

void DisplayDeviceD3D12::ensureDepthStencilState( ID3D12GraphicsCommandList * cl, D3D12_RESOURCE_STATES newState )
{
	if ( !cl || !m_pDepthStencil )
		return;
	if ( m_eDepthStencilState == newState )
		return;
	TransitionResource( cl, m_pDepthStencil.Get(), m_eDepthStencilState, newState );
	m_eDepthStencilState = newState;
}

void DisplayDeviceD3D12::bindPSO( PSOKey::InputLayoutType inputLayout, PSOKey::TopologyType topology )
{
	if ( !m_bCommandListOpen )
		return;

	PSOKey key = {};
	key.inputLayout = inputLayout;
	key.topology = topology;
	key.blendMode = m_nCurrentBlend;
	key.doubleSided = m_bCurrentDoubleSided;
	key.wireframe = (m_eFillMode == FILL_WIREFRAME);
	key.sampleCount = 1;
	// PSO RTV format must match the bound RTV exactly (D3D12 validation enforces this).
	// Three cases: shadow pass writes a depth-as-color R32F target; scene-RT-enabled
	// pre-AA material draws go to the HDR float scene RT; everything else (scene RT
	// disabled, or OVERLAY/UI after the AA pass bound the swap chain) writes the
	// R8G8B8A8 backbuffer.
	key.rtvFormat = m_bRenderingShadowMap ? DXGI_FORMAT_R32_FLOAT
		: ( m_bSceneRTEnabled && m_pSceneRT && !m_bRenderingPostAA ) ? SCENE_RT_FORMAT
		: DXGI_FORMAT_R8G8B8A8_UNORM;

	// Resolve the effective shader for this draw so the PSO cache key
	// correctly distinguishes different shader programs.
	ShaderD3D12 * pShader = nullptr;

	if ( inputLayout == PSOKey::IL_VERTEXTL )
	{
		// Pre-transformed (screen-space) geometry: no depth test/write, always use TL passthrough shader
		key.depthEnable = false;
		key.depthWrite = false;
		key.dsvFormat = DXGI_FORMAT_UNKNOWN;
		pShader = m_pPassThroughTLShader.valid() ? m_pPassThroughTLShader.pointer() : nullptr;

		// In DX9, pre-transformed vertices bypass the viewport transform entirely.
		// In DX12, all vertices go through the viewport.  If updateProjection() set the
		// viewport to a 3D sub-window, UI draws would be squashed into that region.
		// Reset the viewport to the full render window for correct TL rendering.
		RectInt rw = renderWindow();
		D3D12_VIEWPORT fullVP = {};
		fullVP.TopLeftX = 0;
		fullVP.TopLeftY = 0;
		fullVP.Width = (float)rw.width();
		fullVP.Height = (float)rw.height();
		fullVP.MinDepth = 0.0f;
		fullVP.MaxDepth = 1.0f;
		m_pCommandList->RSSetViewports( 1, &fullVP );

		D3D12_RECT fullScissor = { 0, 0, (LONG)rw.width(), (LONG)rw.height() };
		m_pCommandList->RSSetScissorRects( 1, &fullScissor );
	}
	else
	{
		if ( m_bRenderingShadowMap )
		{
			// Shadow pass: render to R32_FLOAT color RT, no depth buffer
			// Use MIN blend to keep closest depth (blend mode 5)
			key.depthEnable = false;
			key.depthWrite = false;
			key.dsvFormat = DXGI_FORMAT_UNKNOWN;
			key.blendMode = 5;	// MIN blend for shadow map
		}
		else
		{
			key.depthEnable = true;
			// Depth-write defaults to on for opaque (NONE blend) and off for
			// any blended geometry — but cloaked ship hulls override this so
			// the front silhouette occludes the back side and the rim shimmer
			// doesn't bleed through the body.  setForceDepthWrite() on the
			// material drives m_bCurrentForceDepthWrite via setupBlending().
			key.depthWrite = (m_nCurrentBlend == 0) || m_bCurrentForceDepthWrite;
			key.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		}
		pShader = m_pMatShader.valid() ? m_pMatShader.pointer() : nullptr;
	}

	// Resolve fallback shader if no explicit shader set
	if ( !pShader || !pShader->valid() )
		pShader = resolveFallbackShader( inputLayout );

	// Encode the effective shader bytecode pointers into the key so different
	// shaders produce distinct PSO cache entries.
	if ( pShader && pShader->valid() )
	{
		key.vsBytecode = pShader->vertexShaderBlob() ? pShader->vertexShaderBlob()->GetBufferPointer() : nullptr;
		key.psBytecode = pShader->pixelShaderBlob() ? pShader->pixelShaderBlob()->GetBufferPointer() : nullptr;
	}

	setPSO( m_pCommandList.Get(), getOrCreatePSO( key, pShader ) );
}

//---------------------------------------------------------------------------------------------------

// Build a stable, human-readable cache key for ID3D12PipelineLibrary.  The
// in-memory PSOKey discriminates by shader BYTECODE POINTER, which is fine
// within one process but useless across launches (different addresses).
// Stable identity comes from the shader's source filename, which is invariant.
static std::wstring makePSOCacheName( const PSOKey & key, ShaderD3D12 * pShader )
{
	const char * pShaderName = (pShader && pShader->valid())
		? (const char *)pShader->shaderName()
		: "passthru";
	char buf[512];
	std::snprintf( buf, sizeof buf,
		"il%d_t%d_b%u_ds%d_dw%d_de%d_w%d_rtv%u_dsv%u_sc%u_%s",
		(int)key.inputLayout, (int)key.topology, (unsigned)key.blendMode,
		(int)key.doubleSided, (int)key.depthWrite, (int)key.depthEnable, (int)key.wireframe,
		(unsigned)key.rtvFormat, (unsigned)key.dsvFormat, (unsigned)key.sampleCount,
		pShaderName );
	wchar_t wbuf[512] = {};
	MultiByteToWideChar( CP_ACP, 0, buf, -1, wbuf, _countof(wbuf) );
	return std::wstring( wbuf );
}

// pso_cache.bin lives next to the running exe — a per-install file, not
// per-user-roaming, because it's keyed on the GPU + driver of THIS machine.
// If the user moves to a different GPU the blob's driver hash mismatches and
// CreatePipelineLibrary returns D3D12_ERROR_DRIVER_VERSION_MISMATCH, which
// initPSOLibrary handles by discarding and starting fresh.
static std::wstring psoCacheFilePath()
{
	wchar_t exePath[MAX_PATH] = {};
	GetModuleFileNameW( nullptr, exePath, MAX_PATH );
	std::wstring p = exePath;
	const auto slash = p.find_last_of( L"\\/" );
	if ( slash != std::wstring::npos ) p.resize( slash + 1 );
	p += L"pso_cache.bin";
	return p;
}

void DisplayDeviceD3D12::initPSOLibrary()
{
	ComPtr<ID3D12Device1> pDevice1;
	if ( FAILED( m_pDevice.As( &pDevice1 ) ) )
	{
		TRACE( "DisplayDeviceD3D12::initPSOLibrary - ID3D12Device1 unavailable; PSO caching disabled" );
		return;
	}

	// Read any existing cache blob.  The blob memory must live as long as the
	// library does — store on the device.
	HANDLE h = CreateFileW( psoCacheFilePath().c_str(), GENERIC_READ, FILE_SHARE_READ,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
	if ( h != INVALID_HANDLE_VALUE )
	{
		LARGE_INTEGER size{};
		if ( GetFileSizeEx( h, &size ) && size.QuadPart > 0 && size.QuadPart < (1LL << 30) )
		{
			m_PSOCacheBlob.resize( (size_t)size.QuadPart );
			DWORD got = 0;
			if ( !ReadFile( h, m_PSOCacheBlob.data(), (DWORD)m_PSOCacheBlob.size(), &got, nullptr )
				|| got != m_PSOCacheBlob.size() )
			{
				m_PSOCacheBlob.clear();
			}
		}
		CloseHandle( h );
	}

	HRESULT hr = pDevice1->CreatePipelineLibrary(
		m_PSOCacheBlob.empty() ? nullptr : m_PSOCacheBlob.data(),
		m_PSOCacheBlob.size(),
		IID_PPV_ARGS( &m_pPSOLibrary ) );

	// Driver upgrade / OS upgrade / corrupt blob → fall back to an empty
	// library so this run still benefits from in-process caching, and the
	// next savePSOLibrary() rewrites a fresh blob.
	if ( FAILED(hr) )
	{
		TRACE( "DisplayDeviceD3D12::initPSOLibrary - existing cache rejected (hr=0x%08X), starting fresh", hr );
		m_PSOCacheBlob.clear();
		hr = pDevice1->CreatePipelineLibrary( nullptr, 0, IID_PPV_ARGS( &m_pPSOLibrary ) );
		if ( FAILED(hr) )
		{
			TRACE( "DisplayDeviceD3D12::initPSOLibrary - CreatePipelineLibrary failed (hr=0x%08X)", hr );
			m_pPSOLibrary.Reset();
		}
	}
	else
	{
		TRACE( "DisplayDeviceD3D12::initPSOLibrary - loaded %zu bytes from pso_cache.bin", m_PSOCacheBlob.size() );
	}
}

void DisplayDeviceD3D12::savePSOLibrary()
{
	if ( !m_pPSOLibrary ) return;

	const SIZE_T sz = m_pPSOLibrary->GetSerializedSize();
	if ( sz == 0 ) return;

	// Skip writing if the library is unchanged (same size as the blob we
	// loaded).  Cheap heuristic — GetSerializedSize is byte-exact for the
	// library content, so size match implies content match in practice.
	if ( sz == m_PSOCacheBlob.size() ) return;

	std::vector<unsigned char> buf( sz );
	if ( FAILED( m_pPSOLibrary->Serialize( buf.data(), sz ) ) )
		return;

	HANDLE h = CreateFileW( psoCacheFilePath().c_str(), GENERIC_WRITE, 0,
		nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
	if ( h == INVALID_HANDLE_VALUE ) return;
	DWORD wrote = 0;
	WriteFile( h, buf.data(), (DWORD)buf.size(), &wrote, nullptr );
	CloseHandle( h );
	TRACE( "DisplayDeviceD3D12::savePSOLibrary - wrote %zu bytes to pso_cache.bin", (size_t)sz );
}

ID3D12PipelineState * DisplayDeviceD3D12::getOrCreatePSO( const PSOKey & key, ShaderD3D12 * pShader )
{
	auto it = m_PSOCache.find( key );
	if ( it != m_PSOCache.end() )
		return it->second.Get();

	// Create a new PSO
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_pRootSignature.Get();

	// Shader bytecode — fall back to a layout-compatible passthrough shader
	// to avoid CREATEINPUTLAYOUT_MISSINGELEMENT when the material shader is not set.
	if ( pShader && pShader->valid() )
	{
		psoDesc.VS = pShader->vertexShaderBytecode();
		psoDesc.PS = pShader->pixelShaderBytecode();
	}
	else
	{
		ShaderD3D12 * pFallback = resolveFallbackShader( key.inputLayout );
		if ( pFallback && pFallback->valid() )
		{
			psoDesc.VS = pFallback->vertexShaderBytecode();
			psoDesc.PS = pFallback->pixelShaderBytecode();
		}
	}

	// Input layout based on vertex type
	D3D12_INPUT_ELEMENT_DESC vertexLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
	D3D12_INPUT_ELEMENT_DESC vertexLLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
	D3D12_INPUT_ELEMENT_DESC vertexTLLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	switch ( key.inputLayout )
	{
	case PSOKey::IL_VERTEX:
		psoDesc.InputLayout = { vertexLayout, _countof(vertexLayout) };
		break;
	case PSOKey::IL_VERTEXL:
		psoDesc.InputLayout = { vertexLLayout, _countof(vertexLLayout) };
		break;
	case PSOKey::IL_VERTEXTL:
		psoDesc.InputLayout = { vertexTLLayout, _countof(vertexTLLayout) };
		break;
	}

	// Rasterizer state
	psoDesc.RasterizerState.FillMode = key.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.CullMode = key.doubleSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
	psoDesc.RasterizerState.FrontCounterClockwise = FALSE;	// CW front faces — matches DX9 default (D3DCULL_CCW)
	psoDesc.RasterizerState.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
	psoDesc.RasterizerState.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
	psoDesc.RasterizerState.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
	psoDesc.RasterizerState.DepthClipEnable = TRUE;
	psoDesc.RasterizerState.MultisampleEnable = FALSE;
	psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;

	// Blend state
	D3D12_RENDER_TARGET_BLEND_DESC & rtBlend = psoDesc.BlendState.RenderTarget[0];
	rtBlend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	switch ( key.blendMode )
	{
	case 0:	// NONE
		rtBlend.BlendEnable = FALSE;
		break;
	case 1:	// ALPHA
		rtBlend.BlendEnable = TRUE;
		rtBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rtBlend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
		rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
		rtBlend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case 2:	// ALPHA_INV
		rtBlend.BlendEnable = TRUE;
		rtBlend.SrcBlend = D3D12_BLEND_INV_SRC_ALPHA;
		rtBlend.DestBlend = D3D12_BLEND_SRC_ALPHA;
		rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
		rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
		rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
		rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case 3:	// ADDITIVE
		rtBlend.BlendEnable = TRUE;
		rtBlend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rtBlend.DestBlend = D3D12_BLEND_ONE;
		rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
		rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
		rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
		rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case 4:	// ADDITIVE_INV
		rtBlend.BlendEnable = TRUE;
		rtBlend.SrcBlend = D3D12_BLEND_INV_SRC_ALPHA;
		rtBlend.DestBlend = D3D12_BLEND_ONE;
		rtBlend.BlendOp = D3D12_BLEND_OP_ADD;
		rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
		rtBlend.DestBlendAlpha = D3D12_BLEND_ZERO;
		rtBlend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		break;
	case 5:	// MIN — shadow map: keep closest depth
		rtBlend.BlendEnable = TRUE;
		rtBlend.SrcBlend = D3D12_BLEND_ONE;
		rtBlend.DestBlend = D3D12_BLEND_ONE;
		rtBlend.BlendOp = D3D12_BLEND_OP_MIN;
		rtBlend.SrcBlendAlpha = D3D12_BLEND_ONE;
		rtBlend.DestBlendAlpha = D3D12_BLEND_ONE;
		rtBlend.BlendOpAlpha = D3D12_BLEND_OP_MIN;
		break;
	}

	// Depth stencil state
	psoDesc.DepthStencilState.DepthEnable = key.depthEnable ? TRUE : FALSE;
	psoDesc.DepthStencilState.DepthWriteMask = key.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	psoDesc.DepthStencilState.StencilEnable = FALSE;

	// Topology
	psoDesc.PrimitiveTopologyType = (key.topology == PSOKey::TOPO_LINE) ?
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// Render target and depth formats
	if ( key.rtvFormat != DXGI_FORMAT_UNKNOWN )
	{
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = key.rtvFormat;
	}
	else
	{
		psoDesc.NumRenderTargets = 0;	// depth-only pass (shadow map)
	}
	psoDesc.DSVFormat = key.dsvFormat;
	psoDesc.SampleDesc.Count = key.sampleCount;
	psoDesc.SampleMask = UINT_MAX;

	ComPtr<ID3D12PipelineState> pso;

	// Try the persistent library first.  A hit here skips DXIL→GPU codegen
	// (the dominant cost of CreateGraphicsPipelineState on first launch) and
	// returns a deserialized PSO in single-digit ms.  Misses fall through to
	// the normal compile path and we store back below.
	std::wstring psoCacheName;
	if ( m_pPSOLibrary )
	{
		psoCacheName = makePSOCacheName( key, pShader );
		HRESULT hrLoad = m_pPSOLibrary->LoadGraphicsPipeline(
			psoCacheName.c_str(), &psoDesc, IID_PPV_ARGS( &pso ) );
		if ( SUCCEEDED(hrLoad) )
		{
			m_PSOCache[key] = pso;
			return pso.Get();
		}
		// E_INVALIDARG / DXGI_ERROR_NOT_FOUND mean "not in library" — normal
		// for cold launches and new PSO permutations; just compile below.
	}

	HRESULT hr = m_pDevice->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&pso) );
	if ( FAILED(hr) )
	{
		TRACE( "DisplayDeviceD3D12::getOrCreatePSO() - Failed to create PSO!" );
		return nullptr;
	}

	// Store the freshly compiled PSO back into the library so the next launch
	// finds it.  StorePipeline returning E_INVALIDARG on a duplicate name is
	// non-fatal — just means another PSO already claimed the slot (shouldn't
	// happen because m_PSOCache de-duplicates upstream, but harmless).
	if ( m_pPSOLibrary && !psoCacheName.empty() )
		m_pPSOLibrary->StorePipeline( psoCacheName.c_str(), pso.Get() );

	m_PSOCache[key] = pso;
	return pso.Get();
}

//---------------------------------------------------------------------------------------------------
// Dynamic buffer allocation
//---------------------------------------------------------------------------------------------------

UploadRingBuffer::Allocation DisplayDeviceD3D12::allocateDynamic( UINT size, UINT alignment )
{
	return m_DynamicVB[m_nFrameIndex].Allocate( size, alignment );
}

UploadRingBuffer::Allocation DisplayDeviceD3D12::allocateCB( UINT size )
{
	return m_DynamicCB[m_nFrameIndex].Allocate( AlignCB(size), 256 );
}

//---------------------------------------------------------------------------------------------------
// SRV descriptor allocation
//---------------------------------------------------------------------------------------------------

UINT DisplayDeviceD3D12::allocateSRV()
{
	return m_SRVStagingHeap.Allocate();
}

D3D12_CPU_DESCRIPTOR_HANDLE DisplayDeviceD3D12::getSRVCPUHandle( UINT index )
{
	return m_SRVHeap.GetCPUHandle( index );
}

D3D12_GPU_DESCRIPTOR_HANDLE DisplayDeviceD3D12::getSRVGPUHandle( UINT index )
{
	return m_SRVHeap.GetGPUHandle( index );
}

UINT DisplayDeviceD3D12::allocSRVSlots( UINT count )
{
	// CAS-with-wrap bounded to the current frame's slab.  Wrapping back to
	// this frame's slab-base overwrites earlier in-frame slots (same tradeoff
	// as always), but stays OUT of other frames' slabs so frame N+1 can't
	// clobber frame N's in-flight descriptors.
	const UINT slabBase = (UINT)m_nFrameIndex * MAX_SRV_DESCRIPTORS;
	const UINT slabEnd  = slabBase + MAX_SRV_DESCRIPTORS;
	UINT current = m_nSRVFrameOffset.load( std::memory_order_relaxed );
	UINT base;
	for (;;)
	{
		const bool wrap = ( current + count > slabEnd );
		base = wrap ? ( slabBase + 8u ) : current;
		const UINT next = base + count;
		if ( m_nSRVFrameOffset.compare_exchange_weak(
				current, next,
				std::memory_order_acq_rel, std::memory_order_relaxed ) )
		{
			// Warn once per session when wrap actually fires — wrap overwrites
			// earlier in-frame slot bindings and causes visible texture corruption.
			if ( wrap )
			{
				static bool s_warned = false;
				if ( !s_warned )
				{
					s_warned = true;
					TRACE( "WARN: SRV ring wrap at frame-offset=%u count=%u (MAX=%u). "
						"Mid-frame wrap = texture corruption. Bump MAX_SRV_DESCRIPTORS.",
						current, count, (UINT)MAX_SRV_DESCRIPTORS );
				}
			}
			return base;
		}
	}
}

//---------------------------------------------------------------------------------------------------
// World matrix and constant buffer binding
//---------------------------------------------------------------------------------------------------

void DisplayDeviceD3D12::setWorldMatrix( const XMMATRIX & mWorld )
{
	m_mCurrentWorld = mWorld;
	m_CBPerObject.mWorld = ShaderMatrix( mWorld );
}

void DisplayDeviceD3D12::bindPerFrameCB()
{
	if ( !m_bCommandListOpen )
		return;

	m_CBPerFrame.mView = ShaderMatrix( m_mView );
	m_CBPerFrame.mProj = ShaderMatrix( m_mProj );
	static ULONGLONG s_nStartTick = GetTickCount64();
	float fElapsedSeconds = (float)(GetTickCount64() - s_nStartTick) / 1000.0f;
	m_CBPerFrame.vCameraPos = ShaderFloat4( m_Proj.m_vPosition.x, m_Proj.m_vPosition.y, m_Proj.m_vPosition.z, fElapsedSeconds );

	// vGlobalAmbient is consumed by the shader's hemisphere-ambient term
	// (Default.hlsl:265) which then multiplies linear texture samples — so
	// the value uploaded here must be in linear space.  m_cAmbientLight is
	// authored as sRGB (e.g. UNIVERSE_AMBIENT = (63,36,72)); linearise it
	// when the sRGB pipeline is on.  srgbToLinear is a no-op when off.
	const float inv = 1.0f / 255.0f;
	m_CBPerFrame.vGlobalAmbient = srgbColorToLinear( ShaderFloat4(
		m_cAmbientLight.m_R * inv, m_cAmbientLight.m_G * inv,
		m_cAmbientLight.m_B * inv, m_cAmbientLight.m_A * inv ) );
	m_CBPerFrame.szShadowMap = ShaderFloat2( (float)m_szShadowMap.width, (float)m_szShadowMap.height );
	m_CBPerFrame.fShadowDistance = m_fShadowRadius;
	m_CBPerFrame.fShadowDepthRange = m_fShadowDepthRange;

	// Compute world-space shadow focus for cascade selection in shaders
	Vector3 vWorldFocus( m_Proj.m_vPosition + (m_Proj.m_mFrame % m_vShadowFocus) );
	m_CBPerFrame.vShadowFocus = ShaderFloat4( vWorldFocus.x, vWorldFocus.y, vWorldFocus.z, 0.0f );

	// Chunk 4 — diffuse SH coefficients for IBL ambient.
	computeDiffuseSH( m_CBPerFrame.vSHCoefs );

	// PBR specular IBL — the prefiltered env cube is baked from the same
	// procedural sky+sun model as the SH above.  Check for drift since the
	// last bake (zone change, sun-position shift, ambient-tint change) and
	// re-bake the cube only if needed.  LUT is environment-independent and
	// never re-bakes.
	maybeRebakeEnvCube();

	// Chunk 5 — primary directional sun, exposed for shaders that need
	// the raw sun (Planet.hlsl atmosphere/day-night, anything else that
	// wants a vector and not the SH-baked irradiance).  Convention matches
	// addDirectionalLight: vLightDirection points AWAY from the source,
	// so direction TOWARD the sun is -vSunDir.xyz.  .w gates "sun present"
	// for shaders that fall back when no directional light is bound.
	{
		bool bSunFound = false;
		for ( auto it = m_Lights.begin(); it != m_Lights.end(); ++it )
		{
			const LightInfo & l = it->second;
			if ( l.type == 3 /* directional */ )
			{
				m_CBPerFrame.vSunDir   = ShaderFloat4( l.dirX, l.dirY, l.dirZ, 1.0f );
				m_CBPerFrame.vSunColor = ShaderFloat4(
					srgbToLinear( l.r ), srgbToLinear( l.g ), srgbToLinear( l.b ), 0.0f );
				bSunFound = true;
				break;
			}
		}
		if ( !bSunFound )
		{
			// Sane default: sun straight down, neutral white, gate closed.
			m_CBPerFrame.vSunDir   = ShaderFloat4( 0.0f, -1.0f, 0.0f, 0.0f );
			m_CBPerFrame.vSunColor = ShaderFloat4( 1.0f,  1.0f, 1.0f, 0.0f );
		}
	}

	// Chunk 5.5 — visible sun world position.  NounStar::render submits
	// each star it draws via submitSunCandidate, the closest wins, and
	// we expose that here for shaders that want geometry-correct sun
	// direction (Planet.hlsl) instead of the art-directable vSunDir
	// vector that just inherits the NodeLight node's frame.k axis.
	{
		Vector3 vSunWorld;
		if ( getSunCandidate( vSunWorld ) )
			m_CBPerFrame.vSunWorldPos = ShaderFloat4( vSunWorld.x, vSunWorld.y, vSunWorld.z, 1.0f );
		else
			m_CBPerFrame.vSunWorldPos = ShaderFloat4( 0.0f, 0.0f, 0.0f, 0.0f );
	}

	// Pack celestial occluders submitted this frame by NounPlanet::render
	// et al.  Used by:
	//  - Default.hlsl, for the directional sun shadow ray-sphere test
	//    (per-pixel against worldPos+radius).
	//  - LimbGlow.hlsl, for the per-pixel rim glow test (against the
	//    occluder's tangent-plane silhouette disc, packed in the LimbGlow
	//    CB by DisplayEffectLimbGlow).
	int occCount = getOccluderCount();
	if ( occCount < 0 ) occCount = 0;
	if ( occCount > 32 ) occCount = 32;
	m_CBPerFrame.nNumOccluders = occCount;
	for ( int i = 0; i < occCount; ++i )
	{
		const OccluderInfo & o = getOccluder( i );
		m_CBPerFrame.vOccluders[i] = ShaderFloat4(
			o.worldPos.x, o.worldPos.y, o.worldPos.z, o.radius );
	}
	for ( int i = occCount; i < 32; ++i )
		m_CBPerFrame.vOccluders[i] = ShaderFloat4( 0, 0, 0, 0 );

	// PCF tap count for sampleShadowCascade in Default.hlsl.  Each tier
	// takes a prefix of the 32-point Poisson disk in ShadowSampling.hlsli.
	switch ( DisplayDevice::sm_nShaderDetail )
	{
	case DisplayDevice::SHADER_DETAIL_LOW:		m_CBPerFrame.nShadowPCFTaps =  4; break;
	case DisplayDevice::SHADER_DETAIL_MEDIUM:	m_CBPerFrame.nShadowPCFTaps =  8; break;
	case DisplayDevice::SHADER_DETAIL_HIGH:		m_CBPerFrame.nShadowPCFTaps = 16; break;
	case DisplayDevice::SHADER_DETAIL_EXTREME:	m_CBPerFrame.nShadowPCFTaps = 32; break;
	default:									m_CBPerFrame.nShadowPCFTaps = 16; break;
	}

	UploadRingBuffer::Allocation alloc = allocateCB( sizeof(CBPerFrame) );
	memcpy( alloc.cpuAddress, &m_CBPerFrame, sizeof(CBPerFrame) );

	// Root parameter 0 = CBV for per-frame constants
	m_pCommandList->SetGraphicsRootConstantBufferView( 0, alloc.gpuAddress );
}

//---------------------------------------------------------------------------------------------------
// Compute Spherical Harmonics coefficients for diffuse environment lighting.
// Integrates a procedural environment (sky colour + sun fill) over 32 sphere
// samples (Fibonacci distribution) onto a 9-coefficient L=2 SH basis.
// Cheap (~300 ALU ops) — runs every frame so the SH adapts immediately when
// ambient or sun direction changes.  The shader evaluates these via
// Ramamoorthi 2001's polynomial form for cosine-weighted irradiance.
//---------------------------------------------------------------------------------------------------

void DisplayDeviceD3D12::computeDiffuseSH( ShaderFloat4 outCoefs[9] )
{
	// Find the directional (sun) light for directional fill component.
	float sunDirX = 0.0f, sunDirY = -1.0f, sunDirZ = 0.0f;
	float sunR = 0.0f, sunG = 0.0f, sunB = 0.0f;
	for ( auto it = m_Lights.begin(); it != m_Lights.end(); ++it )
	{
		const LightInfo & l = it->second;
		if ( l.type == 3 /* directional */ )
		{
			sunDirX = l.dirX; sunDirY = l.dirY; sunDirZ = l.dirZ;
			sunR = l.r; sunG = l.g; sunB = l.b;
			break;
		}
	}

	// Linearise sun colour to match the linear lighting math.  No-op when
	// sRGB pipeline is disabled.
	float sunLinR = srgbToLinear( sunR );
	float sunLinG = srgbToLinear( sunG );
	float sunLinB = srgbToLinear( sunB );

	// Linearised sky colour from m_cAmbientLight (UNIVERSE_AMBIENT).
	const float inv = 1.0f / 255.0f;
	float skyR = srgbToLinear( m_cAmbientLight.m_R * inv );
	float skyG = srgbToLinear( m_cAmbientLight.m_G * inv );
	float skyB = srgbToLinear( m_cAmbientLight.m_B * inv );

	// Modest scale on the sky term to match the perceived brightness of the
	// legacy hemisphere ambient (which lerped *0.6→*1.0 of UNIVERSE_AMBIENT
	// per up-direction).  SH integrates over the full sphere with cosine
	// weighting so the effective per-pixel contribution is comparable.
	const float SKY_SCALE = 1.0f;
	skyR *= SKY_SCALE; skyG *= SKY_SCALE; skyB *= SKY_SCALE;

	// Sun-fill weight — how strongly to inject the sun's directional energy
	// into the SH.  Low value because the sun is already accounted for via
	// the directional NodeLight in the per-light pass; this is just for
	// shadow-side fill that lets the sun's hue tint the dark side.
	const float SUN_FILL = 0.25f;

	// Accumulation buffers.
	float coefR[9] = { 0,0,0,0,0,0,0,0,0 };
	float coefG[9] = { 0,0,0,0,0,0,0,0,0 };
	float coefB[9] = { 0,0,0,0,0,0,0,0,0 };

	const int N = 32;
	const float PI = 3.14159265358979f;
	const float weight = 4.0f * PI / (float)N;	// solid angle per sample
	const float phi = (1.0f + sqrtf(5.0f)) * 0.5f;	// golden ratio for Fibonacci sphere

	for ( int i = 0; i < N; ++i )
	{
		// Fibonacci sphere direction.
		float t = ((float)i + 0.5f) / (float)N;
		float incl = acosf( 1.0f - 2.0f * t );
		float azim = 2.0f * PI * (float)i / phi;
		float dx = sinf(incl) * cosf(azim);
		float dy = cosf(incl);
		float dz = sinf(incl) * sinf(azim);

		// Procedural environment colour at direction d.
		// Sky term: brighter "up", darker "down".  -toSun direction is
		// where the sun lives; positive dot means "this sample direction
		// points toward the sun" → add sun fill.
		float skyT = dy * 0.5f + 0.5f;	// 0=down, 1=up
		float skyMul = 0.3f + 0.7f * skyT;	// dim down, full up
		float L_R = skyR * skyMul;
		float L_G = skyG * skyMul;
		float L_B = skyB * skyMul;

		// Sun fill — vLightDirection points AWAY from the light (per
		// addDirectionalLight convention), so direction TOWARD the sun is
		// -sunDir.  Soft-power weighting (squared) so the sun tints a
		// localised hemisphere rather than half the sky.
		float sunDot = dx * (-sunDirX) + dy * (-sunDirY) + dz * (-sunDirZ);
		if ( sunDot > 0.0f )
		{
			float w = sunDot * sunDot * SUN_FILL;
			L_R += sunLinR * w;
			L_G += sunLinG * w;
			L_B += sunLinB * w;
		}

		// L=2 real SH basis Y_lm at direction (dx,dy,dz).
		float Y0 = 0.282095f;
		float Y1 = 0.488603f * dy;
		float Y2 = 0.488603f * dz;
		float Y3 = 0.488603f * dx;
		float Y4 = 1.092548f * dx * dy;
		float Y5 = 1.092548f * dy * dz;
		float Y6 = 0.315392f * (3.0f * dz * dz - 1.0f);
		float Y7 = 1.092548f * dx * dz;
		float Y8 = 0.546274f * (dx * dx - dy * dy);

		// Accumulate L * Y * dω.
		coefR[0] += L_R * Y0 * weight; coefG[0] += L_G * Y0 * weight; coefB[0] += L_B * Y0 * weight;
		coefR[1] += L_R * Y1 * weight; coefG[1] += L_G * Y1 * weight; coefB[1] += L_B * Y1 * weight;
		coefR[2] += L_R * Y2 * weight; coefG[2] += L_G * Y2 * weight; coefB[2] += L_B * Y2 * weight;
		coefR[3] += L_R * Y3 * weight; coefG[3] += L_G * Y3 * weight; coefB[3] += L_B * Y3 * weight;
		coefR[4] += L_R * Y4 * weight; coefG[4] += L_G * Y4 * weight; coefB[4] += L_B * Y4 * weight;
		coefR[5] += L_R * Y5 * weight; coefG[5] += L_G * Y5 * weight; coefB[5] += L_B * Y5 * weight;
		coefR[6] += L_R * Y6 * weight; coefG[6] += L_G * Y6 * weight; coefB[6] += L_B * Y6 * weight;
		coefR[7] += L_R * Y7 * weight; coefG[7] += L_G * Y7 * weight; coefB[7] += L_B * Y7 * weight;
		coefR[8] += L_R * Y8 * weight; coefG[8] += L_G * Y8 * weight; coefB[8] += L_B * Y8 * weight;
	}

	for ( int j = 0; j < 9; ++j )
	{
		outCoefs[j].x = coefR[j];
		outCoefs[j].y = coefG[j];
		outCoefs[j].z = coefB[j];
		outCoefs[j].w = 0.0f;
	}
}

void DisplayDeviceD3D12::bindPerObjectCB()
{
	if ( !m_bCommandListOpen )
		return;

	// Same byte-compare cache as bindPerMaterialCB / bindPerLightCB.  Major win
	// for OVERLAY/text rendering: Font::push fans out one PrimitiveSetTransform
	// per glyph batch and many adjacent glyphs share the same world matrix —
	// previously every one allocated + memcpy'd + SetGraphicsRootCBV'd a fresh
	// CB.  Single 64-byte struct (one matrix), so memcmp is one cmpxchg-equiv.
	if ( m_bLastObjCBValid && memcmp( &m_CBPerObject, &m_LastObjCB, sizeof(CBPerObject) ) == 0 )
	{
		m_pCommandList->SetGraphicsRootConstantBufferView( 1, m_nLastObjCBGpuVA );
		++m_nObjCBSkipped;
		return;
	}

	UploadRingBuffer::Allocation alloc = allocateCB( sizeof(CBPerObject) );
	memcpy( alloc.cpuAddress, &m_CBPerObject, sizeof(CBPerObject) );

	// Root parameter 1 = CBV for per-object constants
	m_pCommandList->SetGraphicsRootConstantBufferView( 1, alloc.gpuAddress );

	m_LastObjCB = m_CBPerObject;
	m_bLastObjCBValid = true;
	m_nLastObjCBGpuVA = alloc.gpuAddress;
	++m_nObjCBUploads;
}

void DisplayDeviceD3D12::bindPerMaterialCB( const CBPerMaterial & mat )
{
	if ( !m_bCommandListOpen )
		return;

	// Byte-compare against last bound CB this frame.  When the struct is
	// identical we reuse the previous GPU VA — saves the ring-buffer allocation,
	// the memcpy, and the SetGraphicsRootConstantBufferView API call.
	if ( m_bLastMatCBValid && memcmp( &mat, &m_LastMatCB, sizeof(CBPerMaterial) ) == 0 )
	{
		m_pCommandList->SetGraphicsRootConstantBufferView( 2, m_nLastMatCBGpuVA );
		++m_nMatCBSkipped;
		return;
	}

	UploadRingBuffer::Allocation alloc = allocateCB( sizeof(CBPerMaterial) );
	memcpy( alloc.cpuAddress, &mat, sizeof(CBPerMaterial) );

	// Root parameter 2 = CBV for per-material constants
	m_pCommandList->SetGraphicsRootConstantBufferView( 2, alloc.gpuAddress );

	m_LastMatCB = mat;
	m_bLastMatCBValid = true;
	m_nLastMatCBGpuVA = alloc.gpuAddress;
	++m_nMatCBUploads;
}

void DisplayDeviceD3D12::bindPerLightCB( const CBPerLight & light )
{
	if ( !m_bCommandListOpen )
		return;

	// Last-binding fast path (covers single-light scenes).
	if ( m_bLastLightCBValid && memcmp( &light, &m_LastLightCB, sizeof(CBPerLight) ) == 0 )
	{
		m_pCommandList->SetGraphicsRootConstantBufferView( 3, m_nLastLightCBGpuVA );
		++m_nLightCBSkipped;
		return;
	}

	// Multi-light fallback: scan the ring for a recent matching payload.
	// Hit rate is ~100% in multi-light scenes where every material rebinds
	// the same N lights in the same order — the previous frame's binding
	// pattern repeats, and only the FIRST material in a frame populates
	// the ring (subsequent ones all hit).
	for ( int i = 0; i < LIGHT_CB_RING_SLOTS; ++i )
	{
		if ( !m_LightCBRingValid[i] )
			continue;
		if ( memcmp( &light, &m_LightCBRing[i], sizeof(CBPerLight) ) == 0 )
		{
			m_pCommandList->SetGraphicsRootConstantBufferView( 3, m_LightCBRingVA[i] );
			m_LastLightCB = light;
			m_bLastLightCBValid = true;
			m_nLastLightCBGpuVA = m_LightCBRingVA[i];
			++m_nLightCBSkipped;
			return;
		}
	}

	UploadRingBuffer::Allocation alloc = allocateCB( sizeof(CBPerLight) );
	memcpy( alloc.cpuAddress, &light, sizeof(CBPerLight) );

	// Root parameter 3 = CBV for per-light constants
	m_pCommandList->SetGraphicsRootConstantBufferView( 3, alloc.gpuAddress );

	m_LastLightCB = light;
	m_bLastLightCBValid = true;
	m_nLastLightCBGpuVA = alloc.gpuAddress;

	// Insert into the ring (round-robin replace).
	m_LightCBRing[ m_LightCBRingHead ] = light;
	m_LightCBRingVA[ m_LightCBRingHead ] = alloc.gpuAddress;
	m_LightCBRingValid[ m_LightCBRingHead ] = true;
	m_LightCBRingHead = ( m_LightCBRingHead + 1 ) % LIGHT_CB_RING_SLOTS;

	++m_nLightCBUploads;
}

void DisplayDeviceD3D12::bindSRVTableIfChanged( UINT nBaseSlot )
{
	if ( !m_bCommandListOpen || !m_pCommandList )
		return;

	if ( m_bLastBoundSRVValid && m_nLastBoundSRVBase == nBaseSlot )
	{
		++m_nSRVBindSkipped;
		return;
	}

	m_pCommandList->SetGraphicsRootDescriptorTable( 4, getSRVGPUHandle( nBaseSlot ) );
	m_nLastBoundSRVBase = nBaseSlot;
	m_bLastBoundSRVValid = true;
	++m_nSRVBindCalls;
}

void DisplayDeviceD3D12::invalidateBoundSRVTable()
{
	m_bLastBoundSRVValid = false;
}

void DisplayDeviceD3D12::drainInfoQueue()
{
	if ( !m_pInfoQueue )
		return;

	UINT64 count = m_pInfoQueue->GetNumStoredMessages();
	if ( count == 0 )
		return;

	static int s_nDrainLog = 0;
	for ( UINT64 i = 0; i < count; ++i )
	{
		SIZE_T messageLength = 0;
		m_pInfoQueue->GetMessage( i, nullptr, &messageLength );
		if ( messageLength == 0 )
			continue;

		D3D12_MESSAGE * pMessage = (D3D12_MESSAGE *)malloc( messageLength );
		if ( pMessage && SUCCEEDED(m_pInfoQueue->GetMessage( i, pMessage, &messageLength )) )
		{
			// Only log warnings/errors/corruption to keep log manageable
			if ( pMessage->Severity <= D3D12_MESSAGE_SEVERITY_WARNING )
			{
				if ( s_nDrainLog < 50 )
				{
					TRACE( "D3D12 [%s] %s",
						pMessage->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ? "CORRUPTION" :
						pMessage->Severity == D3D12_MESSAGE_SEVERITY_ERROR      ? "ERROR" :
						pMessage->Severity == D3D12_MESSAGE_SEVERITY_WARNING    ? "WARNING" : "INFO",
						pMessage->pDescription );
					++s_nDrainLog;
				}
			}
		}
		if ( pMessage )
			free( pMessage );
	}

	m_pInfoQueue->ClearStoredMessages();
}

void DisplayDeviceD3D12::bindMainRootDefaults()
{
	if ( !m_bCommandListOpen )
		return;

	m_pCommandList->SetGraphicsRootSignature( m_pRootSignature.Get() );
	ID3D12DescriptorHeap * heaps[] = { m_SRVHeap.Get(), m_SamplerHeap.Get() };
	m_pCommandList->SetDescriptorHeaps( _countof(heaps), heaps );

	// Changing root signature / descriptor heaps clears all root parameter
	// bindings in D3D12.  Drop the redundant-bind cache so the next draw
	// re-issues its SetGraphicsRoot* calls instead of assuming they're still live.
	m_bLastBoundSRVValid = false;
	m_bLastMatCBValid = false;
	m_bLastLightCBValid = false;

	// Bind descriptor tables for SRVs [4] and samplers [5]
	m_pCommandList->SetGraphicsRootDescriptorTable( 4, m_SRVHeap.GetGPUHandle( 0 ) );
	m_nLastBoundSRVBase = 0;
	m_bLastBoundSRVValid = true;
	++m_nSRVBindCalls;
	m_pCommandList->SetGraphicsRootDescriptorTable( 5, m_SamplerHeap.GetGPUHandle( 0 ) );

	// Bind the per-frame constant buffer
	bindPerFrameCB();

	// Bind default per-object, per-material, and per-light CBs so that all
	// root parameters are valid before any draw call.  Without this, the GPU
	// reads garbage if a draw fires before the material/transform sets them.
	m_mCurrentWorld = XMMatrixIdentity();
	m_CBPerObject.mWorld = ShaderMatrix( m_mCurrentWorld );
	bindPerObjectCB();

	CBPerMaterial defaultMat = {};
	defaultMat.vMatDiffuse = ShaderFloat4( 1, 1, 1, 1 );
	defaultMat.vMatAmbient = ShaderFloat4( 1, 1, 1, 1 );
	defaultMat.bEnableAmbient = 1;
	bindPerMaterialCB( defaultMat );

	CBPerLight defaultLight = {};
	defaultLight.nLightType = 3;		// directional
	defaultLight.vLightDiffuse = ShaderFloat4( 1, 1, 1, 1 );
	defaultLight.vLightDirection = ShaderFloat4( 0, -1, 0, 0 );
	bindPerLightCB( defaultLight );

	// Explicitly rebind the back buffer as the render target with no DSV.
	// TL materials (UI, fonts) use PSOs with dsvFormat=UNKNOWN.  applyFXAA
	// already bound this, but be explicit so OVERLAY is independent of FXAA state.
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RTVHeap.GetCPUHandle( m_nFrameIndex );
	m_pCommandList->OMSetRenderTargets( 1, &rtv, FALSE, nullptr );

	// Set a full-window viewport; bindPSO for IL_VERTEXTL resets this per-draw
	// anyway, but makes OVERLAY safe even before a TL PSO is bound.
	RectInt rw = renderWindow();
	D3D12_VIEWPORT viewport = {};
	viewport.Width = (float)rw.width();
	viewport.Height = (float)rw.height();
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	m_pCommandList->RSSetViewports( 1, &viewport );

	D3D12_RECT scissor = { 0, 0, (LONG)rw.width(), (LONG)rw.height() };
	m_pCommandList->RSSetScissorRects( 1, &scissor );
}

D3D12_FILTER DisplayDeviceD3D12::getD3D12Filter() const
{
	switch ( m_eFilterMode )
	{
	case FILTER_OFF:			return D3D12_FILTER_MIN_MAG_MIP_POINT;
	case FILTER_POINT:			return D3D12_FILTER_MIN_MAG_MIP_POINT;
	case FILTER_LINEAR:			return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	case FILTER_ANISOTROPIC:	return D3D12_FILTER_ANISOTROPIC;
	default:					return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	}
}

//---------------------------------------------------------------------------------------------------
// Internal initialization
//---------------------------------------------------------------------------------------------------

bool DisplayDeviceD3D12::initializeD3D12()
{
	// Initialize COM
	CoInitializeEx( NULL, COINIT_MULTITHREADED );

	// Debug layer: validates every D3D12 API call (~5-15% CPU overhead per
	// call) and surfaces validation messages via the info queue.  Only enable
	// in debug builds — in release we want the perf back.  If you need
	// validation on a release build for one-off investigation, flip the
	// `#if 0` to `#if 1` here.
#if defined(_DEBUG) || 0
	{
		ComPtr<ID3D12Debug> debugController;
		if ( SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))) )
		{
			debugController->EnableDebugLayer();
			TRACE( "D3D12 debug layer enabled" );
		}
		else
		{
			TRACE( "D3D12 debug layer NOT available — install Graphics Tools feature" );
		}
	}
#endif

	// Create DXGI factory.  DXGI_CREATE_FACTORY_DEBUG enables additional
	// validation around swap-chain / adapter operations and pairs with the
	// D3D12 debug layer.  Same gating: debug-only.
#if defined(_DEBUG) || 0
	UINT dxgiFlags = DXGI_CREATE_FACTORY_DEBUG;
#else
	UINT dxgiFlags = 0;
#endif
	if ( FAILED(CreateDXGIFactory2(dxgiFlags, IID_PPV_ARGS(&m_pDXGIFactory))) )
		return false;

	// Find a hardware adapter
	{
		ComPtr<IDXGIAdapter1> adapter;
		for ( UINT i = 0; m_pDXGIFactory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i )
		{
			DXGI_ADAPTER_DESC1 desc;
			adapter->GetDesc1( &desc );

			if ( desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE )
				continue;

			// Try to create a D3D12 device with this adapter
			if ( SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)) )
			{
				m_pAdapter = adapter;
				break;
			}
		}
	}

	if ( !m_pAdapter )
		return false;

	// Create device
	if ( FAILED(D3D12CreateDevice(m_pAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_pDevice))) )
		return false;

	// Build the persistent PSO cache library now so getOrCreatePSO can hit it
	// on the very first PSO request.  Failure leaves m_pPSOLibrary null and
	// the engine falls back to plain CreateGraphicsPipelineState — no harm.
	initPSOLibrary();

	// Query the info queue so we can drain validation messages each frame.
	// Requires the debug layer to be enabled — only meaningful in debug builds.
#if defined(_DEBUG) || 0
	if ( SUCCEEDED(m_pDevice->QueryInterface(IID_PPV_ARGS(&m_pInfoQueue))) )
	{
		m_pInfoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE );
		m_pInfoQueue->SetBreakOnSeverity( D3D12_MESSAGE_SEVERITY_ERROR, FALSE );
		TRACE( "D3D12 InfoQueue available — validation messages will be logged" );
	}
#endif

	// Log adapter info
	{
		DXGI_ADAPTER_DESC1 desc;
		m_pAdapter->GetDesc1( &desc );
		char buf[256];
		wcstombs( buf, desc.Description, 256 );
		LOG_STATUS( "SysInfo", CharString().format("D3D12 Device: %s", buf) );
		LOG_STATUS( "SysInfo", CharString().format("Video Memory: %u MB", (unsigned)(desc.DedicatedVideoMemory / (1024*1024))) );
	}

	// Create command queue
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if ( FAILED(m_pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_pCommandQueue))) )
		return false;

	// Create swap chain
	if ( !createSwapChain() )
		return false;

	// Create command allocators
	for ( UINT i = 0; i < FRAME_COUNT; i++ )
	{
		if ( FAILED(m_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_pCommandAllocators[i]))) )
			return false;
	}

	// Create command list
	if ( FAILED(m_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_pCommandAllocators[0].Get(), nullptr, IID_PPV_ARGS(&m_pCommandList))) )
		return false;
	m_pCommandList->Close();

	// Create fence — start the current frame's value at 1 so Signal(1) is always > initial 0,
	// which ensures GetCompletedValue() < m_nFenceValues[frame] actually triggers waits
	if ( FAILED(m_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pFence))) )
		return false;
	m_nFenceValues[m_nFrameIndex] = 1;
	m_hFenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );

	// Create descriptor heaps
	if ( !m_RTVHeap.Create(m_pDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, MAX_RTV_DESCRIPTORS, false) )
		return false;
	if ( !m_DSVHeap.Create(m_pDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, MAX_DSV_DESCRIPTORS, false) )
		return false;
	// Shader-visible SRV heap is per-frame slabbed so frame N+1's descriptor
	// writes don't overwrite slots that frame N's GPU is still reading.
	// Total heap size = MAX_SRV_DESCRIPTORS * FRAME_COUNT, with each frame
	// using slots [frameIdx * MAX, (frameIdx+1) * MAX).
	if ( !m_SRVHeap.Create(m_pDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, MAX_SRV_DESCRIPTORS * FRAME_COUNT, true) )
		return false;
	if ( !m_SamplerHeap.Create(m_pDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, MAX_SAMPLER_DESCRIPTORS, true) )
		return false;
	if ( !m_SRVStagingHeap.Create(m_pDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, MAX_SRV_DESCRIPTORS, false) )
		return false;

	// Create render targets and depth stencil
	if ( !createRenderTargets() )
		return false;
	if ( !createDepthStencil() )
		return false;

	// Create per-frame dynamic buffers
	for ( UINT i = 0; i < FRAME_COUNT; i++ )
	{
		if ( !m_DynamicVB[i].Create(m_pDevice.Get(), DYNAMIC_VB_SIZE) )
			return false;
		if ( !m_DynamicCB[i].Create(m_pDevice.Get(), DYNAMIC_CB_SIZE) )
			return false;
	}

	// Create root signatures
	if ( !createRootSignatures() )
		return false;

	// Create default shaders
	if ( !createDefaultShaders() )
		return false;

	// Pre-fill all 8 SRV descriptor table slots with null SRVs.
	// The root signature exposes t0-t7; NVIDIA validates every entry in the table
	// at draw time even if the shader never samples them — a zero-initialized
	// descriptor causes a GPU page fault.  Null SRVs (pResource=nullptr with a
	// valid desc) return (0,0,0,0) when sampled and never fault.
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC nullSRV = {};
		nullSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		nullSRV.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		nullSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		nullSRV.Texture2D.MipLevels = 1;
		// Initialize ALL slots in every frame's slab so per-frame SRV ring
		// never has uninitialized descriptors.  FRAME_COUNT slabs × MAX slots.
		for ( UINT i = 0; i < MAX_SRV_DESCRIPTORS * FRAME_COUNT; i++ )
			m_pDevice->CreateShaderResourceView( nullptr, &nullSRV, m_SRVHeap.GetCPUHandle(i) );
	}

	// Create default samplers in the sampler heap
	{
		D3D12_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D12_FILTER_ANISOTROPIC;
		samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplerDesc.MaxAnisotropy = 16;
		samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

		UINT idx = m_SamplerHeap.Allocate();
		m_pDevice->CreateSampler( &samplerDesc, m_SamplerHeap.GetCPUHandle(idx) );

		// Shadow map sampler (point filtering, clamp)
		D3D12_SAMPLER_DESC shadowSamplerDesc = {};
		shadowSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		shadowSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		shadowSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		shadowSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		shadowSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		shadowSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

		idx = m_SamplerHeap.Allocate();
		m_pDevice->CreateSampler( &shadowSamplerDesc, m_SamplerHeap.GetCPUHandle(idx) );

		// Post-process sampler (linear filtering, clamp).  Used by HDR bloom and
		// any other fullscreen-quad effect that samples a render target.  CLAMP
		// is required: aniso/WRAP at slot 0 makes the 13-tap Gaussian wrap to
		// the opposite edge near borders, producing streaky banding when bright
		// content (e.g. a sun) sits at a screen edge.
		D3D12_SAMPLER_DESC postSamplerDesc = {};
		postSamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		postSamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		postSamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		postSamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		postSamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		postSamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

		idx = m_SamplerHeap.Allocate();
		m_pDevice->CreateSampler( &postSamplerDesc, m_SamplerHeap.GetCPUHandle(idx) );
	}

	// Enumerate supported texture formats
	enumerateTextures();

	// Get texture caps
	m_TextureP2 = false;	// DX12 doesn't require P2
	m_TextureSquare = false;

	D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
	m_pDevice->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options) );

	// Update client area first so renderWindow() returns valid dimensions
	updateClientArea( false );

	// Create dedicated upload queue for loading thread texture uploads
	initUploadQueue();

	// PBR IBL bake — needs the upload queue, so run after initUploadQueue.
	// Failure is non-fatal (PBR materials degrade to direct+diffuse-IBL
	// only when m_bPBRIBLReady is false).
	if ( !bakePBRIBL() )
		TRACE( "initialize: bakePBRIBL failed — PBR materials will render without specular IBL" );

	// Create post-process resources (scene RT + FXAA PSO; needs valid window size)
	if ( m_bSceneRTEnabled )
		createFXAA();

	return true;
}

bool DisplayDeviceD3D12::createSwapChain()
{
	RECT clientRect;
	GetClientRect( m_HWND, &clientRect );

	UINT width, height;
	if ( m_bWindowed )
	{
		width = clientRect.right - clientRect.left;
		height = clientRect.bottom - clientRect.top;
		if ( width == 0 ) width = 1;
		if ( height == 0 ) height = 1;
	}
	else
	{
		width = m_Mode.screenSize.width;
		height = m_Mode.screenSize.height;
	}

	// Store dimensions in mode for reference
	if ( m_bWindowed )
	{
		m_Mode.screenSize.width = width;
		m_Mode.screenSize.height = height;
	}

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount = FRAME_COUNT;
	swapChainDesc.Width = width;
	swapChainDesc.Height = height;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count = 1;

	ComPtr<IDXGISwapChain1> swapChain;
	HRESULT hr = m_pDXGIFactory->CreateSwapChainForHwnd(
		m_pCommandQueue.Get(), m_HWND, &swapChainDesc, nullptr, nullptr, &swapChain );
	if ( FAILED(hr) )
		return false;

	// Disable Alt+Enter fullscreen toggle
	m_pDXGIFactory->MakeWindowAssociation( m_HWND, DXGI_MWA_NO_ALT_ENTER );

	swapChain.As( &m_pSwapChain );
	m_nFrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

	return true;
}

bool DisplayDeviceD3D12::createRenderTargets()
{
	// IMPORTANT: do NOT cast the back-buffer RTV to _SRGB here even though
	// Chunk 1 of the lighting plan implies the canonical "sRGB output"
	// pattern.  The reason is the ACES Narkowicz tonemap in FXAA.hlsl:66-74
	// is a fit to the full ACES RRT + ODT.SDR.sRGB curve — its [0,1] output
	// is ALREADY sRGB-encoded for direct display, not linear.  Adding a
	// hardware gamma encode via an _SRGB RTV double-encodes the scene
	// (washed-out midtones, glow-everywhere look).  An earlier iteration
	// did exactly this and produced exactly that artefact.
	//
	// If a future chunk swaps the tonemap operator to one that outputs
	// linear values (proper ACES RRT/ODT split, or any non-sRGB-baked
	// fit), THEN the RTV should be cast to _SRGB so the hardware encodes
	// uniformly for both the scene and the post-tonemap UI overlay pass.
	// Until then, leave the view alone.
	for ( UINT i = 0; i < FRAME_COUNT; i++ )
	{
		if ( FAILED(m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_pRenderTargets[i]))) )
			return false;

		// Reuse existing RTV index if already allocated (e.g. on resize)
		if ( m_nRTVIndices[i] == UINT(-1) )
			m_nRTVIndices[i] = m_RTVHeap.Allocate();
		m_pDevice->CreateRenderTargetView( m_pRenderTargets[i].Get(), nullptr, m_RTVHeap.GetCPUHandle(m_nRTVIndices[i]) );
	}
	return true;
}

bool DisplayDeviceD3D12::createDepthStencil()
{
	DXGI_SWAP_CHAIN_DESC1 desc;
	m_pSwapChain->GetDesc1( &desc );

	// Use typeless format so we can create both DSV and SRV views (needed for SSAO)
	D3D12_RESOURCE_DESC dsDesc = {};
	dsDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	dsDesc.Width = desc.Width;
	dsDesc.Height = desc.Height;
	dsDesc.DepthOrArraySize = 1;
	dsDesc.MipLevels = 1;
	dsDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	dsDesc.SampleDesc.Count = 1;
	dsDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	clearValue.DepthStencil.Depth = 1.0f;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	if ( FAILED(m_pDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
		&dsDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clearValue, IID_PPV_ARGS(&m_pDepthStencil))) )
		return false;

	// New resource starts in DEPTH_WRITE per CreateCommittedResource arg above.
	m_eDepthStencilState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	// Reuse existing DSV index if already allocated (e.g. on resize)
	if ( m_nDSVIndex == UINT(-1) )
		m_nDSVIndex = m_DSVHeap.Allocate();
	m_pDevice->CreateDepthStencilView( m_pDepthStencil.Get(), &dsvDesc, m_DSVHeap.GetCPUHandle(m_nDSVIndex) );

	// Create SRV for depth buffer (for SSAO post-process) — reuse existing slot
	if ( m_nDepthSRVIndex == UINT(-1) )
		m_nDepthSRVIndex = m_SRVStagingHeap.Allocate();
	if ( m_nDepthSRVIndex != UINT(-1) )
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		m_pDevice->CreateShaderResourceView( m_pDepthStencil.Get(), &srvDesc,
			m_SRVStagingHeap.GetCPUHandle( m_nDepthSRVIndex ) );
	}

	return true;
}

bool DisplayDeviceD3D12::createRootSignatures()
{
	// Root signature layout:
	// [0] CBV - Per-frame constants (b0)
	// [1] CBV - Per-object constants (b1)
	// [2] CBV - Per-material constants (b2)
	// [3] CBV - Per-light constants (b3)
	// [4] Descriptor table - SRV (t0-t7) for textures
	// [5] Descriptor table - Samplers (s0-s1)

	D3D12_ROOT_PARAMETER rootParams[6] = {};

	// 4 inline CBVs
	for ( int i = 0; i < 4; i++ )
	{
		rootParams[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParams[i].Descriptor.ShaderRegister = i;
		rootParams[i].Descriptor.RegisterSpace = 0;
		rootParams[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	}

	// SRV descriptor table
	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 8;	// t0-t7: diffuse, lightmap, bumpmap, ..., shadowMap(t7)
	srvRange.BaseShaderRegister = 0;
	srvRange.RegisterSpace = 0;
	srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[4].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[4].DescriptorTable.pDescriptorRanges = &srvRange;
	rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// Sampler descriptor table
	D3D12_DESCRIPTOR_RANGE samplerRange = {};
	samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
	samplerRange.NumDescriptors = 2;
	samplerRange.BaseShaderRegister = 0;
	samplerRange.RegisterSpace = 0;
	samplerRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParams[5].DescriptorTable.NumDescriptorRanges = 1;
	rootParams[5].DescriptorTable.pDescriptorRanges = &samplerRange;
	rootParams[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// Static comparison sampler for shadow map (register s2)
	D3D12_STATIC_SAMPLER_DESC shadowSampler = {};
	shadowSampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	shadowSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	shadowSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	shadowSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
	shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	shadowSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;	// outside shadow map = fully lit
	shadowSampler.MaxLOD = D3D12_FLOAT32_MAX;
	shadowSampler.ShaderRegister = 2;	// register(s2)
	shadowSampler.RegisterSpace = 0;
	shadowSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
	rootSigDesc.NumParameters = _countof(rootParams);
	rootSigDesc.pParameters = rootParams;
	rootSigDesc.NumStaticSamplers = 1;
	rootSigDesc.pStaticSamplers = &shadowSampler;
	rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> signature;
	ComPtr<ID3DBlob> error;
	HRESULT hr = D3D12SerializeRootSignature( &rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error );
	if ( FAILED(hr) )
	{
		if ( error )
			TRACE( (const char *)error->GetBufferPointer() );
		return false;
	}

	hr = m_pDevice->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(),
		IID_PPV_ARGS(&m_pRootSignature) );
	if ( FAILED(hr) )
		return false;

	// Post-process root signature (simpler - just a texture + sampler)
	D3D12_ROOT_PARAMETER ppParams[2] = {};

	ppParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	ppParams[0].DescriptorTable.NumDescriptorRanges = 1;
	ppParams[0].DescriptorTable.pDescriptorRanges = &srvRange;
	ppParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	ppParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	ppParams[1].DescriptorTable.NumDescriptorRanges = 1;
	ppParams[1].DescriptorTable.pDescriptorRanges = &samplerRange;
	ppParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC ppRootSigDesc = {};
	ppRootSigDesc.NumParameters = _countof(ppParams);
	ppRootSigDesc.pParameters = ppParams;
	ppRootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	hr = D3D12SerializeRootSignature( &ppRootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error );
	if ( FAILED(hr) )
		return false;

	hr = m_pDevice->CreateRootSignature( 0, signature->GetBufferPointer(), signature->GetBufferSize(),
		IID_PPV_ARGS(&m_pPostProcessRootSig) );
	if ( FAILED(hr) )
		return false;

	return true;
}

bool DisplayDeviceD3D12::createDefaultShaders()
{
	m_pDefaultShader = getShader( "Shaders/Default.hlsl" );
	m_pShadowMapShader = getShader( "Shaders/ShadowMap.hlsl" );
	m_pPassThroughShader = getShader( "Shaders/PassThrough.hlsl" );
	m_pPassThroughTLShader = getShader( "Shaders/PassThroughTL.hlsl" );
	m_pPassThroughLShader = getShader( "Shaders/PassThroughL.hlsl" );
	m_pPostProcessShader = getShader( "Shaders/PostProcess.hlsl" );

	// NOTE: bakePBRIBL() is NOT called here — it depends on the upload
	// queue which is created later in initialize() (initUploadQueue).
	// Driven from there instead.

	return true;
}

//---------------------------------------------------------------------------------------------------
// PBR IBL bake.  Generates the split-sum BRDF integration LUT (Karis 2013)
// and the prefiltered specular environment cube from the same procedural
// sky+sun model that drives computeDiffuseSH.
//
// Both textures are CPU-baked once at device init.  The env cube freezes
// the sun position at bake time; updating the cube to track the sun as
// the player moves between zones is a followup (would require either a
// GPU compute path or a background-thread re-bake on zone change).  For
// v1, "static cube" is good enough for an artist to evaluate PBR
// authoring against — sharp sun reflections in mirror metals will track
// the bake-time sun position, not the current scene sun.
//
// Costs:  BRDF LUT  = 256² R16G16_FLOAT × 1024 Hammersley samples
//         Env cube  = 256² × 6 faces × 5 mips RGBA16F, mip k uses 2^(6+k) samples
// Single-threaded CPU eval; expect ~150-300ms on a typical machine.
//---------------------------------------------------------------------------------------------------

namespace
{
	// --- Hammersley / GGX importance sampling (Karis 2013) ---
	inline float vanDerCorpus( uint32_t bits )
	{
		bits = ( bits << 16u ) | ( bits >> 16u );
		bits = ( ( bits & 0x55555555u ) << 1u ) | ( ( bits & 0xAAAAAAAAu ) >> 1u );
		bits = ( ( bits & 0x33333333u ) << 2u ) | ( ( bits & 0xCCCCCCCCu ) >> 2u );
		bits = ( ( bits & 0x0F0F0F0Fu ) << 4u ) | ( ( bits & 0xF0F0F0F0u ) >> 4u );
		bits = ( ( bits & 0x00FF00FFu ) << 8u ) | ( ( bits & 0xFF00FF00u ) >> 8u );
		return float( bits ) * 2.3283064365386963e-10f;	// / 2^32
	}

	inline void hammersley( uint32_t i, uint32_t N, float & outX, float & outY )
	{
		outX = float( i ) / float( N );
		outY = vanDerCorpus( i );
	}

	// Tangent-frame GGX importance sample for normal N (assumed +Z in
	// tangent space).  Returns the half-vector H in tangent space; caller
	// rotates to world space if needed.
	inline void importanceSampleGGX_tangent( float u, float v, float alpha,
		float & Hx, float & Hy, float & Hz )
	{
		const float TWO_PI = 6.28318530718f;
		float a2 = alpha * alpha;
		float phi = TWO_PI * u;
		float cosTheta = sqrtf( ( 1.0f - v ) / ( 1.0f + ( a2 - 1.0f ) * v ) );
		float sinTheta = sqrtf( 1.0f - cosTheta * cosTheta );
		Hx = sinTheta * cosf( phi );
		Hy = sinTheta * sinf( phi );
		Hz = cosTheta;
	}

	inline float smithG_GGX( float NdotV, float alpha )
	{
		float a2 = alpha * alpha;
		float k  = a2 * 0.5f;
		return NdotV / ( NdotV * ( 1.0f - k ) + k );
	}

	// --- Procedural environment colour for direction d (linear RGB).
	// Same shape as computeDiffuseSH's per-sample colour: sky tint + sun fill.
	// Captured by closure over the device's m_cAmbientLight + the current
	// directional sun (if any).
	struct EnvSampler
	{
		float skyR, skyG, skyB;
		float sunR, sunG, sunB;
		float sunDirX, sunDirY, sunDirZ;
		float sunFill;

		void sample( float dx, float dy, float dz, float & rOut, float & gOut, float & bOut ) const
		{
			float skyT = dy * 0.5f + 0.5f;		// 0=down, 1=up
			float skyMul = 0.3f + 0.7f * skyT;
			rOut = skyR * skyMul;
			gOut = skyG * skyMul;
			bOut = skyB * skyMul;
			// Sun fill — direction toward sun is -sunDir.
			float sunDot = dx * ( -sunDirX ) + dy * ( -sunDirY ) + dz * ( -sunDirZ );
			if ( sunDot > 0.0f )
			{
				float w = sunDot * sunDot * sunFill;
				rOut += sunR * w;
				gOut += sunG * w;
				bOut += sunB * w;
			}
		}
	};

	// Direction generation for cube face (x, y in [-1,1]).  +X / -X / +Y / -Y / +Z / -Z.
	inline void faceDirection( int face, float u, float v, float & dx, float & dy, float & dz )
	{
		switch ( face )
		{
		case 0: dx =  1.0f; dy = -v;   dz = -u;   break;	// +X
		case 1: dx = -1.0f; dy = -v;   dz =  u;   break;	// -X
		case 2: dx =  u;    dy =  1.0f; dz =  v;  break;	// +Y
		case 3: dx =  u;    dy = -1.0f; dz = -v;  break;	// -Y
		case 4: dx =  u;    dy = -v;   dz =  1.0f; break;	// +Z
		default:dx = -u;    dy = -v;   dz = -1.0f; break;	// -Z
		}
		float invLen = 1.0f / sqrtf( dx * dx + dy * dy + dz * dz );
		dx *= invLen; dy *= invLen; dz *= invLen;
	}
}

bool DisplayDeviceD3D12::bakePBRIBL()
{
	m_bPBRIBLReady = false;

	if ( !m_pDevice || !m_pUploadCommandList || !m_pUploadAllocator )
	{
		TRACE( "bakePBRIBL: device or upload command list not ready" );
		return false;
	}

	using DirectX::PackedVector::XMConvertFloatToHalf;

	const UINT LUT_SIZE = 256;
	const UINT CUBE_FACE_SIZE = 256;
	const UINT CUBE_MIP_COUNT = 5;
	const UINT BRDF_LUT_SAMPLES = 1024;

	//-------------------------------------------------------------------------
	// 1) CPU-bake BRDF LUT (Karis split-sum).  Output = R16G16_FLOAT, X = NdotV,
	//    Y = roughness.  Each pixel is (A, B) where the final IBL response is
	//    F0 * A + B.
	//-------------------------------------------------------------------------
	std::vector< uint16_t > brdfData( LUT_SIZE * LUT_SIZE * 2 );
	for ( UINT py = 0; py < LUT_SIZE; ++py )
	{
		float roughness = (float)( py + 0.5f ) / (float)LUT_SIZE;
		float alpha = roughness * roughness;
		for ( UINT px = 0; px < LUT_SIZE; ++px )
		{
			float NdotV = (float)( px + 0.5f ) / (float)LUT_SIZE;
			NdotV = Max( NdotV, 1e-3f );

			// View vector in tangent space (Z-up)
			float Vx = sqrtf( 1.0f - NdotV * NdotV );
			float Vy = 0.0f;
			float Vz = NdotV;

			float A = 0.0f, B = 0.0f;
			for ( UINT i = 0; i < BRDF_LUT_SAMPLES; ++i )
			{
				float u, v;
				hammersley( i, BRDF_LUT_SAMPLES, u, v );

				float Hx, Hy, Hz;
				importanceSampleGGX_tangent( u, v, alpha, Hx, Hy, Hz );

				float VdotH = Vx * Hx + Vy * Hy + Vz * Hz;
				// L = 2(V·H)H - V
				float Lx = 2.0f * VdotH * Hx - Vx;
				float Ly = 2.0f * VdotH * Hy - Vy;
				float Lz = 2.0f * VdotH * Hz - Vz;

				float NdotL = Max( Lz, 0.0f );
				float NdotH = Max( Hz, 0.0f );
				float VdotHc = Max( VdotH, 0.0f );

				if ( NdotL > 0.0f )
				{
					float G = smithG_GGX( NdotV, alpha ) * smithG_GGX( NdotL, alpha );
					float Gvis = G * VdotHc / Max( NdotH * NdotV, 1e-6f );
					float Fc = powf( 1.0f - VdotHc, 5.0f );
					A += ( 1.0f - Fc ) * Gvis;
					B += Fc * Gvis;
				}
			}
			A /= (float)BRDF_LUT_SAMPLES;
			B /= (float)BRDF_LUT_SAMPLES;

			UINT idx = ( py * LUT_SIZE + px ) * 2;
			brdfData[ idx + 0 ] = XMConvertFloatToHalf( A );
			brdfData[ idx + 1 ] = XMConvertFloatToHalf( B );
		}
	}

	//-------------------------------------------------------------------------
	// 2) CPU-bake prefiltered env cube.  Sun direction frozen at bake time —
	//    grab the first directional light if present, else +Y-down default.
	//-------------------------------------------------------------------------
	EnvSampler env = {};
	{
		float inv = 1.0f / 255.0f;
		env.skyR = srgbToLinear( m_cAmbientLight.m_R * inv );
		env.skyG = srgbToLinear( m_cAmbientLight.m_G * inv );
		env.skyB = srgbToLinear( m_cAmbientLight.m_B * inv );

		env.sunDirX = 0.0f; env.sunDirY = -1.0f; env.sunDirZ = 0.0f;
		env.sunR = 0.0f; env.sunG = 0.0f; env.sunB = 0.0f;
		for ( auto it = m_Lights.begin(); it != m_Lights.end(); ++it )
		{
			const LightInfo & l = it->second;
			if ( l.type == 3 /*directional*/ )
			{
				env.sunDirX = l.dirX; env.sunDirY = l.dirY; env.sunDirZ = l.dirZ;
				env.sunR = srgbToLinear( l.r );
				env.sunG = srgbToLinear( l.g );
				env.sunB = srgbToLinear( l.b );
				break;
			}
		}
		env.sunFill = 1.0f;	// stronger here than diffuse SH so sun shows up in spec
	}

	// Per-face, per-mip CPU data.  Tightly packed R G B A (half) per pixel.
	struct CubeMipData
	{
		UINT size;
		std::vector< uint16_t > pixels;	// size*size*4 halves
	};
	struct CubeFaceData
	{
		CubeMipData mips[ CUBE_MIP_COUNT ];
	};
	CubeFaceData faces[ 6 ];

	for ( int face = 0; face < 6; ++face )
	{
		for ( UINT mip = 0; mip < CUBE_MIP_COUNT; ++mip )
		{
			UINT mipSize = CUBE_FACE_SIZE >> mip;
			if ( mipSize < 1 ) mipSize = 1;
			faces[ face ].mips[ mip ].size = mipSize;
			faces[ face ].mips[ mip ].pixels.resize( (size_t)mipSize * mipSize * 4 );

			float roughness = (float)mip / (float)( CUBE_MIP_COUNT - 1 );
			float alpha = roughness * roughness;

			// Sample count grows with mip — mirror level needs few, rough levels more
			UINT sampleCount;
			if ( mip == 0 )       sampleCount = 1;
			else if ( mip == 1 )  sampleCount = 32;
			else if ( mip == 2 )  sampleCount = 64;
			else if ( mip == 3 )  sampleCount = 128;
			else                  sampleCount = 256;

			for ( UINT py = 0; py < mipSize; ++py )
			{
				for ( UINT px = 0; px < mipSize; ++px )
				{
					float u = ( ( (float)px + 0.5f ) / (float)mipSize ) * 2.0f - 1.0f;
					float v = ( ( (float)py + 0.5f ) / (float)mipSize ) * 2.0f - 1.0f;

					float Nx, Ny, Nz;
					faceDirection( face, u, v, Nx, Ny, Nz );

					float colR = 0.0f, colG = 0.0f, colB = 0.0f;

					if ( mip == 0 )
					{
						// Mirror level: just sample the environment in direction N.
						env.sample( Nx, Ny, Nz, colR, colG, colB );
					}
					else
					{
						// Build a tangent basis around N to rotate tangent-space
						// half-vectors into world space.
						float upX = ( fabsf( Nz ) < 0.999f ) ? 0.0f : 1.0f;
						float upY = ( fabsf( Nz ) < 0.999f ) ? 0.0f : 0.0f;
						float upZ = ( fabsf( Nz ) < 0.999f ) ? 1.0f : 0.0f;
						// tangent T = normalize(cross(up, N))
						float Tx = upY * Nz - upZ * Ny;
						float Ty = upZ * Nx - upX * Nz;
						float Tz = upX * Ny - upY * Nx;
						float tLen = 1.0f / sqrtf( Tx * Tx + Ty * Ty + Tz * Tz );
						Tx *= tLen; Ty *= tLen; Tz *= tLen;
						// bitangent B = cross(N, T)
						float Bx = Ny * Tz - Nz * Ty;
						float By = Nz * Tx - Nx * Tz;
						float Bz = Nx * Ty - Ny * Tx;

						float totalWeight = 0.0f;
						for ( UINT s = 0; s < sampleCount; ++s )
						{
							float xi_u, xi_v;
							hammersley( s, sampleCount, xi_u, xi_v );

							float Hx_t, Hy_t, Hz_t;
							importanceSampleGGX_tangent( xi_u, xi_v, alpha, Hx_t, Hy_t, Hz_t );

							// Rotate H into world space using TBN basis
							float Hx = Hx_t * Tx + Hy_t * Bx + Hz_t * Nx;
							float Hy = Hx_t * Ty + Hy_t * By + Hz_t * Ny;
							float Hz = Hx_t * Tz + Hy_t * Bz + Hz_t * Nz;

							// Assume V = N: L = 2(N·H)H - N
							float NdotH = Nx * Hx + Ny * Hy + Nz * Hz;
							float Lx = 2.0f * NdotH * Hx - Nx;
							float Ly = 2.0f * NdotH * Hy - Ny;
							float Lz = 2.0f * NdotH * Hz - Nz;

							float NdotL = Nx * Lx + Ny * Ly + Nz * Lz;
							if ( NdotL > 0.0f )
							{
								float r, g, b;
								env.sample( Lx, Ly, Lz, r, g, b );
								colR += r * NdotL;
								colG += g * NdotL;
								colB += b * NdotL;
								totalWeight += NdotL;
							}
						}
						if ( totalWeight > 0.0f )
						{
							colR /= totalWeight;
							colG /= totalWeight;
							colB /= totalWeight;
						}
					}

					UINT idx = ( py * mipSize + px ) * 4;
					faces[ face ].mips[ mip ].pixels[ idx + 0 ] = XMConvertFloatToHalf( colR );
					faces[ face ].mips[ mip ].pixels[ idx + 1 ] = XMConvertFloatToHalf( colG );
					faces[ face ].mips[ mip ].pixels[ idx + 2 ] = XMConvertFloatToHalf( colB );
					faces[ face ].mips[ mip ].pixels[ idx + 3 ] = XMConvertFloatToHalf( 1.0f );
				}
			}
		}
	}

	//-------------------------------------------------------------------------
	// 3) Create the GPU resources and upload.  Pattern mirrors
	//    immediateTextureUpload but bundled into one CL submission for
	//    BRDF LUT + 6 cube faces × 5 mips = 31 subresources.
	//-------------------------------------------------------------------------

	D3D12_HEAP_PROPERTIES defaultHeap = {};
	defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_HEAP_PROPERTIES uploadHeap = {};
	uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

	// LUT texture.
	D3D12_RESOURCE_DESC lutDesc = {};
	lutDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	lutDesc.Width = LUT_SIZE;
	lutDesc.Height = LUT_SIZE;
	lutDesc.DepthOrArraySize = 1;
	lutDesc.MipLevels = 1;
	lutDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
	lutDesc.SampleDesc.Count = 1;
	if ( FAILED( m_pDevice->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE,
		&lutDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_pBRDFLUT) ) ) )
	{
		TRACE( "bakePBRIBL: failed to create BRDF LUT texture" );
		return false;
	}

	// Cube texture (6 array slices, mip chain).
	D3D12_RESOURCE_DESC cubeDesc = {};
	cubeDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	cubeDesc.Width = CUBE_FACE_SIZE;
	cubeDesc.Height = CUBE_FACE_SIZE;
	cubeDesc.DepthOrArraySize = 6;
	cubeDesc.MipLevels = (UINT16)CUBE_MIP_COUNT;
	cubeDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	cubeDesc.SampleDesc.Count = 1;
	if ( FAILED( m_pDevice->CreateCommittedResource( &defaultHeap, D3D12_HEAP_FLAG_NONE,
		&cubeDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_pEnvCube) ) ) )
	{
		TRACE( "bakePBRIBL: failed to create env cube texture" );
		m_pBRDFLUT.Reset();
		return false;
	}

	// Calculate total upload buffer size — sum of all subresource footprints,
	// each aligned up to D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT.
	UINT64 totalUpload = 0;
	{
		UINT64 sz = 0;
		m_pDevice->GetCopyableFootprints( &lutDesc, 0, 1, 0, nullptr, nullptr, nullptr, &sz );
		totalUpload += ( sz + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1 )
			& ~(UINT64)( D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1 );

		for ( UINT face = 0; face < 6; ++face )
		{
			for ( UINT mip = 0; mip < CUBE_MIP_COUNT; ++mip )
			{
				UINT sub = face * CUBE_MIP_COUNT + mip;
				m_pDevice->GetCopyableFootprints( &cubeDesc, sub, 1, 0, nullptr, nullptr, nullptr, &sz );
				totalUpload += ( sz + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1 )
					& ~(UINT64)( D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1 );
			}
		}
	}

	D3D12_RESOURCE_DESC bufDesc = {};
	bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufDesc.Width = totalUpload;
	bufDesc.Height = 1;
	bufDesc.DepthOrArraySize = 1;
	bufDesc.MipLevels = 1;
	bufDesc.SampleDesc.Count = 1;
	bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ComPtr<ID3D12Resource> uploadBuf;
	if ( FAILED( m_pDevice->CreateCommittedResource( &uploadHeap, D3D12_HEAP_FLAG_NONE,
		&bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf) ) ) )
	{
		TRACE( "bakePBRIBL: failed to create upload buffer" );
		m_pBRDFLUT.Reset();
		m_pEnvCube.Reset();
		return false;
	}

	byte * pUploadBase = nullptr;
	uploadBuf->Map( 0, nullptr, (void **)&pUploadBase );

	AutoLock uploadLock( &m_UploadCS );
	if ( m_pUploadFence->GetCompletedValue() < m_nUploadFenceValue )
	{
		m_pUploadFence->SetEventOnCompletion( m_nUploadFenceValue, m_hUploadFenceEvent );
		WaitForSingleObject( m_hUploadFenceEvent, 10000 );
	}
	m_pUploadAllocator->Reset();
	m_pUploadCommandList->Reset( m_pUploadAllocator.Get(), nullptr );

	UINT64 offset = 0;

	// Helper: place a subresource's pixel data into the upload buffer
	// (respecting row pitch padding) and emit a CopyTextureRegion command.
	auto placeAndCopy = [ & ](
		ID3D12Resource * pTexture,
		const D3D12_RESOURCE_DESC & texDesc,
		UINT subresourceIndex,
		const byte * pSrcData,
		UINT srcRowPitch ) -> bool
	{
		offset = ( offset + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1 )
			& ~(UINT64)( D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1 );

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
		UINT numRows = 0;
		UINT64 rowSizeInBytes = 0;
		UINT64 subSize = 0;
		m_pDevice->GetCopyableFootprints( &texDesc, subresourceIndex, 1, offset,
			&footprint, &numRows, &rowSizeInBytes, &subSize );

		byte * pDst = pUploadBase + footprint.Offset;
		for ( UINT row = 0; row < numRows; ++row )
		{
			UINT copyBytes = (UINT)rowSizeInBytes < srcRowPitch ? (UINT)rowSizeInBytes : srcRowPitch;
			memcpy( pDst + row * footprint.Footprint.RowPitch,
				pSrcData + row * srcRowPitch, copyBytes );
		}

		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource = pTexture;
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = subresourceIndex;

		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = uploadBuf.Get();
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint = footprint;

		m_pUploadCommandList->CopyTextureRegion( &dst, 0, 0, 0, &src, nullptr );

		offset = footprint.Offset + subSize;
		return true;
	};

	// LUT
	placeAndCopy( m_pBRDFLUT.Get(), lutDesc, 0,
		reinterpret_cast<const byte *>( brdfData.data() ),
		LUT_SIZE * 2 * sizeof( uint16_t ) );

	// Env cube — D3D12 array layout: subresource = mip + slice * mipCount
	// (Mips contiguous within an array slice).
	for ( UINT face = 0; face < 6; ++face )
	{
		for ( UINT mip = 0; mip < CUBE_MIP_COUNT; ++mip )
		{
			UINT mipSize = faces[ face ].mips[ mip ].size;
			UINT subresourceIndex = mip + face * CUBE_MIP_COUNT;
			placeAndCopy( m_pEnvCube.Get(), cubeDesc, subresourceIndex,
				reinterpret_cast<const byte *>( faces[ face ].mips[ mip ].pixels.data() ),
				mipSize * 4 * sizeof( uint16_t ) );
		}
	}

	uploadBuf->Unmap( 0, nullptr );

	// COPY queues can't issue barriers; resources decay to COMMON, which
	// promotes to PSR on first sample.  Same pattern as immediateTextureUpload.
	m_pUploadCommandList->Close();
	ID3D12CommandList * ppLists[] = { m_pUploadCommandList.Get() };
	m_pUploadQueue->ExecuteCommandLists( 1, ppLists );

	m_nUploadFenceValue++;
	m_pUploadQueue->Signal( m_pUploadFence.Get(), m_nUploadFenceValue );
	if ( m_pUploadFence->GetCompletedValue() < m_nUploadFenceValue )
	{
		m_pUploadFence->SetEventOnCompletion( m_nUploadFenceValue, m_hUploadFenceEvent );
		WaitForSingleObject( m_hUploadFenceEvent, 10000 );
	}

	//-------------------------------------------------------------------------
	// 4) Create SRVs in the staging heap.
	//-------------------------------------------------------------------------
	m_nBRDFLUTSRVStagingIndex = m_SRVStagingHeap.Allocate();
	m_nEnvCubeSRVStagingIndex = m_SRVStagingHeap.Allocate();
	if ( m_nBRDFLUTSRVStagingIndex == UINT(-1) || m_nEnvCubeSRVStagingIndex == UINT(-1) )
	{
		TRACE( "bakePBRIBL: failed to allocate SRV staging slots" );
		if ( m_nBRDFLUTSRVStagingIndex != UINT(-1) ) m_SRVStagingHeap.Free( m_nBRDFLUTSRVStagingIndex );
		if ( m_nEnvCubeSRVStagingIndex != UINT(-1) ) m_SRVStagingHeap.Free( m_nEnvCubeSRVStagingIndex );
		m_nBRDFLUTSRVStagingIndex = UINT(-1);
		m_nEnvCubeSRVStagingIndex = UINT(-1);
		m_pBRDFLUT.Reset();
		m_pEnvCube.Reset();
		return false;
	}

	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		m_pDevice->CreateShaderResourceView( m_pBRDFLUT.Get(), &srvDesc,
			m_SRVStagingHeap.GetCPUHandle( m_nBRDFLUTSRVStagingIndex ) );
	}
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.TextureCube.MipLevels = CUBE_MIP_COUNT;
		m_pDevice->CreateShaderResourceView( m_pEnvCube.Get(), &srvDesc,
			m_SRVStagingHeap.GetCPUHandle( m_nEnvCubeSRVStagingIndex ) );
	}

	// Remember what we baked so maybeRebakeEnvCube can detect drift later.
	m_vLastBakeSunDir[0] = env.sunDirX;
	m_vLastBakeSunDir[1] = env.sunDirY;
	m_vLastBakeSunDir[2] = env.sunDirZ;
	m_vLastBakeSunRGB[0] = env.sunR;
	m_vLastBakeSunRGB[1] = env.sunG;
	m_vLastBakeSunRGB[2] = env.sunB;
	m_vLastBakeSkyRGB[0] = env.skyR;
	m_vLastBakeSkyRGB[1] = env.skyG;
	m_vLastBakeSkyRGB[2] = env.skyB;
	m_nLastBakeTick = GetTickCount();

	m_bPBRIBLReady = true;
	TRACE( "bakePBRIBL: baked %ux%u BRDF LUT + %ux%ux6 RGBA16F env cube (%u mips)",
		LUT_SIZE, LUT_SIZE, CUBE_FACE_SIZE, CUBE_FACE_SIZE, CUBE_MIP_COUNT );
	return true;
}

//---------------------------------------------------------------------------------------------------
// Re-bake just the prefiltered env cube (LUT is environment-independent
// and never re-bakes).  Triggered from maybeRebakeEnvCube() when the sun
// direction or sky colour has drifted past threshold since the last bake
// — typically on zone changes, but also covers time-of-day / nebula-tint
// shifts within a zone.
//
// Reuses the existing m_pEnvCube resource: bakes new CPU data, re-uploads
// to the same texture, waits for GPU idle before the upload to avoid
// overwriting pixel data that an in-flight frame is sampling.  No SRV
// descriptor churn — slot t6 in any material's slab still points at the
// same resource, the resource's pixel contents just changed.
//
// Cost: ~150-300ms CPU (single-threaded sample loop) + GPU upload.  Runs
// synchronously from beginScene — visible as a one-frame hitch on the
// zone-change frame.  Backgrounding the CPU bake on a worker is the right
// followup if the hitch is noticeable in practice.
//---------------------------------------------------------------------------------------------------
bool DisplayDeviceD3D12::rebakeEnvCubeOnly()
{
	if ( !m_bPBRIBLReady || !m_pEnvCube || !m_pUploadCommandList || !m_pUploadAllocator )
		return false;

	using DirectX::PackedVector::XMConvertFloatToHalf;

	const UINT CUBE_FACE_SIZE = 256;
	const UINT CUBE_MIP_COUNT = 5;

	// Snapshot the live environment from the current m_Lights + m_cAmbientLight.
	EnvSampler env = {};
	{
		float inv = 1.0f / 255.0f;
		env.skyR = srgbToLinear( m_cAmbientLight.m_R * inv );
		env.skyG = srgbToLinear( m_cAmbientLight.m_G * inv );
		env.skyB = srgbToLinear( m_cAmbientLight.m_B * inv );

		env.sunDirX = 0.0f; env.sunDirY = -1.0f; env.sunDirZ = 0.0f;
		env.sunR = 0.0f; env.sunG = 0.0f; env.sunB = 0.0f;
		for ( auto it = m_Lights.begin(); it != m_Lights.end(); ++it )
		{
			const LightInfo & l = it->second;
			if ( l.type == 3 /*directional*/ )
			{
				env.sunDirX = l.dirX; env.sunDirY = l.dirY; env.sunDirZ = l.dirZ;
				env.sunR = srgbToLinear( l.r );
				env.sunG = srgbToLinear( l.g );
				env.sunB = srgbToLinear( l.b );
				break;
			}
		}
		env.sunFill = 1.0f;
	}

	// CPU bake the cube.  Same loop as in bakePBRIBL.
	struct CubeMipData { UINT size; std::vector< uint16_t > pixels; };
	struct CubeFaceData { CubeMipData mips[5]; };
	CubeFaceData faces[6];
	for ( int face = 0; face < 6; ++face )
	{
		for ( UINT mip = 0; mip < CUBE_MIP_COUNT; ++mip )
		{
			UINT mipSize = CUBE_FACE_SIZE >> mip;
			if ( mipSize < 1 ) mipSize = 1;
			faces[ face ].mips[ mip ].size = mipSize;
			faces[ face ].mips[ mip ].pixels.resize( (size_t)mipSize * mipSize * 4 );

			float roughness = (float)mip / (float)( CUBE_MIP_COUNT - 1 );
			float alpha = roughness * roughness;
			UINT sampleCount = ( mip == 0 ) ? 1u : ( mip == 1 ? 32u : ( mip == 2 ? 64u : ( mip == 3 ? 128u : 256u ) ) );

			for ( UINT py = 0; py < mipSize; ++py )
			{
				for ( UINT px = 0; px < mipSize; ++px )
				{
					float u = ( ( (float)px + 0.5f ) / (float)mipSize ) * 2.0f - 1.0f;
					float v = ( ( (float)py + 0.5f ) / (float)mipSize ) * 2.0f - 1.0f;

					float Nx, Ny, Nz;
					faceDirection( face, u, v, Nx, Ny, Nz );

					float colR = 0.0f, colG = 0.0f, colB = 0.0f;
					if ( mip == 0 )
					{
						env.sample( Nx, Ny, Nz, colR, colG, colB );
					}
					else
					{
						float upX = ( fabsf( Nz ) < 0.999f ) ? 0.0f : 1.0f;
						float upY = 0.0f;
						float upZ = ( fabsf( Nz ) < 0.999f ) ? 1.0f : 0.0f;
						float Tx = upY * Nz - upZ * Ny;
						float Ty = upZ * Nx - upX * Nz;
						float Tz = upX * Ny - upY * Nx;
						float tLen = 1.0f / sqrtf( Tx * Tx + Ty * Ty + Tz * Tz );
						Tx *= tLen; Ty *= tLen; Tz *= tLen;
						float Bx = Ny * Tz - Nz * Ty;
						float By = Nz * Tx - Nx * Tz;
						float Bz = Nx * Ty - Ny * Tx;

						float totalWeight = 0.0f;
						for ( UINT s = 0; s < sampleCount; ++s )
						{
							float xi_u, xi_v;
							hammersley( s, sampleCount, xi_u, xi_v );

							float Hx_t, Hy_t, Hz_t;
							importanceSampleGGX_tangent( xi_u, xi_v, alpha, Hx_t, Hy_t, Hz_t );

							float Hx = Hx_t * Tx + Hy_t * Bx + Hz_t * Nx;
							float Hy = Hx_t * Ty + Hy_t * By + Hz_t * Ny;
							float Hz = Hx_t * Tz + Hy_t * Bz + Hz_t * Nz;

							float NdotH = Nx * Hx + Ny * Hy + Nz * Hz;
							float Lx = 2.0f * NdotH * Hx - Nx;
							float Ly = 2.0f * NdotH * Hy - Ny;
							float Lz = 2.0f * NdotH * Hz - Nz;

							float NdotL = Nx * Lx + Ny * Ly + Nz * Lz;
							if ( NdotL > 0.0f )
							{
								float r, g, b;
								env.sample( Lx, Ly, Lz, r, g, b );
								colR += r * NdotL;
								colG += g * NdotL;
								colB += b * NdotL;
								totalWeight += NdotL;
							}
						}
						if ( totalWeight > 0.0f )
						{
							colR /= totalWeight;
							colG /= totalWeight;
							colB /= totalWeight;
						}
					}

					UINT idx = ( py * mipSize + px ) * 4;
					faces[ face ].mips[ mip ].pixels[ idx + 0 ] = XMConvertFloatToHalf( colR );
					faces[ face ].mips[ mip ].pixels[ idx + 1 ] = XMConvertFloatToHalf( colG );
					faces[ face ].mips[ mip ].pixels[ idx + 2 ] = XMConvertFloatToHalf( colB );
					faces[ face ].mips[ mip ].pixels[ idx + 3 ] = XMConvertFloatToHalf( 1.0f );
				}
			}
		}
	}

	// Wait for any in-flight render frame to finish using the existing
	// cube before we overwrite its pixel data.  Without this the GPU may
	// be mid-sample on the old cube when the COPY queue writes the new
	// one — visible as torn reflections for one frame.
	waitForGPU();

	// Build an upload buffer sized for all 30 subresources (6 faces × 5 mips).
	D3D12_RESOURCE_DESC cubeDesc = m_pEnvCube->GetDesc();

	UINT64 totalUpload = 0;
	for ( UINT face = 0; face < 6; ++face )
	{
		for ( UINT mip = 0; mip < CUBE_MIP_COUNT; ++mip )
		{
			UINT sub = mip + face * CUBE_MIP_COUNT;
			UINT64 sz = 0;
			m_pDevice->GetCopyableFootprints( &cubeDesc, sub, 1, 0, nullptr, nullptr, nullptr, &sz );
			totalUpload += ( sz + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1 )
				& ~(UINT64)( D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1 );
		}
	}

	D3D12_HEAP_PROPERTIES uploadHeap = {};
	uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC bufDesc = {};
	bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufDesc.Width = totalUpload;
	bufDesc.Height = 1;
	bufDesc.DepthOrArraySize = 1;
	bufDesc.MipLevels = 1;
	bufDesc.SampleDesc.Count = 1;
	bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ComPtr<ID3D12Resource> uploadBuf;
	if ( FAILED( m_pDevice->CreateCommittedResource( &uploadHeap, D3D12_HEAP_FLAG_NONE,
		&bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuf) ) ) )
	{
		TRACE( "rebakeEnvCubeOnly: failed to create upload buffer" );
		return false;
	}

	byte * pUploadBase = nullptr;
	uploadBuf->Map( 0, nullptr, (void **)&pUploadBase );

	AutoLock uploadLock( &m_UploadCS );
	if ( m_pUploadFence->GetCompletedValue() < m_nUploadFenceValue )
	{
		m_pUploadFence->SetEventOnCompletion( m_nUploadFenceValue, m_hUploadFenceEvent );
		WaitForSingleObject( m_hUploadFenceEvent, 10000 );
	}
	m_pUploadAllocator->Reset();
	m_pUploadCommandList->Reset( m_pUploadAllocator.Get(), nullptr );

	UINT64 offset = 0;
	for ( UINT face = 0; face < 6; ++face )
	{
		for ( UINT mip = 0; mip < CUBE_MIP_COUNT; ++mip )
		{
			UINT mipSize = faces[ face ].mips[ mip ].size;
			UINT sub = mip + face * CUBE_MIP_COUNT;

			offset = ( offset + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1 )
				& ~(UINT64)( D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1 );

			D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
			UINT numRows = 0;
			UINT64 rowSizeInBytes = 0;
			UINT64 subSize = 0;
			m_pDevice->GetCopyableFootprints( &cubeDesc, sub, 1, offset,
				&footprint, &numRows, &rowSizeInBytes, &subSize );

			const byte * pSrcData = reinterpret_cast<const byte *>( faces[ face ].mips[ mip ].pixels.data() );
			UINT srcRowPitch = mipSize * 4 * sizeof( uint16_t );
			byte * pDst = pUploadBase + footprint.Offset;
			for ( UINT row = 0; row < numRows; ++row )
			{
				UINT copyBytes = (UINT)rowSizeInBytes < srcRowPitch ? (UINT)rowSizeInBytes : srcRowPitch;
				memcpy( pDst + row * footprint.Footprint.RowPitch,
					pSrcData + row * srcRowPitch, copyBytes );
			}

			D3D12_TEXTURE_COPY_LOCATION dst = {};
			dst.pResource = m_pEnvCube.Get();
			dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dst.SubresourceIndex = sub;

			D3D12_TEXTURE_COPY_LOCATION src = {};
			src.pResource = uploadBuf.Get();
			src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			src.PlacedFootprint = footprint;

			m_pUploadCommandList->CopyTextureRegion( &dst, 0, 0, 0, &src, nullptr );

			offset = footprint.Offset + subSize;
		}
	}

	uploadBuf->Unmap( 0, nullptr );

	m_pUploadCommandList->Close();
	ID3D12CommandList * ppLists[] = { m_pUploadCommandList.Get() };
	m_pUploadQueue->ExecuteCommandLists( 1, ppLists );

	m_nUploadFenceValue++;
	m_pUploadQueue->Signal( m_pUploadFence.Get(), m_nUploadFenceValue );
	if ( m_pUploadFence->GetCompletedValue() < m_nUploadFenceValue )
	{
		m_pUploadFence->SetEventOnCompletion( m_nUploadFenceValue, m_hUploadFenceEvent );
		WaitForSingleObject( m_hUploadFenceEvent, 10000 );
	}

	// Update bake state — drift comparisons key off these.
	m_vLastBakeSunDir[0] = env.sunDirX;
	m_vLastBakeSunDir[1] = env.sunDirY;
	m_vLastBakeSunDir[2] = env.sunDirZ;
	m_vLastBakeSunRGB[0] = env.sunR;
	m_vLastBakeSunRGB[1] = env.sunG;
	m_vLastBakeSunRGB[2] = env.sunB;
	m_vLastBakeSkyRGB[0] = env.skyR;
	m_vLastBakeSkyRGB[1] = env.skyG;
	m_vLastBakeSkyRGB[2] = env.skyB;
	m_nLastBakeTick = GetTickCount();

	return true;
}

//---------------------------------------------------------------------------------------------------
// Called from beginScene right after computeDiffuseSH.  Decides whether
// the env cube has drifted enough from the last bake to warrant a re-bake,
// and throttles re-bake frequency so transitions don't trigger one per
// frame.
//
// Drift threshold: sun direction dot product < 0.95 (~18°) OR sun colour /
// sky colour delta > 0.1 per channel.  Both are conservative — small
// drifts produce indistinguishable env cubes and aren't worth the hitch.
// Throttle: at least 2 seconds between rebakes.
//---------------------------------------------------------------------------------------------------
void DisplayDeviceD3D12::maybeRebakeEnvCube()
{
	if ( !m_bPBRIBLReady )
		return;
	if ( GetTickCount() - m_nLastBakeTick < 2000 )
		return;     // throttle

	// Compute current sun + sky.  Same procedural-env interpretation as
	// bakePBRIBL / rebakeEnvCubeOnly — keep these three in sync.
	float curSunDirX = 0.0f, curSunDirY = -1.0f, curSunDirZ = 0.0f;
	float curSunR = 0.0f, curSunG = 0.0f, curSunB = 0.0f;
	for ( auto it = m_Lights.begin(); it != m_Lights.end(); ++it )
	{
		const LightInfo & l = it->second;
		if ( l.type == 3 /*directional*/ )
		{
			curSunDirX = l.dirX; curSunDirY = l.dirY; curSunDirZ = l.dirZ;
			curSunR = srgbToLinear( l.r ); curSunG = srgbToLinear( l.g ); curSunB = srgbToLinear( l.b );
			break;
		}
	}
	const float inv = 1.0f / 255.0f;
	float curSkyR = srgbToLinear( m_cAmbientLight.m_R * inv );
	float curSkyG = srgbToLinear( m_cAmbientLight.m_G * inv );
	float curSkyB = srgbToLinear( m_cAmbientLight.m_B * inv );

	// Direction drift: dot product of unit vectors.  <0.95 = ~18° apart.
	float sunDot = curSunDirX * m_vLastBakeSunDir[0]
				 + curSunDirY * m_vLastBakeSunDir[1]
				 + curSunDirZ * m_vLastBakeSunDir[2];

	// Colour drift: max-channel absolute delta.
	float sunDelta = Max( Max( fabsf( curSunR - m_vLastBakeSunRGB[0] ),
								fabsf( curSunG - m_vLastBakeSunRGB[1] ) ),
								fabsf( curSunB - m_vLastBakeSunRGB[2] ) );
	float skyDelta = Max( Max( fabsf( curSkyR - m_vLastBakeSkyRGB[0] ),
								fabsf( curSkyG - m_vLastBakeSkyRGB[1] ) ),
								fabsf( curSkyB - m_vLastBakeSkyRGB[2] ) );

	if ( sunDot >= 0.95f && sunDelta < 0.1f && skyDelta < 0.1f )
		return;     // close enough — skip rebake

	TRACE( "maybeRebakeEnvCube: env drift detected (sunDot=%.3f sunDelta=%.3f skyDelta=%.3f) — rebaking",
		sunDot, sunDelta, skyDelta );
	rebakeEnvCubeOnly();
}

void DisplayDeviceD3D12::freeD3D12()
{
	waitForGPU();

	// Release effects.  Two-step: onDeviceShutdown() returns RTV/SRV slots
	// back to our heaps while they're still alive (preventing the slot leak
	// across scene reloads when effects are re-instantiated; also defending
	// against effects that outlive the device — their destructors would
	// otherwise Free() into freed memory).  Then release() drops the
	// effect's D3D resources.
	for ( WeakEffectList::iterator iEffect = m_CreatedEffects.begin();
		iEffect != m_CreatedEffects.end(); ++iEffect )
	{
		DisplayEffect * pEffect = *iEffect;
		if ( pEffect != NULL )
		{
			pEffect->onDeviceShutdown();
			pEffect->release();
		}
	}
	m_CreatedEffects.clear();

	// Return device-owned SRV staging slots to the heap.  In practice
	// m_SRVStagingHeap dies with the device member object so a slot leak here is
	// harmless today, but freeing keeps the heap invariant intact in case the
	// device is ever re-initialized without process restart.
	m_SRVStagingHeap.Free( m_nSceneSRVIndex );             m_nSceneSRVIndex             = UINT(-1);
	m_SRVStagingHeap.Free( m_nSMAAEdgeSRVIndex );          m_nSMAAEdgeSRVIndex          = UINT(-1);
	m_SRVStagingHeap.Free( m_nSMAAWeightsSRVIndex );       m_nSMAAWeightsSRVIndex       = UINT(-1);
	m_SRVStagingHeap.Free( m_nDefaultExposureSRVIndex );   m_nDefaultExposureSRVIndex   = UINT(-1);
	m_SRVStagingHeap.Free( m_nDepthSRVIndex );             m_nDepthSRVIndex             = UINT(-1);
	m_SRVStagingHeap.Free( m_nShadowMapSRVStagingIndex );  m_nShadowMapSRVStagingIndex  = UINT(-1);
	m_SRVStagingHeap.Free( m_nBRDFLUTSRVStagingIndex );    m_nBRDFLUTSRVStagingIndex    = UINT(-1);
	m_SRVStagingHeap.Free( m_nEnvCubeSRVStagingIndex );    m_nEnvCubeSRVStagingIndex    = UINT(-1);
	m_pBRDFLUT.Reset();
	m_pEnvCube.Reset();
	m_bPBRIBLReady = false;

	releaseShaders();

	m_ShadowPassList.clear();
	m_bShadowMapReady = false;
	m_pShadowMapDepth.Reset();

	// Persist the in-memory pipeline library to disk before tearing it down,
	// so the next cold launch on the same GPU/driver gets the cache benefit.
	savePSOLibrary();
	m_pPSOLibrary.Reset();
	m_PSOCacheBlob.clear();
	m_PSOCache.clear();

	// Upload queue cleanup
	m_pUploadCommandList.Reset();
	m_pUploadAllocator.Reset();
	m_pUploadQueue.Reset();
	m_pUploadFence.Reset();
	if ( m_hUploadFenceEvent )
	{
		CloseHandle( m_hUploadFenceEvent );
		m_hUploadFenceEvent = NULL;
	}

	// FXAA cleanup
	m_pSceneRT.Reset();
	m_pFXAAPSO.Reset();
	m_pFXAARootSig.Reset();
	m_pFXAAShader = NULL;

	// Release per-worker D3D12 contexts before main resources, in case any
	// per-worker CL still holds an outstanding reference.
	releaseWorkerD3D12Resources();

	for ( UINT i = 0; i < FRAME_COUNT; i++ )
	{
		m_DeferredPrimitives[i].release();
		// Drain any deferred raw resources still held — at shutdown time the
		// GPU is idle (resetCommandList's fence wait happened during release()
		// path), so these are safe to release immediately.
		Array< ID3D12Resource * > & res = m_DeferredResources[i];
		for ( int j = 0; j < res.size(); ++j )
			if ( res[j] ) res[j]->Release();
		res.release();
		m_DynamicVB[i].Release();
		m_DynamicCB[i].Release();
		m_pRenderTargets[i].Reset();
		m_pCommandAllocators[i].Reset();
	}

	m_pDepthStencil.Reset();
	m_pCommandList.Reset();
	m_pFence.Reset();

	if ( m_hFenceEvent )
	{
		CloseHandle( m_hFenceEvent );
		m_hFenceEvent = NULL;
	}

	m_pRootSignature.Reset();
	m_pPostProcessRootSig.Reset();
	m_pSwapChain.Reset();
	m_pCommandQueue.Reset();
	m_pDevice.Reset();
	m_pAdapter.Reset();
	m_pDXGIFactory.Reset();
}

bool DisplayDeviceD3D12::readyShadowMap()
{
	if ( m_bShadowMapReady )
		return true;
	if ( !m_bShadowMapSupported )
		return false;

	m_bShadowMapSupported = false;

	if ( !m_pShadowMapShader.valid() || !m_pShadowMapShader->valid() )
		return false;
	if ( !m_pDevice )
		return false;

	// Release previous shadow map resources if size changed
	m_pShadowMapDepth.Reset();

	D3D12_RESOURCE_DESC rtDesc = {};
	rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rtDesc.DepthOrArraySize = 1;
	rtDesc.MipLevels = 1;
	rtDesc.Format = DXGI_FORMAT_R32_FLOAT;
	rtDesc.SampleDesc.Count = 1;
	rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE rtClear = {};
	rtClear.Format = DXGI_FORMAT_R32_FLOAT;
	rtClear.Color[0] = 1.0f;
	rtClear.Color[1] = 0.0f;
	rtClear.Color[2] = 0.0f;
	rtClear.Color[3] = 1.0f;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	// Single shadow map texture
	rtDesc.Width = m_szShadowMap.width;
	rtDesc.Height = m_szShadowMap.height;

	if ( FAILED(m_pDevice->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE,
		&rtDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &rtClear, IID_PPV_ARGS(&m_pShadowMapDepth) )) )
	{
		TRACE( "readyShadowMap: Failed to create cascade 0 RT" );
		return false;
	}

	// Reuse existing heap indices if already allocated (shadow map size change)
	if ( m_nShadowMapDSVIndex == UINT(-1) )
		m_nShadowMapDSVIndex = m_RTVHeap.Allocate();
	if ( m_nShadowMapDSVIndex == UINT(-1) )
	{
		TRACE( "readyShadowMap: Failed to allocate cascade 0 RTV" );
		m_pShadowMapDepth.Reset();
		return false;
	}

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	m_pDevice->CreateRenderTargetView( m_pShadowMapDepth.Get(), &rtvDesc,
		m_RTVHeap.GetCPUHandle( m_nShadowMapDSVIndex ) );

	if ( m_nShadowMapSRVStagingIndex == UINT(-1) )
		m_nShadowMapSRVStagingIndex = m_SRVStagingHeap.Allocate();
	if ( m_nShadowMapSRVStagingIndex == UINT(-1) )
	{
		TRACE( "readyShadowMap: Failed to allocate cascade 0 SRV" );
		m_pShadowMapDepth.Reset();
		return false;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	m_pDevice->CreateShaderResourceView( m_pShadowMapDepth.Get(), &srvDesc,
		m_SRVStagingHeap.GetCPUHandle( m_nShadowMapSRVStagingIndex ) );

	TRACE( "readyShadowMap: Created %dx%d R32_FLOAT shadow map",
		m_szShadowMap.width, m_szShadowMap.height );

	m_bShadowMapSupported = true;
	m_bShadowMapReady = true;
	return true;
}

//---------------------------------------------------------------------------------------------------
// Synchronization
//---------------------------------------------------------------------------------------------------

void DisplayDeviceD3D12::waitForGPU()
{
	if ( !m_pCommandQueue || !m_pFence || !m_hFenceEvent )
		return;

	const UINT64 fence = m_nFenceValues[m_nFrameIndex];
	HRESULT hrSignal = m_pCommandQueue->Signal( m_pFence.Get(), fence );
	if ( FAILED(hrSignal) )
	{
		// Signal fails when the device has been removed (TDR, driver reset).
		// GetCompletedValue returns UINT64_MAX in that case so the wait below
		// is correctly skipped, but surface the removal reason so the log shows
		// the actual cause instead of a cascade of downstream failures.
		HRESULT removed = m_pDevice ? m_pDevice->GetDeviceRemovedReason() : S_OK;
		TRACE( "waitForGPU: Signal failed hr=0x%08x, DeviceRemovedReason=0x%08x", hrSignal, removed );
	}

	if ( m_pFence->GetCompletedValue() < fence )
	{
		m_pFence->SetEventOnCompletion( fence, m_hFenceEvent );
		WaitForSingleObject( m_hFenceEvent, INFINITE );
	}

	// All GPU work is now complete.  Synchronize ALL frame slots to the same
	// next value so that moveToNextFrame() never signals a value LOWER than
	// the fence's current completed value (which would make it go backward
	// and cause subsequent waits to deadlock).
	const UINT64 nextValue = fence + 1;
	for ( UINT i = 0; i < FRAME_COUNT; i++ )
	{
		m_nFenceValues[i] = nextValue;
		m_nAllocatorFence[i] = 0;	// all allocators are now safe to reset
	}
}

void DisplayDeviceD3D12::moveToNextFrame()
{
	const UINT oldFrame = m_nFrameIndex;
	const UINT64 currentFenceValue = m_nFenceValues[m_nFrameIndex];
	m_pCommandQueue->Signal( m_pFence.Get(), currentFenceValue );

	// Record that this allocator's commands will be done when fence reaches currentFenceValue
	m_nAllocatorFence[oldFrame] = currentFenceValue;

	m_nFrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

	// Wait for the new frame's allocator to be free (its previous submission must be done).
	// This wait is where a GPU-bound frame shows up as CPU cost — if the GPU hasn't
	// finished the frame that last used this allocator, we block here.  With VSync off,
	// a persistent non-zero CPU% in this scope = GPU overbudget (not CPU-bound rendering).
	if ( m_pFence->GetCompletedValue() < m_nAllocatorFence[m_nFrameIndex] )
	{
		PROFILE_START( "DisplayDeviceD3D12::GPU_fence_wait" );
		m_pFence->SetEventOnCompletion( m_nAllocatorFence[m_nFrameIndex], m_hFenceEvent );
		WaitForSingleObject( m_hFenceEvent, INFINITE );
		PROFILE_END();
	}

	m_nFenceValues[m_nFrameIndex] = currentFenceValue + 1;
}

bool DisplayDeviceD3D12::initUploadQueue()
{
	// Create a COPY command queue — copy queues can only do copy operations,
	// avoiding debug-layer validation issues and resource state conflicts with the render queue.
	// Resources implicitly promote/decay to COMMON state on copy queues.
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
	queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	HRESULT hr = m_pDevice->CreateCommandQueue( &queueDesc, IID_PPV_ARGS(&m_pUploadQueue) );
	if ( FAILED(hr) )
		return false;

	hr = m_pDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&m_pUploadAllocator) );
	if ( FAILED(hr) )
		return false;

	hr = m_pDevice->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_COPY,
		m_pUploadAllocator.Get(), nullptr, IID_PPV_ARGS(&m_pUploadCommandList) );
	if ( FAILED(hr) )
		return false;
	m_pUploadCommandList->Close();

	hr = m_pDevice->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_pUploadFence) );
	if ( FAILED(hr) )
		return false;
	m_nUploadFenceValue = 0;
	m_hUploadFenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );

	TRACE( "Upload queue initialized for immediate texture uploads." );
	return true;
}

void DisplayDeviceD3D12::immediateTextureUpload( PrimitiveSurfaceD3D12 * pSurface )
{
	if ( !pSurface || pSurface->m_PendingMips.size() == 0 || !pSurface->m_Texture )
		return;
	if ( !m_pUploadCommandList || !m_pUploadAllocator )
		return;

	AutoLock lock( &m_UploadCS );

	// Wait for any previous upload to complete
	if ( m_pUploadFence->GetCompletedValue() < m_nUploadFenceValue )
	{
		m_pUploadFence->SetEventOnCompletion( m_nUploadFenceValue, m_hUploadFenceEvent );
		DWORD result = WaitForSingleObject( m_hUploadFenceEvent, 10000 );
		if ( result == WAIT_TIMEOUT )
		{
			TRACE( "DisplayDeviceD3D12::immediateTextureUpload() - Upload fence timeout waiting for previous upload!" );
			return;
		}
	}

	// Reset and open the upload command list
	m_pUploadAllocator->Reset();
	m_pUploadCommandList->Reset( m_pUploadAllocator.Get(), nullptr );

	// COPY queues don't support resource barriers — resources implicitly promote
	// from COMMON to COPY_DEST when written by a copy operation.
	// The resource was created in COPY_DEST state, and after previous copy queue
	// work it decays to COMMON, which promotes automatically.

	D3D12_RESOURCE_DESC texDesc = pSurface->m_Texture->GetDesc();

	// Calculate total upload buffer size needed
	UINT64 totalUploadSize = 0;
	for ( int i = 0; i < (int)pSurface->m_PendingMips.size(); ++i )
	{
		UINT64 mipSize = 0;
		m_pDevice->GetCopyableFootprints( &texDesc, pSurface->m_PendingMips[i].mipLevel, 1, 0,
			nullptr, nullptr, nullptr, &mipSize );
		totalUploadSize += (mipSize + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1)
			& ~(UINT64)(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);
	}

	// Create a temporary upload buffer
	ComPtr<ID3D12Resource> uploadBuffer;
	D3D12_HEAP_PROPERTIES uploadHeap = {};
	uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC bufDesc = {};
	bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufDesc.Width = totalUploadSize;
	bufDesc.Height = 1;
	bufDesc.DepthOrArraySize = 1;
	bufDesc.MipLevels = 1;
	bufDesc.SampleDesc.Count = 1;
	bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = m_pDevice->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadBuffer) );
	if ( FAILED(hr) )
	{
		m_pUploadCommandList->Close();
		// Free PendingMips to reclaim memory even if upload fails
		for ( int i = 0; i < (int)pSurface->m_PendingMips.size(); ++i )
			delete[] pSurface->m_PendingMips[i].pData;
		pSurface->m_PendingMips.release();
		return;
	}

	byte * pUploadBase = nullptr;
	uploadBuffer->Map( 0, nullptr, (void **)&pUploadBase );

	UINT64 bufferOffset = 0;
	int nativeBPP = GetNativePixelBytes( pSurface->m_eFormat );
	int dxgiBPP   = GetBytesPerPixel( pSurface->m_DXGIFormat );
	bool bExpand = !IsBlockCompressed( pSurface->m_DXGIFormat )
		&& (nativeBPP != dxgiBPP) && (nativeBPP > 0) && (dxgiBPP > 0);

	for ( int i = 0; i < (int)pSurface->m_PendingMips.size(); ++i )
	{
		PrimitiveSurfaceD3D12::PendingMip & mip = pSurface->m_PendingMips[i];
		if ( !mip.pData || mip.dataSize == 0 )
			continue;

		// Align offset
		bufferOffset = (bufferOffset + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1)
			& ~(UINT64)(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1);

		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
		UINT numRows = 0;
		UINT64 rowSizeInBytes = 0;
		UINT64 mipUploadSize = 0;
		m_pDevice->GetCopyableFootprints( &texDesc, mip.mipLevel, 1, bufferOffset,
			&footprint, &numRows, &rowSizeInBytes, &mipUploadSize );

		byte * pDst = pUploadBase + footprint.Offset;
		byte * pSrc = mip.pData;
		UINT srcRowPitch = mip.pitch;
		UINT dstRowPitch = footprint.Footprint.RowPitch;

		for ( UINT row = 0; row < numRows; ++row )
		{
			byte * pSrcRow = pSrc + row * srcRowPitch;
			byte * pDstRow = pDst + row * dstRowPitch;

			if ( bExpand )
			{
				UINT mipW = footprint.Footprint.Width;
				for ( UINT x = 0; x < mipW; ++x )
				{
					for ( int c = 0; c < nativeBPP; ++c )
						pDstRow[x * dxgiBPP + c] = pSrcRow[x * nativeBPP + c];
					for ( int c = nativeBPP; c < dxgiBPP; ++c )
						pDstRow[x * dxgiBPP + c] = 0xFF;
				}
			}
			else
			{
				UINT copyBytes = (UINT)rowSizeInBytes < srcRowPitch ? (UINT)rowSizeInBytes : srcRowPitch;
				memcpy( pDstRow, pSrcRow, copyBytes );
			}
		}

		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource = pSurface->m_Texture.Get();
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = mip.mipLevel;

		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = uploadBuffer.Get();
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint = footprint;

		m_pUploadCommandList->CopyTextureRegion( &dst, 0, 0, 0, &src, nullptr );

		bufferOffset = footprint.Offset + mipUploadSize;
	}

	// COPY queues cannot issue barriers — the resource will implicitly decay
	// to COMMON state after the copy queue finishes.  The render queue's
	// execute() path will transition COMMON → PSR when the texture is first used.
	pSurface->m_CurrentState = D3D12_RESOURCE_STATE_COMMON;

	// Close and execute
	m_pUploadCommandList->Close();
	ID3D12CommandList * ppLists[] = { m_pUploadCommandList.Get() };
	m_pUploadQueue->ExecuteCommandLists( 1, ppLists );

	// Signal and wait for the copy to finish
	m_nUploadFenceValue++;
	m_pUploadQueue->Signal( m_pUploadFence.Get(), m_nUploadFenceValue );
	if ( m_pUploadFence->GetCompletedValue() < m_nUploadFenceValue )
	{
		m_pUploadFence->SetEventOnCompletion( m_nUploadFenceValue, m_hUploadFenceEvent );
		DWORD result = WaitForSingleObject( m_hUploadFenceEvent, 10000 );
		if ( result == WAIT_TIMEOUT )
		{
			TRACE( "DisplayDeviceD3D12::immediateTextureUpload() - Upload fence timeout!" );
		}
	}

	// Unmap and release upload buffer
	uploadBuffer->Unmap( 0, nullptr );
	uploadBuffer.Reset();

	// Free PendingMips — data is now on the GPU
	for ( int i = 0; i < (int)pSurface->m_PendingMips.size(); ++i )
		delete[] pSurface->m_PendingMips[i].pData;
	pSurface->m_PendingMips.release();

	// Create SRV if needed.  Same _SRGB-aware view-format selection as the
	// two SRV creation sites in PrimitiveSurfaceD3D12.cpp — kept in sync
	// deliberately.  Without this, a texture uploaded via the synchronous
	// path would get a UNORM SRV even when sRGB is on, causing colour
	// textures to skip the sRGB→linear hardware decode at sample time.
	if ( !pSurface->m_bSRVCreated )
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = GetSRVFormat( pSurface->m_DXGIFormat, IsSRGBColourTexture( pSurface->m_eType ) );
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = pSurface->m_nLevels;

		m_pDevice->CreateShaderResourceView(
			pSurface->m_Texture.Get(), &srvDesc,
			m_SRVStagingHeap.GetCPUHandle( pSurface->m_SRVIndex ) );
		pSurface->m_bSRVCreated = true;
	}

	pSurface->m_bUploaded = true;
}

void DisplayDeviceD3D12::deferReleaseResource( ID3D12Resource * pResource )
{
	if ( !pResource )
		return;
	// Take ownership of one AddRef.  Caller passes a pointer that already has
	// a reference (typically from ComPtr::Detach()); we hold it on this
	// frame's deferred list and Release at beginScene of frame N+FRAME_COUNT
	// (after fence confirms GPU is past the last draw that referenced it).
	AutoLock lock( &m_DeferredPrimsLock );
	// The lock serializes concurrent producers on m_DeferredResources[idx].
	// It does NOT synchronize m_nFrameIndex against moveToNextFrame (which
	// writes m_nFrameIndex WITHOUT taking this lock).  A torn or stale read
	// here at worst pushes to the wrong frame index, which only delays the
	// release one frame; never lets us release while the GPU is still using
	// it (FRAME_COUNT=2 absorbs the one-frame skew).
	const UINT idx = m_nFrameIndex;
	m_DeferredResources[idx].push( pResource );
}

void DisplayDeviceD3D12::safeDeferReleaseResource( DisplayDevice * pDev, ID3D12Resource * pRes )
{
	if ( !pRes )
		return;
	// Look up pDev in the live-device list — pointer comparison only,
	// never dereferences pDev.  Safe to call with a dangling pointer.
	for ( int i = 0; i < sm_DeviceList.size(); ++i )
	{
		if ( sm_DeviceList[i] == pDev )
		{
			((DisplayDeviceD3D12 *)pDev)->deferReleaseResource( pRes );
			return;
		}
	}
	// Device gone (or never was on the list) — just drop the COM ref.
	pRes->Release();
}

void DisplayDeviceD3D12::flushCommandList()
{
	if ( !m_pCommandList || !m_bCommandListOpen )
		return;

	m_bCommandListOpen = false;
	{
		PROFILE_START( "flushCommandList:Close" );
		m_pCommandList->Close();
		PROFILE_END();
	}
	{
		PROFILE_START( "flushCommandList:ExecuteCommandLists" );
		ID3D12CommandList * ppCommandLists[] = { m_pCommandList.Get() };
		m_pCommandQueue->ExecuteCommandLists( _countof(ppCommandLists), ppCommandLists );
		PROFILE_END();
	}
}

void DisplayDeviceD3D12::resetCommandList()
{
	// Safety: ensure the GPU has finished all commands from this allocator's
	// last submission before resetting.  This is the last line of defense
	// against COMMAND_ALLOCATOR_SYNC errors.
	if ( m_pFence && m_hFenceEvent && m_nAllocatorFence[m_nFrameIndex] > 0 )
	{
		if ( m_pFence->GetCompletedValue() < m_nAllocatorFence[m_nFrameIndex] )
		{
			m_pFence->SetEventOnCompletion( m_nAllocatorFence[m_nFrameIndex], m_hFenceEvent );
			WaitForSingleObject( m_hFenceEvent, INFINITE );
		}
	}

	m_pCommandAllocators[m_nFrameIndex]->Reset();
	m_pCommandList->Reset( m_pCommandAllocators[m_nFrameIndex].Get(), nullptr );
	m_bCommandListOpen = true;

	// Command-list reset clears the GPU pipeline-state slot, so our last-bound
	// PSO cache must also drop.  Otherwise the next setPSO() call could believe
	// the desired PSO is already bound and skip the SetPipelineState.
	m_pCurrentPSO = nullptr;

	// Light CB ring's GpuVAs point into the previous frame's upload ring which
	// has been reset — invalidate every slot so we don't bind stale memory.
	for ( int i = 0; i < LIGHT_CB_RING_SLOTS; ++i )
		m_LightCBRingValid[i] = false;
	m_LightCBRingHead = 0;
}

//---------------------------------------------------------------------------------------------------
// Projection
//---------------------------------------------------------------------------------------------------

void DisplayDeviceD3D12::updateProjection()
{
	// Build view matrix
	Matrix33 invFrame = ~m_Proj.m_mFrame;
	Vector3 viewOffset = m_Proj.m_mFrame * -m_Proj.m_vPosition;
	XMMATRIX view;
	setXMMatrix( view, invFrame, viewOffset );
	m_mView = view;

	// Build projection matrix
	if ( m_Proj.m_fFOV > 0.0f )	// perspective
	{
		float fh = m_Proj.m_fFOV;
		float fv = m_Proj.m_fFOV * ((float)m_Proj.m_rWindow.height() / (float)m_Proj.m_rWindow.width());
		float w = 1.0f / tanf( fh * 0.5f );
		float h = 1.0f / tanf( fv * 0.5f );
		float Q = m_Proj.m_fBack / (m_Proj.m_fBack - m_Proj.m_fFront);

		m_mProj = XMMATRIX(
			w, 0, 0, 0,
			0, h, 0, 0,
			0, 0, Q, 1.0f,
			0, 0, -Q * m_Proj.m_fFront, 0
		);
	}
	else	// orthographic — map pixel (left,top) → NDC(-1,+1) and (right,bottom) → NDC(+1,-1)
	{
		m_mProj = XMMatrixOrthographicOffCenterLH(
			(float)m_Proj.m_rWindow.left,
			(float)(m_Proj.m_rWindow.right + 1),
			(float)(m_Proj.m_rWindow.bottom + 1),
			(float)m_Proj.m_rWindow.top,
			m_Proj.m_fFront,
			m_Proj.m_fBack );
	}

	// Set viewport on command list — only valid while the command list is open
	if ( m_bCommandListOpen )
	{
		D3D12_VIEWPORT viewport = {};
		viewport.TopLeftX = (float)m_Proj.m_rWindow.left;
		viewport.TopLeftY = (float)m_Proj.m_rWindow.top;
		viewport.Width = (float)m_Proj.m_rWindow.width();
		viewport.Height = (float)m_Proj.m_rWindow.height();
		viewport.MinDepth = 0.0f;
		viewport.MaxDepth = 1.0f;
		m_pCommandList->RSSetViewports( 1, &viewport );

		D3D12_RECT scissor = {
			m_Proj.m_rWindow.left, m_Proj.m_rWindow.top,
			m_Proj.m_rWindow.right, m_Proj.m_rWindow.bottom };
		m_pCommandList->RSSetScissorRects( 1, &scissor );
	}
}

//---------------------------------------------------------------------------------------------------
// FXAA post-process
//---------------------------------------------------------------------------------------------------

bool DisplayDeviceD3D12::createFXAA()
{
	RectInt rw = renderWindow();
	UINT width  = (UINT)rw.width();
	UINT height = (UINT)rw.height();
	if ( width == 0 || height == 0 )
		return false;

	// Only the scene RT dimensions depend on window size.  Shader bytecode,
	// root signature, and PSO are size-independent — preserve them across
	// resize so an interactive WM_SIZE drag doesn't recompile HLSL + rebuild
	// the PSO on every mouse-move event.
	m_pSceneRT.Reset();

	// Reset state-tracking flag — the new scene RT is created below with
	// initial state PIXEL_SHADER_RESOURCE.  If this flag is left true from
	// the previous scene RT's last state, beginScene skips the PSR→RT
	// transition and the subsequent ClearRenderTargetView hits a resource
	// that's still in PSR state → GPU validation error and visual corruption.
	m_bSceneRTisRT = false;

	// Create intermediate render target (same format as swap chain)
	D3D12_RESOURCE_DESC rtDesc = {};
	rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rtDesc.Width = width;
	rtDesc.Height = height;
	rtDesc.DepthOrArraySize = 1;
	rtDesc.MipLevels = 1;
	rtDesc.Format = SCENE_RT_FORMAT;
	rtDesc.SampleDesc.Count = 1;
	rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = SCENE_RT_FORMAT;
	clearValue.Color[3] = 1.0f;	// match beginScene clear color {0,0,0,1}

	HRESULT hr = m_pDevice->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
		IID_PPV_ARGS(&m_pSceneRT) );
	if ( FAILED(hr) )
	{
		HRESULT removed = m_pDevice->GetDeviceRemovedReason();
		TRACE( "createFXAA: CreateCommittedResource failed hr=0x%08x removed=0x%08x  %ux%u fmt=%d  RTVslot=%u SRVslot=%u",
			hr, removed, width, height, (int)SCENE_RT_FORMAT,
			m_nSceneRTVIndex, m_nSceneSRVIndex );
		// Scene RT itself failed — disable the whole post-process pipeline.
		m_bSceneRTEnabled = false;
		m_eAAMode = AA_NONE;
		return false;
	}

	// Create RTV for scene RT via allocator (avoids collision with shadow map, effects)
	if ( m_nSceneRTVIndex == UINT(-1) )
		m_nSceneRTVIndex = m_RTVHeap.Allocate();
	m_pDevice->CreateRenderTargetView( m_pSceneRT.Get(), nullptr,
		m_RTVHeap.GetCPUHandle( m_nSceneRTVIndex ) );

	// Create SRV for scene RT in the shader-visible heap
	if ( m_nSceneSRVIndex == UINT(-1) )
		m_nSceneSRVIndex = m_SRVStagingHeap.Allocate();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = SCENE_RT_FORMAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	m_pDevice->CreateShaderResourceView( m_pSceneRT.Get(), &srvDesc,
		m_SRVStagingHeap.GetCPUHandle( m_nSceneSRVIndex ) );

	// Pipeline (shader + root sig + PSO) is built on first call only.
	if ( m_pFXAAPSO && m_pFXAARootSig && m_pFXAAShader.valid() && m_pFXAAShader->valid() )
		return true;

	// Load FXAA shader
	m_pFXAAShader = getShader( "Shaders/FXAA.hlsl" );
	if ( !m_pFXAAShader.valid() || !m_pFXAAShader->valid() )
	{
		TRACE( "createFXAA: Failed to compile FXAA shader!" );
		// FXAA shader unavailable — disable the post-process pipeline.  When the
		// tonemap-only AA_NONE path lands (Phase 2), this can fall back to AA_NONE
		// while keeping the scene RT pipeline alive instead.
		m_bSceneRTEnabled = false;
		m_eAAMode = AA_NONE;
		return false;
	}

	// Create FXAA root signature:
	// [0] CBV  - FXAA constants (rcpFrame, thresholds)
	// [1] Table - SRV (scene texture)
	// [2] Table - Sampler
	D3D12_ROOT_PARAMETER fxaaParams[3] = {};

	fxaaParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	fxaaParams[0].Descriptor.ShaderRegister = 0;
	fxaaParams[0].Descriptor.RegisterSpace = 0;
	fxaaParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// 2-SRV table: t0 = scene RT, t1 = 1x1 R32F exposure multiplier (from
	// DisplayEffectExposure or the default fallback set at createFXAA time).
	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 2;
	srvRange.BaseShaderRegister = 0;
	srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	fxaaParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	fxaaParams[1].DescriptorTable.NumDescriptorRanges = 1;
	fxaaParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
	fxaaParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_DESCRIPTOR_RANGE samplerRange = {};
	samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
	samplerRange.NumDescriptors = 1;
	samplerRange.BaseShaderRegister = 0;
	samplerRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	fxaaParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	fxaaParams[2].DescriptorTable.NumDescriptorRanges = 1;
	fxaaParams[2].DescriptorTable.pDescriptorRanges = &samplerRange;
	fxaaParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
	rsDesc.NumParameters = 3;
	rsDesc.pParameters = fxaaParams;
	rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> sig, err;
	hr = D3D12SerializeRootSignature( &rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err );
	if ( FAILED(hr) )
	{
		if ( err ) TRACE( (const char *)err->GetBufferPointer() );
		m_bSceneRTEnabled = false;
		m_eAAMode = AA_NONE;
		return false;
	}

	hr = m_pDevice->CreateRootSignature( 0, sig->GetBufferPointer(), sig->GetBufferSize(),
		IID_PPV_ARGS(&m_pFXAARootSig) );
	if ( FAILED(hr) )
	{
		m_bSceneRTEnabled = false;
		m_eAAMode = AA_NONE;
		return false;
	}

	// Create FXAA PSO — fullscreen triangle, no input layout, no depth
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = m_pFXAARootSig.Get();
	psoDesc.VS = m_pFXAAShader->vertexShaderBytecode();
	psoDesc.PS = m_pFXAAShader->pixelShaderBytecode();
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

	hr = m_pDevice->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pFXAAPSO) );
	if ( FAILED(hr) )
	{
		TRACE( "createFXAA: Failed to create FXAA PSO!" );
		m_bSceneRTEnabled = false;
		m_eAAMode = AA_NONE;
		return false;
	}

	// Tonemap-only PSO — same root sig and PSO desc as FXAA, only PS bytecode
	// differs.  Used when m_eAAMode == AA_NONE (FSAA=FSAA_NONE in config) to
	// resolve the HDR scene RT to the LDR backbuffer with no AA applied.  A
	// failure here is non-fatal — FXAA still works, AA_NONE just falls back
	// to "no resolve" (whatever the AA_NONE selector lands on, applyTonemap()
	// will early-out if the PSO is null).
	m_pTonemapShader = getShader( "Shaders/Tonemap.hlsl" );
	if ( m_pTonemapShader.valid() && m_pTonemapShader->valid() )
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC tmDesc = psoDesc;
		tmDesc.VS = m_pTonemapShader->vertexShaderBytecode();
		tmDesc.PS = m_pTonemapShader->pixelShaderBytecode();
		hr = m_pDevice->CreateGraphicsPipelineState( &tmDesc, IID_PPV_ARGS(&m_pTonemapPSO) );
		if ( FAILED(hr) )
			TRACE( "createFXAA: Failed to create Tonemap PSO (AA_NONE path will be unavailable)" );
	}
	else
	{
		TRACE( "createFXAA: Tonemap.hlsl missing or failed to compile (AA_NONE path will be unavailable)" );
	}

	TRACE( "FXAA initialized (%dx%d)", width, height );

	// SMAA shares the scene RT lifecycle — its intermediate RTs are sized to
	// the same dimensions, so we (re)create them here.  Failure is non-fatal:
	// if SMAA can't init, AA_SMAA falls back to AA_FXAA in present()'s switch
	// via the m_bSMAAAvailable flag.
	createSMAA();

	return true;
}

// Lazy-init the 1x1 R32F=1.0 fallback exposure texture used as t1 by
// applyFXAA / applyTonemap when no exposure effect ran this frame.  Done
// lazily (rather than in createFXAA) because clear-to-1.0 + transition-to-PSR
// needs an open command list, which createFXAA doesn't have during init /
// resize flushes.  Clear vector must exactly match the optimized clear value
// set at resource-creation time ({1, 0, 0, 0}) or D3D12 logs a
// CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE warning every first-frame.
void DisplayDeviceD3D12::ensureDefaultExposureTexture()
{
	if ( m_bDefaultExposureInitialized )
		return;

	D3D12_RESOURCE_DESC expDesc = {};
	expDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	expDesc.Width  = 1;
	expDesc.Height = 1;
	expDesc.DepthOrArraySize = 1;
	expDesc.MipLevels = 1;
	expDesc.Format = DXGI_FORMAT_R32_FLOAT;
	expDesc.SampleDesc.Count = 1;
	expDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE cv = {};
	cv.Format = DXGI_FORMAT_R32_FLOAT;
	cv.Color[0] = 1.0f;

	HRESULT hrExp = m_pDevice->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE,
		&expDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv,
		IID_PPV_ARGS(&m_pDefaultExposureTex) );
	if ( FAILED(hrExp) )
		return;

	if ( m_nDefaultExposureRTVIndex == UINT(-1) )
		m_nDefaultExposureRTVIndex = m_RTVHeap.Allocate();
	m_pDevice->CreateRenderTargetView( m_pDefaultExposureTex.Get(), nullptr,
		m_RTVHeap.GetCPUHandle( m_nDefaultExposureRTVIndex ) );

	if ( m_nDefaultExposureSRVIndex == UINT(-1) )
		m_nDefaultExposureSRVIndex = m_SRVStagingHeap.Allocate();
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	m_pDevice->CreateShaderResourceView( m_pDefaultExposureTex.Get(), &srvDesc,
		m_SRVStagingHeap.GetCPUHandle( m_nDefaultExposureSRVIndex ) );

	float white[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	m_pCommandList->ClearRenderTargetView(
		m_RTVHeap.GetCPUHandle( m_nDefaultExposureRTVIndex ), white, 0, nullptr );
	TransitionResource( m_pCommandList.Get(), m_pDefaultExposureTex.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	m_bDefaultExposureInitialized = true;
}

void DisplayDeviceD3D12::applyFXAA()
{
	if ( m_eAAMode != AA_FXAA || !m_bSceneRTEnabled || !m_pSceneRT || !m_pFXAAPSO || !m_bCommandListOpen )
		return;

	ensureDefaultExposureTexture();

	RectInt rw = renderWindow();
	float width  = (float)rw.width();
	float height = (float)rw.height();

	// Transition scene RT from render target → shader resource
	if ( m_bSceneRTisRT )
	{
		TransitionResource( m_pCommandList.Get(), m_pSceneRT.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		m_bSceneRTisRT = false;
	}

	// Set the swap chain back buffer as render target
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RTVHeap.GetCPUHandle( m_nFrameIndex );
	m_pCommandList->OMSetRenderTargets( 1, &rtv, FALSE, nullptr );

	// Set viewport and scissor for fullscreen
	D3D12_VIEWPORT vp = { 0, 0, width, height, 0, 1 };
	m_pCommandList->RSSetViewports( 1, &vp );
	D3D12_RECT scissor = { 0, 0, (LONG)width, (LONG)height };
	m_pCommandList->RSSetScissorRects( 1, &scissor );

	// Set FXAA pipeline.  Root-sig swap clears root bindings — drop the cache
	// so any later material draw re-issues instead of assuming stale state.
	invalidateBoundSRVTable();
	m_pCommandList->SetGraphicsRootSignature( m_pFXAARootSig.Get() );
	setPSO( m_pCommandList.Get(), m_pFXAAPSO.Get() );

	// Bind descriptor heaps
	ID3D12DescriptorHeap * heaps[] = { m_SRVHeap.Get(), m_SamplerHeap.Get() };
	m_pCommandList->SetDescriptorHeaps( _countof(heaps), heaps );

	// Upload FXAA constant buffer
	struct FXAACBuffer {
		float rcpFrameX, rcpFrameY;
		float fSubpix;
		float fEdgeThreshold;
		float fEdgeThresholdMin;
		float pad[3];
	};
	FXAACBuffer cb;
	cb.rcpFrameX = 1.0f / width;
	cb.rcpFrameY = 1.0f / height;
	cb.fSubpix = 0.75f;
	cb.fEdgeThreshold = 0.166f;
	cb.fEdgeThresholdMin = 0.0833f;

	UploadRingBuffer::Allocation cbAlloc = allocateCB( sizeof(FXAACBuffer) );
	memcpy( cbAlloc.cpuAddress, &cb, sizeof(cb) );
	m_pCommandList->SetGraphicsRootConstantBufferView( 0, cbAlloc.gpuAddress );

	// Copy scene SRV + exposure SRV into a 2-slot run in the shader-visible heap.
	// t0 = scene RT, t1 = 1x1 R32F exposure multiplier.  DisplayEffectExposure
	// sets m_nCurrentExposureSRVIndex each frame to its ping-pong write target;
	// if no exposure effect ran this frame (or exposure is disabled), it stays
	// at m_nDefaultExposureSRVIndex which points at a 1x1 R32F=1.0 fallback.
	UINT fxaaSRVSlot = allocSRVSlots( 2 );
	m_pDevice->CopyDescriptorsSimple( 1,
		m_SRVHeap.GetCPUHandle( fxaaSRVSlot ),
		m_SRVStagingHeap.GetCPUHandle( m_nSceneSRVIndex ),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

	UINT exposureSrc = ( m_nCurrentExposureSRVIndex != UINT(-1) )
		? m_nCurrentExposureSRVIndex
		: m_nDefaultExposureSRVIndex;
	if ( exposureSrc != UINT(-1) )
	{
		m_pDevice->CopyDescriptorsSimple( 1,
			m_SRVHeap.GetCPUHandle( fxaaSRVSlot + 1 ),
			m_SRVStagingHeap.GetCPUHandle( exposureSrc ),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	}

	m_pCommandList->SetGraphicsRootDescriptorTable( 1, m_SRVHeap.GetGPUHandle( fxaaSRVSlot ) );
	m_pCommandList->SetGraphicsRootDescriptorTable( 2, m_SamplerHeap.GetGPUHandle( 0 ) );

	// Draw fullscreen triangle (3 vertices, no vertex buffer — generated in VS)
	m_pCommandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	m_pCommandList->IASetVertexBuffers( 0, 0, nullptr );
	m_pCommandList->DrawInstanced( 3, 1, 0, 0 );

	// Subsequent draws (OVERLAY/UI) target the swap chain backbuffer (R8G8B8A8),
	// not the scene RT — flag this so material PSOs key off the right format.
	m_bRenderingPostAA = true;

	// Leave scene RT in PSR state — beginScene will transition PSR → RT
}

//---------------------------------------------------------------------------------------------------
// applyTonemap — AA_NONE path.  Resolves the HDR scene RT to the LDR
// backbuffer with exposure + ACES filmic tonemap and no anti-aliasing.
// Mirrors applyFXAA's resource binding (shared root sig, same SRV + CB
// layout) — only the bound PSO and CB constants differ.

void DisplayDeviceD3D12::applyTonemap()
{
	if ( m_eAAMode != AA_NONE || !m_bSceneRTEnabled || !m_pSceneRT || !m_pTonemapPSO || !m_bCommandListOpen )
		return;

	ensureDefaultExposureTexture();

	RectInt rw = renderWindow();
	float width  = (float)rw.width();
	float height = (float)rw.height();

	if ( m_bSceneRTisRT )
	{
		TransitionResource( m_pCommandList.Get(), m_pSceneRT.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		m_bSceneRTisRT = false;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RTVHeap.GetCPUHandle( m_nFrameIndex );
	m_pCommandList->OMSetRenderTargets( 1, &rtv, FALSE, nullptr );

	D3D12_VIEWPORT vp = { 0, 0, width, height, 0, 1 };
	m_pCommandList->RSSetViewports( 1, &vp );
	D3D12_RECT scissor = { 0, 0, (LONG)width, (LONG)height };
	m_pCommandList->RSSetScissorRects( 1, &scissor );

	invalidateBoundSRVTable();
	m_pCommandList->SetGraphicsRootSignature( m_pFXAARootSig.Get() );
	setPSO( m_pCommandList.Get(), m_pTonemapPSO.Get() );

	ID3D12DescriptorHeap * heaps[] = { m_SRVHeap.Get(), m_SamplerHeap.Get() };
	m_pCommandList->SetDescriptorHeaps( _countof(heaps), heaps );

	// Same CB layout as FXAA — Tonemap.hlsl only reads rcpFrame; the other
	// fields are present so the shared root sig stays compatible.
	struct TonemapCBuffer {
		float rcpFrameX, rcpFrameY;
		float fSubpix;
		float fEdgeThreshold;
		float fEdgeThresholdMin;
		float pad[3];
	};
	TonemapCBuffer cb;
	cb.rcpFrameX = 1.0f / width;
	cb.rcpFrameY = 1.0f / height;
	cb.fSubpix = 0.0f;
	cb.fEdgeThreshold = 0.0f;
	cb.fEdgeThresholdMin = 0.0f;

	UploadRingBuffer::Allocation cbAlloc = allocateCB( sizeof(TonemapCBuffer) );
	memcpy( cbAlloc.cpuAddress, &cb, sizeof(cb) );
	m_pCommandList->SetGraphicsRootConstantBufferView( 0, cbAlloc.gpuAddress );

	UINT srvSlot = allocSRVSlots( 2 );
	m_pDevice->CopyDescriptorsSimple( 1,
		m_SRVHeap.GetCPUHandle( srvSlot ),
		m_SRVStagingHeap.GetCPUHandle( m_nSceneSRVIndex ),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

	UINT exposureSrc = ( m_nCurrentExposureSRVIndex != UINT(-1) )
		? m_nCurrentExposureSRVIndex
		: m_nDefaultExposureSRVIndex;
	if ( exposureSrc != UINT(-1) )
	{
		m_pDevice->CopyDescriptorsSimple( 1,
			m_SRVHeap.GetCPUHandle( srvSlot + 1 ),
			m_SRVStagingHeap.GetCPUHandle( exposureSrc ),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
	}

	m_pCommandList->SetGraphicsRootDescriptorTable( 1, m_SRVHeap.GetGPUHandle( srvSlot ) );
	m_pCommandList->SetGraphicsRootDescriptorTable( 2, m_SamplerHeap.GetGPUHandle( 0 ) );

	m_pCommandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	m_pCommandList->IASetVertexBuffers( 0, 0, nullptr );
	m_pCommandList->DrawInstanced( 3, 1, 0, 0 );

	m_bRenderingPostAA = true;
}

//---------------------------------------------------------------------------------------------------
// SMAA — 3-pass MLAA-style edge AA.  Called from createFXAA() so it shares
// the scene RT lifecycle (allocated on init, recreated on resize).  Failure
// here is non-fatal — the AA_SMAA path silently falls back to AA_FXAA via
// the m_bSMAAAvailable flag.
//---------------------------------------------------------------------------------------------------

static bool _smaaCompileEntry( const wchar_t * pPath, const char * pEntry, const char * pTarget,
	const D3D_SHADER_MACRO * pMacros, ComPtr<ID3DBlob> & blobOut )
{
	UINT flags = 0;
#if defined(_DEBUG)
	flags |= D3DCOMPILE_DEBUG;
#endif
	ComPtr<ID3DBlob> errors;
	HRESULT hr = D3DCompileFromFile( pPath, pMacros, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		pEntry, pTarget, flags, 0, &blobOut, &errors );
	if ( FAILED(hr) )
	{
		// Brace each branch — the TRACE macro embeds its own trailing
		// semicolon, so an unbraced `if (x) TRACE(...); else TRACE(...);`
		// expands to `if (x) Logging::report(...) ;; else …` (the second
		// semicolon becomes a null statement outside the if, orphaning
		// the else — C2181).
		if ( errors )
		{
			TRACE( "SMAA shader compile error (%s): %s", pEntry, (const char *)errors->GetBufferPointer() );
		}
		else
		{
			TRACE( "SMAA shader compile error (%s): hr=0x%08x", pEntry, hr );
		}
		return false;
	}
	return true;
}

bool DisplayDeviceD3D12::createSMAA()
{
	// Bail early if the scene RT pipeline isn't up — SMAA needs the same RT
	// the other AA paths read.
	if ( !m_bSceneRTEnabled || !m_pSceneRT )
	{
		m_bSMAAAvailable = false;
		return false;
	}

	RectInt rw = renderWindow();
	UINT width  = (UINT)rw.width();
	UINT height = (UINT)rw.height();
	if ( width == 0 || height == 0 )
	{
		m_bSMAAAvailable = false;
		return false;
	}

	SizeInt currentSize( (int)width, (int)height );

	// Compile + root sig + PSOs are one-time setup; only the intermediate RTs
	// get recreated on resize (sized to the new scene RT).
	const bool bFirstInit = !m_pSMAAEdgePSO || !m_pSMAARootSig;
	const bool bSizeChanged = m_LastSMAASize != currentSize;

	if ( bFirstInit )
	{
		// Map sm_nShaderDetail to the SMAA_PRESET define.  Both enums happen
		// to use 0/1/2/3 (LOW/MEDIUM/HIGH/EXTREME) so the cast is direct, but
		// stringify here so the preprocessor sees a literal.
		const char * pPresetStr = "1";
		switch ( DisplayDevice::sm_nShaderDetail )
		{
		case DisplayDevice::SHADER_DETAIL_LOW:     pPresetStr = "0"; break;
		case DisplayDevice::SHADER_DETAIL_MEDIUM:  pPresetStr = "1"; break;
		case DisplayDevice::SHADER_DETAIL_HIGH:    pPresetStr = "2"; break;
		case DisplayDevice::SHADER_DETAIL_EXTREME: pPresetStr = "3"; break;
		}
		D3D_SHADER_MACRO macros[] = {
			{ "SMAA_PRESET", pPresetStr },
			{ nullptr, nullptr }
		};

		CharString sPath = DisplayDevice::sm_sShadersPath + "Shaders/SMAA.hlsl";
		wchar_t wszPath[MAX_PATH];
		MultiByteToWideChar( CP_ACP, 0, sPath, -1, wszPath, MAX_PATH );

		if ( !_smaaCompileEntry( wszPath, "vs_main", "vs_5_1", macros, m_pSMAAVSBlob ) ||
		     !_smaaCompileEntry( wszPath, "PS_Edge", "ps_5_1", macros, m_pSMAAPSEdgeBlob ) ||
		     !_smaaCompileEntry( wszPath, "PS_Weights", "ps_5_1", macros, m_pSMAAPSWeightsBlob ) ||
		     !_smaaCompileEntry( wszPath, "PS_Blend", "ps_5_1", macros, m_pSMAAPSBlendBlob ) )
		{
			TRACE( "createSMAA: shader compile failed — AA_SMAA will fall back to AA_FXAA" );
			m_bSMAAAvailable = false;
			return false;
		}

		// Root signature:
		//   [0] CBV b0 (rcpFrame)
		//   [1] SRV table (t0=scene, t1=edges-or-weights, t2=exposure)
		//   [2] Sampler table (s0=linear)
		D3D12_ROOT_PARAMETER params[3] = {};

		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[0].Descriptor.ShaderRegister = 0;
		params[0].Descriptor.RegisterSpace = 0;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_DESCRIPTOR_RANGE srvRange = {};
		srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		srvRange.NumDescriptors = 3;
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
			if ( err ) TRACE( "createSMAA root sig error: %s", (const char *)err->GetBufferPointer() );
			m_bSMAAAvailable = false;
			return false;
		}
		hr = m_pDevice->CreateRootSignature( 0, sig->GetBufferPointer(), sig->GetBufferSize(),
			IID_PPV_ARGS(&m_pSMAARootSig) );
		if ( FAILED(hr) ) { m_bSMAAAvailable = false; return false; }

		// Base PSO desc — fullscreen triangle, no input layout, no depth.
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = m_pSMAARootSig.Get();
		psoDesc.VS = { m_pSMAAVSBlob->GetBufferPointer(), m_pSMAAVSBlob->GetBufferSize() };
		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		psoDesc.RasterizerState.DepthClipEnable = FALSE;
		psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		psoDesc.DepthStencilState.DepthEnable = FALSE;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleMask = UINT_MAX;

		// Pass 1 — Edge detection.  Output: R8G8_UNORM edge mask.
		psoDesc.PS = { m_pSMAAPSEdgeBlob->GetBufferPointer(), m_pSMAAPSEdgeBlob->GetBufferSize() };
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8_UNORM;
		hr = m_pDevice->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pSMAAEdgePSO) );
		if ( FAILED(hr) ) { TRACE( "createSMAA: PS_Edge PSO failed" ); m_bSMAAAvailable = false; return false; }

		// Pass 2 — Weight calculation.  Output: R8G8B8A8_UNORM blend weights.
		psoDesc.PS = { m_pSMAAPSWeightsBlob->GetBufferPointer(), m_pSMAAPSWeightsBlob->GetBufferSize() };
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		hr = m_pDevice->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pSMAAWeightsPSO) );
		if ( FAILED(hr) ) { TRACE( "createSMAA: PS_Weights PSO failed" ); m_bSMAAAvailable = false; return false; }

		// Pass 3 — Neighborhood blend.  Output: R8G8B8A8_UNORM backbuffer.
		psoDesc.PS = { m_pSMAAPSBlendBlob->GetBufferPointer(), m_pSMAAPSBlendBlob->GetBufferSize() };
		psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		hr = m_pDevice->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&m_pSMAABlendPSO) );
		if ( FAILED(hr) ) { TRACE( "createSMAA: PS_Blend PSO failed" ); m_bSMAAAvailable = false; return false; }
	}

	if ( bFirstInit || bSizeChanged )
	{
		// (Re)allocate intermediate RTs at scene-RT size.  Edge mask is R8G8
		// (2 bytes/pixel ≈ 4 MB at 1080p, 16 MB at 4K).  Weights are RGBA8 (4
		// bytes/pixel ≈ 8 MB at 1080p, 32 MB at 4K).  Both well within the
		// shared-heap budget.
		m_pSMAAEdgeRT.Reset();
		m_pSMAAWeightsRT.Reset();

		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_DESC rtDesc = {};
		rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		rtDesc.Width = width;
		rtDesc.Height = height;
		rtDesc.DepthOrArraySize = 1;
		rtDesc.MipLevels = 1;
		rtDesc.SampleDesc.Count = 1;
		rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		// Edge RT
		rtDesc.Format = DXGI_FORMAT_R8G8_UNORM;
		D3D12_CLEAR_VALUE cvEdge = {};
		cvEdge.Format = DXGI_FORMAT_R8G8_UNORM;
		HRESULT hr = m_pDevice->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE,
			&rtDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cvEdge,
			IID_PPV_ARGS(&m_pSMAAEdgeRT) );
		if ( FAILED(hr) ) { TRACE( "createSMAA: Edge RT alloc failed" ); m_bSMAAAvailable = false; return false; }

		// Weights RT
		rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		D3D12_CLEAR_VALUE cvW = {};
		cvW.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		hr = m_pDevice->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE,
			&rtDesc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cvW,
			IID_PPV_ARGS(&m_pSMAAWeightsRT) );
		if ( FAILED(hr) ) { TRACE( "createSMAA: Weights RT alloc failed" ); m_bSMAAAvailable = false; return false; }

		// (Re)create RTV + SRV descriptors.  Allocate slots on first init; on
		// resize we reuse the existing slot indices and just overwrite the view.
		if ( m_nSMAAEdgeRTVIndex == UINT(-1) )    m_nSMAAEdgeRTVIndex    = m_RTVHeap.Allocate();
		if ( m_nSMAAEdgeSRVIndex == UINT(-1) )    m_nSMAAEdgeSRVIndex    = m_SRVStagingHeap.Allocate();
		if ( m_nSMAAWeightsRTVIndex == UINT(-1) ) m_nSMAAWeightsRTVIndex = m_RTVHeap.Allocate();
		if ( m_nSMAAWeightsSRVIndex == UINT(-1) ) m_nSMAAWeightsSRVIndex = m_SRVStagingHeap.Allocate();

		m_pDevice->CreateRenderTargetView( m_pSMAAEdgeRT.Get(), nullptr,
			m_RTVHeap.GetCPUHandle( m_nSMAAEdgeRTVIndex ) );
		m_pDevice->CreateRenderTargetView( m_pSMAAWeightsRT.Get(), nullptr,
			m_RTVHeap.GetCPUHandle( m_nSMAAWeightsRTVIndex ) );

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;

		srvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
		m_pDevice->CreateShaderResourceView( m_pSMAAEdgeRT.Get(), &srvDesc,
			m_SRVStagingHeap.GetCPUHandle( m_nSMAAEdgeSRVIndex ) );

		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		m_pDevice->CreateShaderResourceView( m_pSMAAWeightsRT.Get(), &srvDesc,
			m_SRVStagingHeap.GetCPUHandle( m_nSMAAWeightsSRVIndex ) );

		m_LastSMAASize = currentSize;
	}

	m_bSMAAAvailable = true;
	TRACE( "SMAA initialized (%dx%d, preset=%d)", width, height, DisplayDevice::sm_nShaderDetail );
	return true;
}

//---------------------------------------------------------------------------------------------------

void DisplayDeviceD3D12::applySMAA()
{
	if ( m_eAAMode != AA_SMAA || !m_bSMAAAvailable || !m_bSceneRTEnabled || !m_pSceneRT
		|| !m_pSMAAEdgePSO || !m_pSMAAWeightsPSO || !m_pSMAABlendPSO
		|| !m_pSMAAEdgeRT || !m_pSMAAWeightsRT || !m_bCommandListOpen )
		return;

	// Lazy-init the default exposure texture if no AA pass has run yet this
	// session.  Same rationale as in applyFXAA / applyTonemap — needs an open
	// CL to clear+transition the 1x1 fallback, which createSMAA() doesn't
	// have during init.
	if ( !m_bDefaultExposureInitialized )
	{
		D3D12_RESOURCE_DESC expDesc = {};
		expDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		expDesc.Width  = 1;
		expDesc.Height = 1;
		expDesc.DepthOrArraySize = 1;
		expDesc.MipLevels = 1;
		expDesc.Format = DXGI_FORMAT_R32_FLOAT;
		expDesc.SampleDesc.Count = 1;
		expDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_CLEAR_VALUE cv = {};
		cv.Format = DXGI_FORMAT_R32_FLOAT;
		cv.Color[0] = 1.0f;

		HRESULT hrExp = m_pDevice->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE,
			&expDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv,
			IID_PPV_ARGS(&m_pDefaultExposureTex) );
		if ( SUCCEEDED(hrExp) )
		{
			if ( m_nDefaultExposureRTVIndex == UINT(-1) )
				m_nDefaultExposureRTVIndex = m_RTVHeap.Allocate();
			m_pDevice->CreateRenderTargetView( m_pDefaultExposureTex.Get(), nullptr,
				m_RTVHeap.GetCPUHandle( m_nDefaultExposureRTVIndex ) );
			if ( m_nDefaultExposureSRVIndex == UINT(-1) )
				m_nDefaultExposureSRVIndex = m_SRVStagingHeap.Allocate();
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Texture2D.MipLevels = 1;
			m_pDevice->CreateShaderResourceView( m_pDefaultExposureTex.Get(), &srvDesc,
				m_SRVStagingHeap.GetCPUHandle( m_nDefaultExposureSRVIndex ) );
			float white[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
			m_pCommandList->ClearRenderTargetView(
				m_RTVHeap.GetCPUHandle( m_nDefaultExposureRTVIndex ), white, 0, nullptr );
			TransitionResource( m_pCommandList.Get(), m_pDefaultExposureTex.Get(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
			m_bDefaultExposureInitialized = true;
		}
	}

	RectInt rw = renderWindow();
	float width  = (float)rw.width();
	float height = (float)rw.height();

	// Scene RT to PSR (so the edge pass can sample it).  All 3 passes leave
	// scene RT in PSR.
	if ( m_bSceneRTisRT )
	{
		TransitionResource( m_pCommandList.Get(), m_pSceneRT.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
		m_bSceneRTisRT = false;
	}

	// Common viewport / scissor / topology for all 3 passes.
	D3D12_VIEWPORT vp = { 0, 0, width, height, 0, 1 };
	D3D12_RECT scissor = { 0, 0, (LONG)width, (LONG)height };

	// SMAA CB — just rcpFrame for now.
	struct SMAACB {
		float rcpFrameX, rcpFrameY;
		float pad[2];
	};
	SMAACB cb;
	cb.rcpFrameX = 1.0f / width;
	cb.rcpFrameY = 1.0f / height;
	cb.pad[0] = cb.pad[1] = 0;
	UploadRingBuffer::Allocation cbAlloc = allocateCB( sizeof(SMAACB) );
	memcpy( cbAlloc.cpuAddress, &cb, sizeof(cb) );

	// Root sig + descriptor heap bound once for the 3 passes; PSO + SRV table
	// swap per pass.
	invalidateBoundSRVTable();
	m_pCommandList->SetGraphicsRootSignature( m_pSMAARootSig.Get() );
	ID3D12DescriptorHeap * heaps[] = { m_SRVHeap.Get(), m_SamplerHeap.Get() };
	m_pCommandList->SetDescriptorHeaps( _countof(heaps), heaps );
	m_pCommandList->SetGraphicsRootConstantBufferView( 0, cbAlloc.gpuAddress );
	m_pCommandList->SetGraphicsRootDescriptorTable( 2, m_SamplerHeap.GetGPUHandle( 0 ) );
	m_pCommandList->RSSetViewports( 1, &vp );
	m_pCommandList->RSSetScissorRects( 1, &scissor );
	m_pCommandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	m_pCommandList->IASetVertexBuffers( 0, 0, nullptr );

	// Pass 1 — Edge detection.  Input: scene RT (t0).  Output: edge RT.
	{
		TransitionResource( m_pCommandList.Get(), m_pSMAAEdgeRT.Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );
		D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RTVHeap.GetCPUHandle( m_nSMAAEdgeRTVIndex );
		m_pCommandList->OMSetRenderTargets( 1, &rtv, FALSE, nullptr );
		float black[4] = { 0, 0, 0, 0 };
		m_pCommandList->ClearRenderTargetView( rtv, black, 0, nullptr );

		UINT srvSlot = allocSRVSlots( 3 );
		m_pDevice->CopyDescriptorsSimple( 1,
			m_SRVHeap.GetCPUHandle( srvSlot ),
			m_SRVStagingHeap.GetCPUHandle( m_nSceneSRVIndex ),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		// t1 and t2 unused this pass — leave as whatever's in those slots; the
		// edge shader only reads t0 (scene) and t2 (exposure).  Wire exposure
		// at t2 in case the shader ever references it.
		UINT exposureSrc = ( m_nCurrentExposureSRVIndex != UINT(-1) )
			? m_nCurrentExposureSRVIndex : m_nDefaultExposureSRVIndex;
		if ( exposureSrc != UINT(-1) )
		{
			m_pDevice->CopyDescriptorsSimple( 1,
				m_SRVHeap.GetCPUHandle( srvSlot + 2 ),
				m_SRVStagingHeap.GetCPUHandle( exposureSrc ),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		}
		m_pCommandList->SetGraphicsRootDescriptorTable( 1, m_SRVHeap.GetGPUHandle( srvSlot ) );
		setPSO( m_pCommandList.Get(), m_pSMAAEdgePSO.Get() );
		m_pCommandList->DrawInstanced( 3, 1, 0, 0 );

		// Edge RT → PSR for Pass 2 to sample.
		TransitionResource( m_pCommandList.Get(), m_pSMAAEdgeRT.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	}

	// Pass 2 — Blend weights.  Input: edge RT (t1).  Output: weights RT.
	{
		TransitionResource( m_pCommandList.Get(), m_pSMAAWeightsRT.Get(),
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );
		D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RTVHeap.GetCPUHandle( m_nSMAAWeightsRTVIndex );
		m_pCommandList->OMSetRenderTargets( 1, &rtv, FALSE, nullptr );
		float black[4] = { 0, 0, 0, 0 };
		m_pCommandList->ClearRenderTargetView( rtv, black, 0, nullptr );

		UINT srvSlot = allocSRVSlots( 3 );
		// t0 unused; t1 = edge RT.
		m_pDevice->CopyDescriptorsSimple( 1,
			m_SRVHeap.GetCPUHandle( srvSlot + 1 ),
			m_SRVStagingHeap.GetCPUHandle( m_nSMAAEdgeSRVIndex ),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		m_pCommandList->SetGraphicsRootDescriptorTable( 1, m_SRVHeap.GetGPUHandle( srvSlot ) );
		setPSO( m_pCommandList.Get(), m_pSMAAWeightsPSO.Get() );
		m_pCommandList->DrawInstanced( 3, 1, 0, 0 );

		// Weights RT → PSR for Pass 3 to sample.
		TransitionResource( m_pCommandList.Get(), m_pSMAAWeightsRT.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	}

	// Pass 3 — Neighborhood blend.  Input: scene RT (t0) + weights RT (t1) +
	// exposure (t2).  Output: swap chain backbuffer.
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RTVHeap.GetCPUHandle( m_nFrameIndex );
		m_pCommandList->OMSetRenderTargets( 1, &rtv, FALSE, nullptr );

		UINT srvSlot = allocSRVSlots( 3 );
		m_pDevice->CopyDescriptorsSimple( 1,
			m_SRVHeap.GetCPUHandle( srvSlot ),
			m_SRVStagingHeap.GetCPUHandle( m_nSceneSRVIndex ),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		m_pDevice->CopyDescriptorsSimple( 1,
			m_SRVHeap.GetCPUHandle( srvSlot + 1 ),
			m_SRVStagingHeap.GetCPUHandle( m_nSMAAWeightsSRVIndex ),
			D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		UINT exposureSrc = ( m_nCurrentExposureSRVIndex != UINT(-1) )
			? m_nCurrentExposureSRVIndex : m_nDefaultExposureSRVIndex;
		if ( exposureSrc != UINT(-1) )
		{
			m_pDevice->CopyDescriptorsSimple( 1,
				m_SRVHeap.GetCPUHandle( srvSlot + 2 ),
				m_SRVStagingHeap.GetCPUHandle( exposureSrc ),
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		}
		m_pCommandList->SetGraphicsRootDescriptorTable( 1, m_SRVHeap.GetGPUHandle( srvSlot ) );
		setPSO( m_pCommandList.Get(), m_pSMAABlendPSO.Get() );
		m_pCommandList->DrawInstanced( 3, 1, 0, 0 );
	}

	m_bRenderingPostAA = true;
}

//---------------------------------------------------------------------------------------------------

void DisplayDeviceD3D12::enumerateTextures()
{
	m_TextureFormats.release();

	// DX12 supports all common formats
	m_TextureFormats.push( ColorFormat::RGB888 );
	m_TextureFormats.push( ColorFormat::RGB8888 );
	m_TextureFormats.push( ColorFormat::RGB888L );
	m_TextureFormats.push( ColorFormat::RGB565 );
	m_TextureFormats.push( ColorFormat::RGB555 );
	m_TextureFormats.push( ColorFormat::RGB5551 );
	m_TextureFormats.push( ColorFormat::RGB4444 );
	m_TextureFormats.push( ColorFormat::DXT1 );
	m_TextureFormats.push( ColorFormat::DXT3 );
	m_TextureFormats.push( ColorFormat::DXT5 );
	m_TextureFormats.push( ColorFormat::BC7 );
}

bool DisplayDeviceD3D12::updateClientArea( bool a_bAllowReset )
{
	// Skip the whole query/resize during an interactive window drag.  The
	// message pump still ticks during Windows' modal size-move loop, so
	// present() keeps calling updateClientArea(true) on each WM_SIZE.  We
	// leave m_ClientRectangle stale; after WM_EXITSIZEMOVE clears the flag
	// the next frame sees the mismatch and does one coalesced resize.
	if ( a_bAllowReset && sm_bResizeSuspended )
		return true;

	// Reconcile m_bWindowed with the actual window style.  setMode toggles
	// WS_POPUP (fullscreen) vs WS_OVERLAPPEDWINDOW (windowed) and flips
	// m_bWindowed in lockstep — but if a setMode call partially fails, or
	// any other path ever restyles the window without going through
	// setMode, the two can diverge.  In the divergent state the branches
	// below pick the wrong path: e.g. m_bWindowed=false (fullscreen)
	// against an actually-windowed HWND falls through the `else` branch
	// which never calls ResizeBuffers, leaving the swap chain at its old
	// fullscreen size while the window is small — content gets squished
	// and UI elements (laid out from renderWindow()/m_ClientRectangle)
	// extend past the visible window edges.
	//
	// Detect the mismatch via WS_POPUP and flip m_bWindowed to match
	// reality, then invalidate m_ClientRectangle so the windowed-branch
	// resize fires on this same call.  Cheaper and safer than re-entering
	// setMode (which would also restyle the window we just observed and
	// could re-trigger the inconsistency).
	if ( a_bAllowReset && IsWindow( m_HWND ) )
	{
		const LONG style          = GetWindowLong( m_HWND, GWL_STYLE );
		const bool bActualWindowed = ( style & WS_POPUP ) == 0;
		if ( bActualWindowed != m_bWindowed )
		{
			TRACE( "updateClientArea: m_bWindowed=%d but window style is %s — reconciling",
				m_bWindowed ? 1 : 0, bActualWindowed ? "WINDOWED" : "POPUP" );
			m_bWindowed = bActualWindowed;
			m_ClientRectangle = RectInt( 0, 0, -1, -1 );	// force resize on this pass
		}
	}

	if ( m_bWindowed )
	{
		RectInt clientWindow;
		if ( IsWindow( m_HWND ) && !IsIconic( m_HWND ) )
		{
			m_ClientPlacement.length = sizeof( m_ClientPlacement );
			GetWindowPlacement( m_HWND, &m_ClientPlacement );

			RECT rect;
			GetClientRect( m_HWND, &rect );
			ClientToScreen( m_HWND, (POINT *)&rect );
			ClientToScreen( m_HWND, ((POINT *)&rect) + 1 );

			clientWindow = RectInt( rect.left, rect.top, rect.right - 1, rect.bottom - 1 );
			if ( !clientWindow.valid() )
			{
				clientWindow = RectInt( 0, 0, 0, 0 );
				m_bMinimized = true;
			}
			else
				m_bMinimized = false;
		}
		else
			m_bMinimized = true;

		if ( clientWindow != m_ClientRectangle )
		{
			m_ClientRectangle = clientWindow;
			if ( a_bAllowReset && m_pSwapChain )
			{
				// Resize swap chain buffers — wait for all GPU work, then release
				// back buffer references.  Do NOT reset the descriptor heaps; the
				// tracked indices (m_nRTVIndices, m_nDSVIndex, etc.) are reused by
				// createRenderTargets/createDepthStencil.  Resetting the heap would
				// let future Allocate() return indices already in use.
				//
				// ResizeBuffers requires zero outstanding backbuffer references,
				// including those held by a currently-recording command list.
				// flushCommandList closes AND executes — the render commands
				// target a backbuffer that's about to be discarded (visually
				// lost, one throwaway frame), but texture-upload copies
				// queued in the same list by flushPendingUploads must execute
				// before we tear the device down.  A bare Close() without
				// Execute() dropped those uploads, leaving texture resources
				// intact but with no pixel data — post-resize sampling
				// returned zero (black-screen at ~1/3 launches; specific
				// textures missing the rest of the time).
				if ( m_bCommandListOpen )
					flushCommandList();
				waitForGPU();
				for ( UINT i = 0; i < FRAME_COUNT; i++ )
					m_pRenderTargets[i].Reset();
				m_pDepthStencil.Reset();

				UINT w = m_ClientRectangle.width() + 1;
				UINT h = m_ClientRectangle.height() + 1;
				if ( w < 1 ) w = 1;
				if ( h < 1 ) h = 1;

				HRESULT hrResize = m_pSwapChain->ResizeBuffers( FRAME_COUNT, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0 );
				if ( FAILED( hrResize ) )
				{
					HRESULT removed = m_pDevice ? m_pDevice->GetDeviceRemovedReason() : S_OK;
					TRACE( "updateClientArea: ResizeBuffers failed hr=0x%08x, DeviceRemovedReason=0x%08x  %ux%u",
						hrResize, removed, w, h );
					// Force a re-resize next frame by invalidating the cached
					// rectangle.  If the failure was transient (GPU still
					// draining previous work) the next present will retry; if
					// it's DEVICE_REMOVED there's nothing sane to do here but
					// at least we've logged it.
					m_ClientRectangle = RectInt( 0, 0, -1, -1 );
					return false;
				}
				m_nFrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

				if ( !createRenderTargets() )
					TRACE( "updateClientArea: createRenderTargets failed after resize to %ux%u", w, h );
				if ( !createDepthStencil() )
					TRACE( "updateClientArea: createDepthStencil failed after resize to %ux%u", w, h );

				// Recreate scene RT + AA PSO (needed by FXAA, HDR, SSAO effects)
				if ( m_bSceneRTEnabled )
				{
					if ( !createFXAA() )
						TRACE( "updateClientArea: createFXAA failed after resize to %ux%u", w, h );
				}
			}
		}
	}
	else
	{
		// Borderless fullscreen: the window IS a real HWND (WS_POPUP) sized
		// to the monitor — use its actual client rect.  Do NOT use
		// m_Mode.screenSize: that's whatever mode the user selected from the
		// Mode list and may not match the monitor the window landed on, so
		// relying on it clobbers the monitor-bounds rect that setMode set
		// and squishes the whole scene into the upper-left of the backbuffer.
		if ( IsWindow( m_HWND ) && !IsIconic( m_HWND ) )
		{
			RECT rect;
			GetClientRect( m_HWND, &rect );
			ClientToScreen( m_HWND, (POINT *)&rect );
			ClientToScreen( m_HWND, ((POINT *)&rect) + 1 );

			RectInt clientWindow = RectInt( rect.left, rect.top,
				rect.right - 1, rect.bottom - 1 );
			if ( clientWindow.valid() )
			{
				m_ClientRectangle = clientWindow;
				m_bMinimized = false;
			}
			else
			{
				m_bMinimized = true;
			}
		}
		else
		{
			m_bMinimized = true;
		}
	}
	return true;
}

//---------------------------------------------------------------------------------------------------
// Static
//---------------------------------------------------------------------------------------------------

void DisplayDeviceD3D12::enumerateModes()
{
	if ( sm_ModeList.size() > 0 )
		return;

	ComPtr<IDXGIFactory4> factory;
	if ( FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) )
		return;

	ComPtr<IDXGIAdapter1> adapter;
	for ( UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i )
	{
		DXGI_ADAPTER_DESC1 adapterDesc;
		adapter->GetDesc1( &adapterDesc );

		if ( adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE )
			continue;

		ComPtr<IDXGIOutput> output;
		if ( FAILED(adapter->EnumOutputs(0, &output)) )
			continue;

		UINT numModes = 0;
		output->GetDisplayModeList( DXGI_FORMAT_R8G8B8A8_UNORM, 0, &numModes, nullptr );
		if ( numModes == 0 )
			continue;

		std::vector<DXGI_MODE_DESC> modeDescs( numModes );
		output->GetDisplayModeList( DXGI_FORMAT_R8G8B8A8_UNORM, 0, &numModes, modeDescs.data() );

		char name[256];
		wcstombs( name, adapterDesc.Description, 256 );

		for ( UINT j = 0; j < numModes; ++j )
		{
			const DXGI_MODE_DESC & md = modeDescs[j];

			// Skip duplicate resolutions (different refresh rates)
			bool bFound = false;
			for ( int k = 0; k < sm_ModeList.size(); ++k )
			{
				if ( sm_ModeList[k].screenSize.width == (int)md.Width &&
					 sm_ModeList[k].screenSize.height == (int)md.Height )
				{
					bFound = true;
					break;
				}
			}
			if ( bFound )
				continue;

			Mode & mode = sm_ModeList.push();
			mode.deviceID = (void *)(intptr_t)i;
			mode.screenSize = SizeInt( md.Width, md.Height );
			mode.colorFormat = ColorFormat::RGB8888;
			// Format as "WxH" (e.g. "1920x1080") — short enough to fit the
			// 300px Options listbox column.  Previous format prepended the
			// adapter name ("NVIDIA GeForce RTX 5080 - 1920x1080") which
			// overflowed the column on modern long device names.  The
			// startup match in PlatformWin.cpp does substring + exact-match,
			// both of which work fine with the short form.
			mode.modeDescription = CharString().format( "%dx%d", md.Width, md.Height );
		}
	}
}

ColorFormat::Format DisplayDeviceD3D12::findFormat( DXGI_FORMAT format )
{
	switch ( format )
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:	return ColorFormat::RGB8888;
	case DXGI_FORMAT_B8G8R8X8_UNORM:	return ColorFormat::RGB888L;
	case DXGI_FORMAT_B5G6R5_UNORM:		return ColorFormat::RGB565;
	case DXGI_FORMAT_B5G5R5A1_UNORM:	return ColorFormat::RGB5551;
	case DXGI_FORMAT_B4G4R4A4_UNORM:	return ColorFormat::RGB4444;
	case DXGI_FORMAT_BC1_UNORM:			return ColorFormat::DXT1;
	case DXGI_FORMAT_BC2_UNORM:			return ColorFormat::DXT3;
	case DXGI_FORMAT_BC3_UNORM:			return ColorFormat::DXT5;
	case DXGI_FORMAT_BC7_UNORM:			return ColorFormat::BC7;
	default:							return ColorFormat::RGB8888;
	}
}

DXGI_FORMAT DisplayDeviceD3D12::findFormat( ColorFormat::Format format )
{
	switch ( format )
	{
	case ColorFormat::RGB888:	return DXGI_FORMAT_B8G8R8X8_UNORM;
	case ColorFormat::RGB8888:	return DXGI_FORMAT_R8G8B8A8_UNORM;
	case ColorFormat::RGB888L:	return DXGI_FORMAT_B8G8R8X8_UNORM;
	case ColorFormat::RGB565:	return DXGI_FORMAT_B5G6R5_UNORM;
	case ColorFormat::RGB555:	return DXGI_FORMAT_B5G5R5A1_UNORM;
	case ColorFormat::RGB5551:	return DXGI_FORMAT_B5G5R5A1_UNORM;
	case ColorFormat::RGB4444:	return DXGI_FORMAT_B4G4R4A4_UNORM;
	case ColorFormat::DXT1:		return DXGI_FORMAT_BC1_UNORM;
	case ColorFormat::DXT3:		return DXGI_FORMAT_BC2_UNORM;
	case ColorFormat::DXT5:		return DXGI_FORMAT_BC3_UNORM;
	case ColorFormat::BC7:		return DXGI_FORMAT_BC7_UNORM;
	default:					return DXGI_FORMAT_R8G8B8A8_UNORM;
	}
}

void DisplayDeviceD3D12::setXMMatrix( XMMATRIX & m, const Matrix33 & mRotation, const Vector3 & vOffset )
{
	m = XMMATRIX(
		mRotation.i.x, mRotation.i.y, mRotation.i.z, 0.0f,
		mRotation.j.x, mRotation.j.y, mRotation.j.z, 0.0f,
		mRotation.k.x, mRotation.k.y, mRotation.k.z, 0.0f,
		vOffset.x, vOffset.y, vOffset.z, 1.0f
	);
}

//------------------------------------------------------------------------------------
// EOF
