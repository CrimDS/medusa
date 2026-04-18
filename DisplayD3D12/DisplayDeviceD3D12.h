/*
	DisplayDeviceD3D12.h
	DirectX 12 Display Device Implementation
	(c)2024 Palestar
*/

#ifndef DISPLAY_DEVICE_D3D12_H
#define DISPLAY_DEVICE_D3D12_H

#include "Display/DisplayDevice.h"
#include "Display/PrimitiveMaterial.h"
#include "Display/PrimitiveSetTransform.h"
#include "Standard/Array.h"
#include "Standard/List.h"
#include "Standard/CriticalSection.h"
#include <functional>

#include "D3D12Helpers.h"
#include "ShaderD3D12.h"

//---------------------------------------------------------------------------------------------------

#define ENABLE_SHADOW_MAP_DEBUG		0
#define DEBUG_SHADOW_MAP_SIZE		256

//----------------------------------------------------------------------------

class PrimitiveSurfaceD3D12; 		// forward declare

__declspec(align(16)) class DisplayDeviceD3D12 : public DisplayDevice
{
public:
	DECLARE_WIDGET_CLASS();

	// Constants
	enum {
		PRIMITIVE_STACK_SIZE	= 1024 * 8,
		DYNAMIC_VB_SIZE			= 1024 * 1024 * 64, 		// 64MB ring buffer per frame
		DYNAMIC_CB_SIZE			= 1024 * 1024 * 8, 		// 8MB for constant buffers per frame
		MAX_SRV_DESCRIPTORS		= 4096,
		MAX_SAMPLER_DESCRIPTORS	= 64,
		MAX_RTV_DESCRIPTORS		= 32,
		MAX_DSV_DESCRIPTORS		= 8,
	};

	// Types
	typedef Reference<DisplayDeviceD3D12>		Ref;
	typedef PrimitiveMaterial::Blending		Blending;
	typedef Array<DisplayDevice::Mode>		ModeList;
	typedef Array<DisplayDeviceD3D12 *> 		DeviceList;
	typedef Array<ColorFormat::Format>		TextureFormat;
	typedef Hash< int, Array< PrimitiveMaterial::Ref > >	StackHash;

	// Construction
	DisplayDeviceD3D12();
	virtual ~DisplayDeviceD3D12();

	// DisplayDevice interface - Accessors
	virtual bool					isLocked() const;
	virtual int						modeCount() const;
	virtual const Mode *			mode(int n) const;
	virtual ColorFormat *			primaryFormat() const;
	virtual int						surfaceFormatCount() const;
	virtual ColorFormat::Format		surfaceFormat(int n) const;
	virtual const Mode *			activeMode() const;
	virtual bool					windowed() const;
	virtual RectInt					clientWindow() const;
	virtual RectInt					renderWindow() const;
	virtual bool					textureP2() const;
	virtual bool					textureSquare() const;
	virtual SizeInt					textureMaxSize() const;
	virtual SizeInt					textureMinSize() const;
	virtual float					pixelShaderVersion() const;
	virtual float					vertexShaderVersion() const;
	virtual FilterMode				filterMode() const;
	virtual dword					totalVideoMemory() const;
	virtual dword					freeVideoMemory() const;

	// DisplayDevice interface - Mutators
	virtual void					lock();
	virtual void					unlock();
	virtual bool					initialize( void *hWnd, const Mode * pMode,
										bool bWindowed, bool bHardware, FSAA eFSAA = FSAA_NONE );
	virtual bool					setMode( const Mode * pMode, bool bWindowed );
	virtual void					setFilterMode( FilterMode eMode );
	virtual void					release();
	virtual void					flushVideoMemory() const;

	virtual void					clear( Color nColor );
	virtual void					clearZ( float fDepth = 1.0f );
	virtual void					setAmbient( Color nColor );
	virtual int						addDirectionalLight( int nPriority, Color nColor, const Vector3 & vDirection );
	virtual int						addPointLight( int nPriority, Color nColor, const Vector3 & vPosition, float fRadius );
	virtual void					clearLights();
	virtual void					setFillMode( FillMode nMode );
	virtual void					setFog( FogMode nMode, float fBegin, float fEnd, Color nColor );
	virtual void					disableFog();
	virtual void					setProjection( const Matrix33 & mFrame, const Vector3 & vPosition,
										const RectInt & rWindow, float fFOV, float fFront, float fBack );
	virtual void					setShadowPass( int a_nMaxShadowLights, const Vector3 & a_vFocus,
										float a_fShadowRadius, const SizeInt & a_szShadowMap );
	virtual bool					beginScene();
	virtual bool					beginShadowPass( Transform & a_LightTransform );
	virtual bool					endShadowPass();
	virtual bool					endScene();
	virtual void					abortScene();
	virtual void					present();

	virtual DevicePrimitive *		create( const PrimitiveKey & key );
	virtual bool					push( DevicePrimitive * primitive );
	virtual DisplayEffect::Ref		createEffect( const char * pName );
	virtual bool					push( DisplayEffect * pEffect );
	virtual bool					capture( const char * pFilename );

	// Shader management
	ShaderD3D12::Ref				getShader( const char * pShader );
	bool							releaseShader( const char * pShader );
	void							releaseShaders();

	// Effect factory registration
	void							registerEffect( const char * pName, Factory * pFactory );

	// PSO management
	ID3D12PipelineState *			getOrCreatePSO( const PSOKey & key, ShaderD3D12 * pShader = nullptr );
	ID3D12RootSignature *			getRootSignature() const { return m_pRootSignature.Get(); }
	ID3D12RootSignature *			getPostProcessRootSignature() const { return m_pPostProcessRootSig.Get(); }

	// Dynamic buffer allocation
	UploadRingBuffer::Allocation	allocateDynamic( UINT size, UINT alignment = 4 );
	UploadRingBuffer::Allocation	allocateCB( UINT size );

	// SRV descriptor allocation for the current frame
	UINT							allocateSRV();
	D3D12_CPU_DESCRIPTOR_HANDLE		getSRVCPUHandle( UINT index );
	D3D12_GPU_DESCRIPTOR_HANDLE		getSRVGPUHandle( UINT index );

	// Command list access
	ID3D12GraphicsCommandList *		getCommandList() const { return m_pCommandList.Get(); }
	bool							isCommandListOpen() const { return m_bCommandListOpen; }
	ID3D12Device *					getDevice() const { return m_pDevice.Get(); }

	// Set current world matrix for shaders
	void							setWorldMatrix( const XMMATRIX & mWorld );
	XMMATRIX						getWorldMatrix() const { return m_mCurrentWorld; }
	XMMATRIX						getViewMatrix() const { return m_mView; }
	XMMATRIX						getProjMatrix() const { return m_mProj; }

	// Bind constant buffers to the pipeline
	void							bindPerFrameCB();
	void							bindPerObjectCB();
	void							bindPerMaterialCB( const CBPerMaterial & mat );
	void							bindPerLightCB( const CBPerLight & light );

	// Bind the SRV root descriptor table (root param 4) to the given base slot,
	// skipping the API call when the base is unchanged from the last bind this frame.
	// Any code path that changes the root signature or descriptor heaps must call
	// invalidateBoundSRVTable() before re-binding, because those operations
	// implicitly clear root-param bindings in D3D12.
	void							bindSRVTableIfChanged( UINT nBaseSlot );
	void							invalidateBoundSRVTable();

	// Rebind the main root signature, descriptor heaps, and default CBV/SRV/Sampler
	// root parameters.  Called from beginScene on fresh command lists, and again
	// before the OVERLAY pass in present() after applyFXAA() swapped the root sig.
	void							bindMainRootDefaults();

	// Drain the D3D12 InfoQueue (if debug layer is active) and log any validation
	// messages.  Called from present() so errors are visible in Client.log.
	void							drainInfoQueue();

	// Get filter mode as D3D12 filter type
	D3D12_FILTER					getD3D12Filter() const;

	// Bind pipeline state before draw calls — called by each drawing primitive
	void							bindPSO( PSOKey::InputLayoutType inputLayout, PSOKey::TopologyType topology );

	//----------------------------------------------------------------------------
	// Types

	struct LightInfo
	{
		int		type;		// 1=point, 2=spot, 3=directional
		float	r, g, b, a;
		float	posX, posY, posZ;
		float	dirX, dirY, dirZ;
		float	range;
		float	att0, att1, att2;
		float	specR, specG, specB, specA;
	};

	typedef std::multimap< int, LightInfo, std::greater<int> >		LightMap;
	typedef std::map< CharString, ShaderD3D12::Ref >				ShaderMap;
	typedef std::map< CharString, Factory * >						EffectMap;
	typedef std::list< DisplayEffect::Ref >							EffectList;
	typedef std::list< DisplayEffect::WeakRef >						WeakEffectList;

	struct Projection {
		Projection() : m_mFrame( Matrix33::IDENTITY ),
			m_vPosition( Vector3::ZERO ),
			m_rWindow( PointInt(0,0), SizeInt(1024,1024) ),
			m_fFOV( PI / 4.0f ),
			m_fFront( 1.0f ),
			m_fBack( 20000.0f )
		{};

		Matrix33		m_mFrame;
		Vector3			m_vPosition;
		RectInt			m_rWindow;
		float			m_fFOV;
		float			m_fFront;
		float			m_fBack;
	};

	struct ShadowPass
	{
		ShadowPass() : m_nCascadeIndex(0), m_bOrthoProj(false), m_fShadowDepthRange(0.0f) {}

		Array< DevicePrimitive::Ref >	m_Primitives;
		Transform						m_LightTransform;
		XMFLOAT4X4						m_LightView;		// stored as XMFLOAT4X4 to avoid alignment issues
		XMFLOAT4X4						m_LightProj;
		float							m_fShadowDepthRange;	// far - near of shadow projection (world units)
		int								m_nCascadeIndex;	// which cascade (0..NUM_SHADOW_CASCADES-1)
		bool							m_bOrthoProj;
	};
	typedef std::list< ShadowPass >		ShadowPassList;

	//----------------------------------------------------------------------------
	// Data

	CriticalSection					m_Lock;
	dword							m_dwLockingThread;
	dword							m_nLockCount;

	bool							m_bBeginScene;
	Array< PrimitiveMaterial::Ref >	m_Stack[ PASS_COUNT ];
	PrimitiveMaterial::Ref			m_pCurrentMaterial;
	PrimitiveSetTransform::Ref		m_pCurrentTransform;

	TextureFormat					m_TextureFormats;
	bool							m_TextureP2;
	bool							m_TextureSquare;
	SizeInt							m_TextureMaxSize;
	SizeInt							m_TextureMinSize;
	FilterMode						m_eFilterMode;

	HWND							m_HWND;
	bool							m_bHardware;
	ColorFormat::Ref				m_pFormat;
	Mode							m_Mode;
	bool							m_bWindowed;
	FSAA							m_eFSAA;

	RectInt							m_ClientRectangle;
	bool							m_bMinimized;
	WINDOWPLACEMENT					m_ClientPlacement;
	Projection						m_Proj;
	FillMode						m_eFillMode;

	// D3D12 Core objects
	ComPtr<IDXGIFactory4>			m_pDXGIFactory;
	ComPtr<IDXGIAdapter1>			m_pAdapter;
	ComPtr<ID3D12Device>			m_pDevice;
	ComPtr<ID3D12InfoQueue>			m_pInfoQueue;		// debug layer validation messages (optional)
	ComPtr<ID3D12CommandQueue>		m_pCommandQueue;
	ComPtr<IDXGISwapChain3>			m_pSwapChain;

	// Frame resources
	UINT							m_nFrameIndex;
	ComPtr<ID3D12CommandAllocator>	m_pCommandAllocators[FRAME_COUNT];
	ComPtr<ID3D12GraphicsCommandList> m_pCommandList;
	ComPtr<ID3D12Resource>			m_pRenderTargets[FRAME_COUNT];
	ComPtr<ID3D12Resource>			m_pDepthStencil;

	// Synchronization
	ComPtr<ID3D12Fence>				m_pFence;
	UINT64							m_nFenceValues[FRAME_COUNT];
	UINT64							m_nAllocatorFence[FRAME_COUNT];	// fence value signaled after each allocator's last submission
	HANDLE							m_hFenceEvent;

	// Descriptor heaps
	DescriptorHeap					m_RTVHeap;
	DescriptorHeap					m_DSVHeap;
	DescriptorHeap					m_SRVHeap;			// shader-visible for textures/CBVs
	DescriptorHeap					m_SamplerHeap;		// shader-visible for samplers
	DescriptorHeap					m_SRVStagingHeap;	// CPU-only staging for texture creation

	// Tracked descriptor indices (for reuse on resize instead of leaking)
	UINT							m_nRTVIndices[FRAME_COUNT];	// swap chain RTV indices
	UINT							m_nDSVIndex;				// depth stencil DSV index

	// Per-frame dynamic buffers
	UploadRingBuffer				m_DynamicVB[FRAME_COUNT];
	UploadRingBuffer				m_DynamicCB[FRAME_COUNT];

	// Deferred deletion: primitives whose D3D12 resources may still be referenced
	// by in-flight command lists.  Cleared per-frame after the GPU fence confirms
	// the frame is complete.
	Array< DevicePrimitive::Ref >	m_DeferredPrimitives[FRAME_COUNT];

	// Root signatures
	ComPtr<ID3D12RootSignature>		m_pRootSignature;
	ComPtr<ID3D12RootSignature>		m_pPostProcessRootSig;

	// Pipeline State Object cache
	std::map<PSOKey, ComPtr<ID3D12PipelineState>>	m_PSOCache;

	// Static samplers are baked into root signature

	// Matrices
	XMMATRIX						m_mView;
	XMMATRIX						m_mProj;
	XMMATRIX						m_mCurrentWorld;

	// Per-frame constant buffer data
	CBPerFrame						m_CBPerFrame;
	CBPerObject						m_CBPerObject;
	CBPerMaterial					m_CurrentMatCB;		// accumulated per-material state; bound after setupTextures()

	// Lighting
	Color							m_cAmbientLight;
	LightMap						m_Lights;

	// Shaders
	ShaderMap						m_ShaderMap;
	ShaderD3D12::Ref				m_pDefaultShader;
	ShaderD3D12::Ref				m_pShadowMapShader;
	ShaderD3D12::Ref				m_pPassThroughShader;
	ShaderD3D12::Ref				m_pPassThroughTLShader;
	ShaderD3D12::Ref				m_pPassThroughLShader;
	ShaderD3D12::Ref				m_pPostProcessShader;
	ShaderD3D12::Ref				m_pMatShader;		// current material shader

	// Effects
	EffectMap						m_EffectMap;
	WeakEffectList					m_CreatedEffects;
	EffectList						m_EffectList;

	// Shadow mapping
	float							m_fShadowDepthRange;	// far - near of shadow projection (world units)
	bool							m_bShadowMapReady;
	bool							m_bShadowMapSupported;
	Vector3							m_vShadowFocus;
	float							m_fShadowRadius;
	SizeInt							m_szShadowMap;
	bool							m_bShadowPass;
	int								m_nMaxShadowLights;
	int								m_nShadowMapPass;
	ShadowPassList					m_ShadowPassList;

	bool							m_bFirstShadowPass;
	LightMap::iterator				m_iCurrentShadowLight;
	ShadowPassList::iterator		m_iCurrentShadowPass;

	// Shadow map GPU resources (R32_FLOAT color RT)
	ComPtr<ID3D12Resource>			m_pShadowMapDepth;		// R32_FLOAT color RT
	UINT							m_nShadowMapDSVIndex;	// RTV index in m_RTVHeap (reused name)
	UINT							m_nShadowMapSRVStagingIndex;	// SRV index in m_SRVStagingHeap
	UINT							m_nDepthSRVIndex;			// SRV index for depth buffer (SSAO)

	// Command list open state — true between resetCommandList() and flushCommandList()
	bool							m_bCommandListOpen;

	// Per-frame SRV ring allocator — reset each beginScene(), advanced by setupTextures()
	UINT							m_nSRVFrameOffset;		// next free slot in m_SRVHeap
	UINT							m_nSRVTextureBase;		// base slot for the current material's textures

	// Tracks the last base slot bound to root param 4 so we can skip redundant
	// SetGraphicsRootDescriptorTable calls within a frame.  Invalidated on any
	// root-signature/descriptor-heap change (post-process effects, applyFXAA).
	UINT							m_nLastBoundSRVBase;
	bool							m_bLastBoundSRVValid;

	// Per-frame counters for the ALT+P profiler view — surfaced via PROFILE_LMESSAGE.
	UINT							m_nSRVBindCalls;		// requested this frame
	UINT							m_nSRVBindSkipped;		// skipped because base unchanged
	UINT							m_nMatCBUploads;		// CBPerMaterial uploads this frame
	UINT							m_nMatCBSkipped;		// skipped because data unchanged
	UINT							m_nLightCBUploads;		// CBPerLight uploads this frame
	UINT							m_nLightCBSkipped;		// skipped because data unchanged
	UINT							m_nSRVCopies;			// CopyDescriptorsSimple calls to SRV slots
	UINT							m_nSRVCopiesSkipped;	// skipped because destination already holds source

	// Per-slot cache of which staging-heap SRV index currently lives in each
	// shader-visible heap slot.  Lets setupTextures()/shadow-map binding skip
	// CopyDescriptorsSimple when the destination slot already holds the source.
	// Sized to MAX_SRV_DESCRIPTORS and reset at the start of each frame.
	Array<UINT>						m_SRVSlotStagingIndex;

	// Last-bound per-material and per-light CB data for redundant-upload suppression.
	// We hash the struct via byte-wise compare against this cached copy; on a hit
	// we reuse m_nLastMatCBGpuVA / m_nLastLightCBGpuVA instead of re-allocating
	// from the ring and re-issuing SetGraphicsRootConstantBufferView.
	CBPerMaterial					m_LastMatCB;
	bool							m_bLastMatCBValid;
	D3D12_GPU_VIRTUAL_ADDRESS		m_nLastMatCBGpuVA;

	CBPerLight						m_LastLightCB;
	bool							m_bLastLightCBValid;
	D3D12_GPU_VIRTUAL_ADDRESS		m_nLastLightCBGpuVA;

	// Current pipeline state tracking
	bool							m_bUsingFixedFunction;
	int								m_nTextureStage;
	UINT							m_nCurrentBlend;		// set by setupBlending(): 0=none,1=alpha,2=alpha_inv,3=additive,4=additive_inv
	bool							m_bCurrentDoubleSided;	// set by setupBlending()
	bool							m_bRenderingShadowMap;	// true during shadow map geometry rendering
	bool							m_bShadowMapInRTState;	// true when shadow map resource is in RENDER_TARGET state

	// Dedicated upload queue for immediate texture uploads from loading thread
	ComPtr<ID3D12CommandQueue>		m_pUploadQueue;
	ComPtr<ID3D12CommandAllocator>	m_pUploadAllocator;
	ComPtr<ID3D12GraphicsCommandList> m_pUploadCommandList;
	ComPtr<ID3D12Fence>				m_pUploadFence;
	UINT64							m_nUploadFenceValue;
	HANDLE							m_hUploadFenceEvent;
	CriticalSection					m_UploadCS;

	// FXAA post-process
	ComPtr<ID3D12Resource>			m_pSceneRT;				// intermediate render target for FXAA
	UINT							m_nSceneRTVIndex;		// RTV index in m_RTVHeap
	UINT							m_nSceneSRVIndex;		// SRV index in m_SRVStagingHeap
	ShaderD3D12::Ref				m_pFXAAShader;
	ComPtr<ID3D12PipelineState>		m_pFXAAPSO;
	ComPtr<ID3D12RootSignature>		m_pFXAARootSig;
	bool							m_bFXAAEnabled;
	bool							m_bSceneRTisRT;			// true when m_pSceneRT is in RENDER_TARGET state

	// Static
	static ModeList					sm_ModeList;
	static DeviceList				sm_DeviceList;

	//----------------------------------------------------------------------------
	// Internal methods

	bool							updateClientArea( bool a_bAllowReset );
	bool							initializeD3D12();
	bool							createSwapChain();
	bool							createRenderTargets();
	bool							createDepthStencil();
	bool							createRootSignatures();
	bool							createDefaultShaders();
	void							freeD3D12();
	bool							readyShadowMap();
	bool							createFXAA();
	void							applyFXAA();

	void							waitForGPU();
	void							moveToNextFrame();
	void							flushCommandList();
	void							resetCommandList();
	bool							initUploadQueue();
	void							immediateTextureUpload( class PrimitiveSurfaceD3D12 * pSurface );

	void							updateProjection();
	void							enumerateTextures();

	static void						enumerateModes();
	static ColorFormat::Format		findFormat( DXGI_FORMAT format );
	static DXGI_FORMAT				findFormat( ColorFormat::Format format );

	static void						setXMMatrix( XMMATRIX & m, const Matrix33 & mRotation, const Vector3 & vOffset );
};

//----------------------------------------------------------------------------

#endif

//------------------------------------------------------------------------------------
// EOF
