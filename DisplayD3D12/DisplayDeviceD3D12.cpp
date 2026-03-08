/*
	DisplayDeviceD3D12.cpp
	DirectX 12 Display Device Implementation
	(c)2024 Palestar
*/

//#define PROFILE_OFF
#define MEDUSA_TRACE_ON

#include "Debug/Trace.h"
#include "Debug/Assert.h"
#include "Debug/Profile.h"
#include "File/FileDisk.h"
#include "File/Path.h"
#include "Math/Constants.h"
#include "Math/Plane.h"
#include "Math/SphericalHull.h"
#include "Standard/Limits.h"
#include "Draw/ImageCodec.h"

#include "Display/Types.h"
#include "DisplayDeviceD3D12.h"
#include "PrimitiveFactory.h"
#include "PrimitiveSurfaceD3D12.h"
#include "PrimitiveMaterialD3D12.h"
#include "PrimitiveWindowD3D12.h"
#include "DisplayEffectHDR.h"
#include "DisplayEffectBlur.h"

#include <math.h>

//---------------------------------------------------------------------------------------------------

DisplayDeviceD3D12::ModeList	DisplayDeviceD3D12::sm_ModeList;
DisplayDeviceD3D12::DeviceList	DisplayDeviceD3D12::sm_DeviceList;

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
	m_bRenderingShadowMap( false ),
	m_bShadowMapInRTState( false ),
	m_bFirstShadowPass( true ),
	m_vShadowFocus( 0, 0, 1.0f ),
	m_fShadowRadius( DEFAULT_SHADOW_RADIUS ),
	m_szShadowMap( DEFAULT_SHADOW_MAP_SIZE, DEFAULT_SHADOW_MAP_SIZE ),
	m_nSceneRTVIndex( UINT(-1) ),
	m_nSceneSRVIndex( UINT(-1) ),
	m_bFXAAEnabled( true ),
	m_nShadowMapDSVIndex( UINT(-1) ),
	m_nShadowMapSRVStagingIndex( UINT(-1) )
{
	TRACE( "DisplayDeviceD3D12 created!" );

	memset( m_nFenceValues, 0, sizeof(m_nFenceValues) );

	m_mView = XMMatrixIdentity();
	m_mProj = XMMatrixIdentity();
	m_mCurrentWorld = XMMatrixIdentity();

	lock();
	enumerateModes();
	unlock();

	// Register supported effects
	registerEffect( "HDR", DisplayEffectHDRD3D12::staticFactory() );
	registerEffect( "BLUR", DisplayEffectBlurD3D12::staticFactory() );
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
	if ( m_bWindowed )
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

	if ( pMode != NULL )
		m_Mode = *pMode;
	m_bWindowed = bWindowed;

	// Resize swap chain
	waitForGPU();

	for ( UINT i = 0; i < FRAME_COUNT; i++ )
		m_pRenderTargets[i].Reset();
	m_pDepthStencil.Reset();

	if ( m_pSwapChain )
	{
		HRESULT hr = m_pSwapChain->ResizeBuffers( FRAME_COUNT,
			m_Mode.screenSize.width, m_Mode.screenSize.height,
			DXGI_FORMAT_R8G8B8A8_UNORM, 0 );
		if ( FAILED(hr) )
			return false;
	}

	createRenderTargets();
	createDepthStencil();
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
	static int s_nAmbLog = 0;
	if ( s_nAmbLog < 10 )
	{
		TRACE( "setAmbient: color=(%d,%d,%d,%d)", (int)nColor.m_R, (int)nColor.m_G, (int)nColor.m_B, (int)nColor.m_A );
		++s_nAmbLog;
	}
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

	static int s_nDirLog = 0;
	if ( s_nDirLog < 10 )
	{
		TRACE( "addDirectionalLight: priority=%d, color=(%d,%d,%d), dir=(%.2f,%.2f,%.2f), totalLights=%d",
			nPriority, (int)nColor.r, (int)nColor.g, (int)nColor.b,
			vDirection.x, vDirection.y, vDirection.z, (int)m_Lights.size() );
		++s_nDirLog;
	}

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
	// Don't open the command list when minimized — present() will still advance the frame
	if ( m_bMinimized )
		return false;

	m_pCurrentTransform = NULL;
	m_pCurrentMaterial = NULL;

	// Reset shadow state for each scene — prevents stale shadow data
	// when views that don't use shadows (e.g. engineering view) render
	m_bFirstShadowPass = true;
	m_nShadowMapPass = 0;
	m_ShadowPassList.clear();

	if ( !m_bCommandListOpen )
	{

		resetCommandList();

		// Transition the render target from PRESENT to RENDER_TARGET
		TransitionResource( m_pCommandList.Get(), m_pRenderTargets[m_nFrameIndex].Get(),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET );

		// FLIP_DISCARD leaves back buffer contents undefined after Present.
		// Always clear both the render target and depth buffer at frame start
		// to prevent ghosting from previous frames.
		{
			D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RTVHeap.GetCPUHandle( m_nFrameIndex );
			float black[4] = { 0, 0, 0, 1 };
			m_pCommandList->ClearRenderTargetView( rtv, black, 0, nullptr );

			// Also clear the FXAA scene RT if enabled
			if ( m_bFXAAEnabled && m_pSceneRT )
			{
				// Transition scene RT to render target for clearing
				TransitionResource( m_pCommandList.Get(), m_pSceneRT.Get(),
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );
				D3D12_CPU_DESCRIPTOR_HANDLE sceneRTV = m_RTVHeap.GetCPUHandle( m_nSceneRTVIndex );
				m_pCommandList->ClearRenderTargetView( sceneRTV, black, 0, nullptr );
			}

			if ( m_pDepthStencil )
			{
				D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_DSVHeap.GetCPUHandle( 0 );
				m_pCommandList->ClearDepthStencilView( dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr );
			}
		}

		// Reset per-frame dynamic buffers only once per frame
		m_DynamicVB[m_nFrameIndex].Reset();
		m_DynamicCB[m_nFrameIndex].Reset();

		// Release primitives deferred from the previous use of this frame slot.
		// moveToNextFrame() already waited on the fence, so the GPU is done.
		m_DeferredPrimitives[m_nFrameIndex].release();

		// Reset per-frame SRV ring allocator (slots 0-7 are permanent null SRVs)
		m_nSRVFrameOffset = 8;
		m_nSRVTextureBase = 8;

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

		// Set root signature and descriptor heaps once per frame
		m_pCommandList->SetGraphicsRootSignature( m_pRootSignature.Get() );
		ID3D12DescriptorHeap * heaps[] = { m_SRVHeap.Get(), m_SamplerHeap.Get() };
		m_pCommandList->SetDescriptorHeaps( _countof(heaps), heaps );

		// Bind descriptor tables for SRVs [4] and samplers [5]
		m_pCommandList->SetGraphicsRootDescriptorTable( 4, m_SRVHeap.GetGPUHandle( 0 ) );
		m_pCommandList->SetGraphicsRootDescriptorTable( 5, m_SamplerHeap.GetGPUHandle( 0 ) );

		// Bind the per-frame constant buffer
		bindPerFrameCB();

		// Bind default per-object, per-material, and per-light CBs so that all
		// root parameters are valid before any draw call.  Without this, the GPU
		// reads garbage if a draw fires before the material/transform sets them.
		{
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
		}
	}
	// else: sub-render call — command list is already open, reuse it

	// Set render targets (must be set for each sub-render as shadow passes change them)
	// When FXAA is enabled, render to the intermediate scene RT instead of the swap chain
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	if ( m_bFXAAEnabled && m_pSceneRT )
		rtvHandle = m_RTVHeap.GetCPUHandle( m_nSceneRTVIndex );
	else
		rtvHandle = m_RTVHeap.GetCPUHandle( m_nFrameIndex );
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

		for ( int i = 0; i < pass.m_Primitives.size(); ++i )
			pass.m_Primitives[i]->execute();

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
		if ( m_bFXAAEnabled && m_pSceneRT )
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

	// Execute all material stacks in order
	for ( int i = 0; i < PASS_COUNT; ++i )
	{
		Array< PrimitiveMaterial::Ref > & materials = m_Stack[i];
		for ( int j = 0; j < materials.size(); ++j )
		{
			PrimitiveMaterial * pMaterial = materials[j];
			pMaterial->execute();
			pMaterial->clear();
		}
		materials.release();
	}

	// Post-render effects (e.g. bloom, blur — processed in reverse order)
	for ( EffectList::reverse_iterator iEffect = m_EffectList.rbegin();
		iEffect != m_EffectList.rend(); ++iEffect )
	{
		DisplayEffect * pEffect = *iEffect;
		if ( !pEffect->postRender( this ) )
			TRACE( "ERROR: Effect postRender() returned false!" );
	}
	m_EffectList.clear();

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
		// Apply FXAA: resolve scene RT → swap chain back buffer
		if ( m_bFXAAEnabled && m_pSceneRT )
			applyFXAA();

		// Transition render target to present state and close the command list
		TransitionResource( m_pCommandList.Get(), m_pRenderTargets[m_nFrameIndex].Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT );
		flushCommandList();
	}

	// Present the frame
	if ( m_pSwapChain )
	{
		UINT syncInterval = sm_bWaitVB ? 1 : 0;
		m_pSwapChain->Present( syncInterval, 0 );
	}

	moveToNextFrame();
	updateClientArea( true );

	PROFILE_END();
}

//---------------------------------------------------------------------------------------------------

DevicePrimitive * DisplayDeviceD3D12::create( const PrimitiveKey & key )
{
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

	if ( pPrimitive->primitiveKey() == PrimitiveMaterial::staticPrimitiveKey() )
	{
		m_pCurrentMaterial = (PrimitiveMaterial *)pPrimitive;

		if ( !m_bShadowPass )
		{
			PrimitiveMaterialD3D12 * pMaterial = (PrimitiveMaterialD3D12 *)pPrimitive;
			if ( !pMaterial->m_bPushed )
			{
				m_Stack[ pMaterial->m_nPass ].push( pMaterial );
				pMaterial->m_bPushed = true;
			}

			if ( m_pCurrentTransform.valid() )
				pMaterial->addChild( m_pCurrentTransform );
		}
		else
		{
			ShadowPass & pass = *m_iCurrentShadowPass;
			if ( m_pCurrentTransform.valid() && (pass.m_Primitives.size() == 0 || pass.m_Primitives.last() != m_pCurrentTransform) )
				pass.m_Primitives.push( (DevicePrimitive *)m_pCurrentTransform );
		}
	}
	else
	{
		if ( pPrimitive->primitiveKey() == PrimitiveSetTransform::staticPrimitiveKey() )
			m_pCurrentTransform = (PrimitiveSetTransform *)pPrimitive;

		if ( !m_bShadowPass )
		{
			if ( m_pCurrentMaterial.valid() )
				m_pCurrentMaterial->addChild( pPrimitive );
		}
		else
		{
			ShadowPass & pass = *m_iCurrentShadowPass;
			if ( !m_pCurrentMaterial.valid() || m_pCurrentMaterial->pass() == PRIMARY )
				pass.m_Primitives.push( pPrimitive );
		}
	}

	return true;
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
	key.rtvFormat = m_bRenderingShadowMap ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;

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
			key.depthWrite = (m_nCurrentBlend == 0);
			key.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		}
		pShader = m_pMatShader.valid() ? m_pMatShader.pointer() : nullptr;
	}

	// Resolve fallback shader if no explicit shader set (same logic as getOrCreatePSO)
	if ( !pShader || !pShader->valid() )
	{
		switch ( inputLayout )
		{
		case PSOKey::IL_VERTEX:
			if ( m_pPassThroughShader.valid() )  pShader = m_pPassThroughShader.pointer();
			break;
		case PSOKey::IL_VERTEXL:
			if ( m_pPassThroughLShader.valid() )  pShader = m_pPassThroughLShader.pointer();
			break;
		case PSOKey::IL_VERTEXTL:
			if ( m_pPassThroughTLShader.valid() ) pShader = m_pPassThroughTLShader.pointer();
			break;
		}
	}

	// Encode the effective shader bytecode pointers into the key so different
	// shaders produce distinct PSO cache entries.
	if ( pShader && pShader->valid() )
	{
		key.vsBytecode = pShader->vertexShaderBlob() ? pShader->vertexShaderBlob()->GetBufferPointer() : nullptr;
		key.psBytecode = pShader->pixelShaderBlob() ? pShader->pixelShaderBlob()->GetBufferPointer() : nullptr;
	}

	ID3D12PipelineState * pPSO = getOrCreatePSO( key, pShader );
	if ( pPSO )
		m_pCommandList->SetPipelineState( pPSO );

}

//---------------------------------------------------------------------------------------------------

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
		ShaderD3D12 * pFallback = nullptr;
		switch ( key.inputLayout )
		{
		case PSOKey::IL_VERTEX:
			if ( m_pPassThroughShader.valid() )  pFallback = m_pPassThroughShader.pointer();
			break;
		case PSOKey::IL_VERTEXL:
			if ( m_pPassThroughLShader.valid() )  pFallback = m_pPassThroughLShader.pointer();
			break;
		case PSOKey::IL_VERTEXTL:
			if ( m_pPassThroughTLShader.valid() ) pFallback = m_pPassThroughTLShader.pointer();
			break;
		}
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
	HRESULT hr = m_pDevice->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS(&pso) );
	if ( FAILED(hr) )
	{
		TRACE( "DisplayDeviceD3D12::getOrCreatePSO() - Failed to create PSO!" );
		return nullptr;
	}

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

	const float inv = 1.0f / 255.0f;
	m_CBPerFrame.vGlobalAmbient = ShaderFloat4(
		m_cAmbientLight.m_R * inv, m_cAmbientLight.m_G * inv,
		m_cAmbientLight.m_B * inv, m_cAmbientLight.m_A * inv );
	m_CBPerFrame.szShadowMap = ShaderFloat2( (float)m_szShadowMap.width, (float)m_szShadowMap.height );
	m_CBPerFrame.fShadowDistance = m_fShadowRadius;
	m_CBPerFrame.fShadowDepthRange = m_fShadowDepthRange;

	// Compute world-space shadow focus for cascade selection in shaders
	Vector3 vWorldFocus( m_Proj.m_vPosition + (m_Proj.m_mFrame % m_vShadowFocus) );
	m_CBPerFrame.vShadowFocus = ShaderFloat4( vWorldFocus.x, vWorldFocus.y, vWorldFocus.z, 0.0f );

	UploadRingBuffer::Allocation alloc = allocateCB( sizeof(CBPerFrame) );
	memcpy( alloc.cpuAddress, &m_CBPerFrame, sizeof(CBPerFrame) );

	// Root parameter 0 = CBV for per-frame constants
	m_pCommandList->SetGraphicsRootConstantBufferView( 0, alloc.gpuAddress );
}

void DisplayDeviceD3D12::bindPerObjectCB()
{
	if ( !m_bCommandListOpen )
		return;

	UploadRingBuffer::Allocation alloc = allocateCB( sizeof(CBPerObject) );
	memcpy( alloc.cpuAddress, &m_CBPerObject, sizeof(CBPerObject) );

	// Root parameter 1 = CBV for per-object constants
	m_pCommandList->SetGraphicsRootConstantBufferView( 1, alloc.gpuAddress );
}

void DisplayDeviceD3D12::bindPerMaterialCB( const CBPerMaterial & mat )
{
	if ( !m_bCommandListOpen )
		return;

	UploadRingBuffer::Allocation alloc = allocateCB( sizeof(CBPerMaterial) );
	memcpy( alloc.cpuAddress, &mat, sizeof(CBPerMaterial) );

	// Root parameter 2 = CBV for per-material constants
	m_pCommandList->SetGraphicsRootConstantBufferView( 2, alloc.gpuAddress );
}

void DisplayDeviceD3D12::bindPerLightCB( const CBPerLight & light )
{
	if ( !m_bCommandListOpen )
		return;

	UploadRingBuffer::Allocation alloc = allocateCB( sizeof(CBPerLight) );
	memcpy( alloc.cpuAddress, &light, sizeof(CBPerLight) );

	// Root parameter 3 = CBV for per-light constants
	m_pCommandList->SetGraphicsRootConstantBufferView( 3, alloc.gpuAddress );
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

#if defined(_DEBUG)
	// Enable debug layer
	{
		ComPtr<ID3D12Debug> debugController;
		if ( SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))) )
			debugController->EnableDebugLayer();
	}
#endif

	// Create DXGI factory
	UINT dxgiFlags = 0;
#if defined(_DEBUG)
	dxgiFlags = DXGI_CREATE_FACTORY_DEBUG;
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
	if ( !m_SRVHeap.Create(m_pDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, MAX_SRV_DESCRIPTORS, true) )
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
		// Initialize ALL slots so per-frame SRV ring never has uninitialized descriptors
		for ( UINT i = 0; i < MAX_SRV_DESCRIPTORS; i++ )
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
	}

	// Enumerate supported texture formats
	enumerateTextures();

	// Get texture caps
	m_TextureP2 = false;	// DX12 doesn't require P2
	m_TextureSquare = false;

	D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
	m_pDevice->CheckFeatureSupport( D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options) );

	// Create FXAA post-process resources
	if ( m_bFXAAEnabled )
		createFXAA();

	updateClientArea( false );

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
	for ( UINT i = 0; i < FRAME_COUNT; i++ )
	{
		if ( FAILED(m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_pRenderTargets[i]))) )
			return false;

		UINT rtvIndex = m_RTVHeap.Allocate();
		m_pDevice->CreateRenderTargetView( m_pRenderTargets[i].Get(), nullptr, m_RTVHeap.GetCPUHandle(rtvIndex) );
	}
	return true;
}

bool DisplayDeviceD3D12::createDepthStencil()
{
	DXGI_SWAP_CHAIN_DESC1 desc;
	m_pSwapChain->GetDesc1( &desc );

	D3D12_RESOURCE_DESC dsDesc = {};
	dsDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	dsDesc.Width = desc.Width;
	dsDesc.Height = desc.Height;
	dsDesc.DepthOrArraySize = 1;
	dsDesc.MipLevels = 1;
	dsDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
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

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	UINT dsvIndex = m_DSVHeap.Allocate();
	m_pDevice->CreateDepthStencilView( m_pDepthStencil.Get(), &dsvDesc, m_DSVHeap.GetCPUHandle(dsvIndex) );

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

	return true;
}

void DisplayDeviceD3D12::freeD3D12()
{
	waitForGPU();

	// Release effects
	for ( WeakEffectList::iterator iEffect = m_CreatedEffects.begin();
		iEffect != m_CreatedEffects.end(); ++iEffect )
	{
		DisplayEffect * pEffect = *iEffect;
		if ( pEffect != NULL )
			pEffect->release();
	}
	m_CreatedEffects.clear();

	releaseShaders();

	m_ShadowPassList.clear();
	m_bShadowMapReady = false;
	m_pShadowMapDepth.Reset();

	m_PSOCache.clear();

	// FXAA cleanup
	m_pSceneRT.Reset();
	m_pFXAAPSO.Reset();
	m_pFXAARootSig.Reset();
	m_pFXAAShader = NULL;

	for ( UINT i = 0; i < FRAME_COUNT; i++ )
	{
		m_DeferredPrimitives[i].release();
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
	m_pCommandQueue->Signal( m_pFence.Get(), fence );
	m_nFenceValues[m_nFrameIndex]++;

	if ( m_pFence->GetCompletedValue() < fence )
	{
		m_pFence->SetEventOnCompletion( fence, m_hFenceEvent );
		WaitForSingleObject( m_hFenceEvent, INFINITE );
	}
}

void DisplayDeviceD3D12::moveToNextFrame()
{
	const UINT64 currentFenceValue = m_nFenceValues[m_nFrameIndex];
	m_pCommandQueue->Signal( m_pFence.Get(), currentFenceValue );

	m_nFrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

	if ( m_pFence->GetCompletedValue() < m_nFenceValues[m_nFrameIndex] )
	{
		m_pFence->SetEventOnCompletion( m_nFenceValues[m_nFrameIndex], m_hFenceEvent );
		DWORD result = WaitForSingleObject( m_hFenceEvent, 5000 );
		if ( result == WAIT_TIMEOUT )
		{
			HRESULT reason = m_pDevice ? m_pDevice->GetDeviceRemovedReason() : S_OK;
			TRACE( CharString().format( "DisplayDeviceD3D12::moveToNextFrame() - GPU fence timeout! DeviceRemovedReason=0x%08X", reason ) );
		}
	}

	m_nFenceValues[m_nFrameIndex] = currentFenceValue + 1;
}

void DisplayDeviceD3D12::flushCommandList()
{
	if ( !m_pCommandList || !m_bCommandListOpen )
		return;

	m_bCommandListOpen = false;
	m_pCommandList->Close();
	ID3D12CommandList * ppCommandLists[] = { m_pCommandList.Get() };
	m_pCommandQueue->ExecuteCommandLists( _countof(ppCommandLists), ppCommandLists );
}

void DisplayDeviceD3D12::resetCommandList()
{
	// Caller is responsible for ensuring the command list is not open before calling this.
	// beginScene() only calls this when !m_bCommandListOpen (first call per frame).
	m_pCommandAllocators[m_nFrameIndex]->Reset();
	m_pCommandList->Reset( m_pCommandAllocators[m_nFrameIndex].Get(), nullptr );
	m_bCommandListOpen = true;
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

	// Release old resources if resizing
	m_pSceneRT.Reset();
	m_pFXAAPSO.Reset();
	m_pFXAARootSig.Reset();

	// Create intermediate render target (same format as swap chain)
	D3D12_RESOURCE_DESC rtDesc = {};
	rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rtDesc.Width = width;
	rtDesc.Height = height;
	rtDesc.DepthOrArraySize = 1;
	rtDesc.MipLevels = 1;
	rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	rtDesc.SampleDesc.Count = 1;
	rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	HRESULT hr = m_pDevice->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
		IID_PPV_ARGS(&m_pSceneRT) );
	if ( FAILED(hr) )
	{
		TRACE( "createFXAA: Failed to create scene render target!" );
		m_bFXAAEnabled = false;
		return false;
	}

	// Create RTV for scene RT (use slot after the swap chain RTVs)
	if ( m_nSceneRTVIndex == UINT(-1) )
		m_nSceneRTVIndex = FRAME_COUNT;		// slots 0..FRAME_COUNT-1 are swap chain
	m_pDevice->CreateRenderTargetView( m_pSceneRT.Get(), nullptr,
		m_RTVHeap.GetCPUHandle( m_nSceneRTVIndex ) );

	// Create SRV for scene RT in the shader-visible heap
	if ( m_nSceneSRVIndex == UINT(-1) )
		m_nSceneSRVIndex = m_SRVStagingHeap.Allocate();

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	m_pDevice->CreateShaderResourceView( m_pSceneRT.Get(), &srvDesc,
		m_SRVStagingHeap.GetCPUHandle( m_nSceneSRVIndex ) );

	// Load FXAA shader
	m_pFXAAShader = getShader( "Shaders/FXAA.hlsl" );
	if ( !m_pFXAAShader.valid() || !m_pFXAAShader->valid() )
	{
		TRACE( "createFXAA: Failed to compile FXAA shader!" );
		m_bFXAAEnabled = false;
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

	D3D12_DESCRIPTOR_RANGE srvRange = {};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 1;
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
		m_bFXAAEnabled = false;
		return false;
	}

	hr = m_pDevice->CreateRootSignature( 0, sig->GetBufferPointer(), sig->GetBufferSize(),
		IID_PPV_ARGS(&m_pFXAARootSig) );
	if ( FAILED(hr) )
	{
		m_bFXAAEnabled = false;
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
		m_bFXAAEnabled = false;
		return false;
	}

	TRACE( "FXAA initialized (%dx%d)", width, height );
	return true;
}

void DisplayDeviceD3D12::applyFXAA()
{
	if ( !m_bFXAAEnabled || !m_pSceneRT || !m_pFXAAPSO || !m_bCommandListOpen )
		return;

	RectInt rw = renderWindow();
	float width  = (float)rw.width();
	float height = (float)rw.height();

	// Transition scene RT from render target → shader resource
	TransitionResource( m_pCommandList.Get(), m_pSceneRT.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );

	// Set the swap chain back buffer as render target
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_RTVHeap.GetCPUHandle( m_nFrameIndex );
	m_pCommandList->OMSetRenderTargets( 1, &rtv, FALSE, nullptr );

	// Set viewport and scissor for fullscreen
	D3D12_VIEWPORT vp = { 0, 0, width, height, 0, 1 };
	m_pCommandList->RSSetViewports( 1, &vp );
	D3D12_RECT scissor = { 0, 0, (LONG)width, (LONG)height };
	m_pCommandList->RSSetScissorRects( 1, &scissor );

	// Set FXAA pipeline
	m_pCommandList->SetGraphicsRootSignature( m_pFXAARootSig.Get() );
	m_pCommandList->SetPipelineState( m_pFXAAPSO.Get() );

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

	// Copy scene SRV to a slot in the shader-visible SRV heap
	UINT fxaaSRVSlot = m_nSRVFrameOffset;
	m_nSRVFrameOffset++;
	m_pDevice->CopyDescriptorsSimple( 1,
		m_SRVHeap.GetCPUHandle( fxaaSRVSlot ),
		m_SRVStagingHeap.GetCPUHandle( m_nSceneSRVIndex ),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

	m_pCommandList->SetGraphicsRootDescriptorTable( 1, m_SRVHeap.GetGPUHandle( fxaaSRVSlot ) );
	m_pCommandList->SetGraphicsRootDescriptorTable( 2, m_SamplerHeap.GetGPUHandle( 0 ) );

	// Draw fullscreen triangle (3 vertices, no vertex buffer — generated in VS)
	m_pCommandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	m_pCommandList->IASetVertexBuffers( 0, 0, nullptr );
	m_pCommandList->DrawInstanced( 3, 1, 0, 0 );

	// Transition scene RT back for next frame
	TransitionResource( m_pCommandList.Get(), m_pSceneRT.Get(),
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET );
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
}

bool DisplayDeviceD3D12::updateClientArea( bool a_bAllowReset )
{
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
				// Resize swap chain buffers
				waitForGPU();
				for ( UINT i = 0; i < FRAME_COUNT; i++ )
					m_pRenderTargets[i].Reset();
				m_pDepthStencil.Reset();
				m_RTVHeap.Reset();
				m_DSVHeap.Reset();

				UINT w = m_ClientRectangle.width() + 1;
				UINT h = m_ClientRectangle.height() + 1;
				if ( w < 1 ) w = 1;
				if ( h < 1 ) h = 1;

				m_pSwapChain->ResizeBuffers( FRAME_COUNT, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0 );
				m_nFrameIndex = m_pSwapChain->GetCurrentBackBufferIndex();

				createRenderTargets();
				createDepthStencil();
			}
		}
	}
	else
	{
		m_ClientRectangle = RectInt( 0, 0, m_Mode.screenSize );
		if ( IsWindow( m_HWND ) && !IsIconic( m_HWND ) )
			m_bMinimized = false;
		else
			m_bMinimized = true;
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
			mode.modeDescription = CharString().format( "%s - %dx%d",
				name, md.Width, md.Height );
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
