/*
	PrimitiveSurfaceD3D12.cpp
	(c)2024 Palestar
*/

#include "Debug/Trace.h"
#include "Standard/Bits.h"
#include "Standard/AutoLock.h"
#include "PrimitiveSurfaceD3D12.h"
#include "PrimitiveFactory.h"
#include "D3D12Helpers.h"		// g_bSRGBPipeline

// Forward declarations — definitions are further down this file.  Needed
// because execute() / set() / flushPendingUploads() (defined earlier in
// this TU) call these helpers before C++ would otherwise see them.
DXGI_FORMAT GetTypelessFormat( DXGI_FORMAT fmt );
DXGI_FORMAT GetSRVFormat( DXGI_FORMAT fmt, bool bSRGB );
bool        IsSRGBColourTexture( PrimitiveSurface::Type eType );

//---------------------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveSurfaceD3D12 );

PrimitiveSurfaceD3D12::PrimitiveSurfaceD3D12()
{
	m_eType = DIFFUSE;
	m_nIndex = 0;
	m_nUV = 0;
	m_bFiltered = true;
	memset( m_fParams, 0, sizeof(m_fParams) );

	m_Size = SizeInt( 0, 0 );
	m_eFormat = ColorFormat::INVALID;
	m_bMipMap = true;
	m_eMode = TM_WRAP;
	m_nLevels = 0;
	m_Pitch = 0;
	m_SRVIndex = UINT(-1);
	m_bSRVCreated = false;
	m_bUploaded = false;
	m_pStagingData = NULL;
	m_StagingSize = 0;
	m_LockedLevel = -1;
	m_DXGIFormat = DXGI_FORMAT_UNKNOWN;
	m_CurrentState = D3D12_RESOURCE_STATE_COPY_DEST;
}

//------------------------------------------------------------------------------------

bool PrimitiveSurfaceD3D12::execute()
{
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
	if ( !pDevice )
		return false;

	// Flush any pending mip uploads into the main command list.
	// Only the open-command-list state matters; m_bBeginScene is false during
	// the OVERLAY pass in present(), but the command list is still open and
	// flushPendingUploads only needs that.
	if ( m_PendingMips.size() > 0 && pDevice->m_bCommandListOpen )
		flushPendingUploads( pDevice );

	// Create the SRV if not yet done
	if ( !m_bSRVCreated && m_Texture )
	{
		if ( m_SRVIndex == UINT(-1) )
			m_SRVIndex = pDevice->allocateSRV();

		// Defensive: if the staging heap is genuinely exhausted (Allocate
		// returns UINT(-1)), GetCPUHandle below would compute an
		// out-of-bounds descriptor pointer and CreateShaderResourceView
		// would corrupt memory.  Skip this surface for the frame instead;
		// the next call site will try again and may succeed if other
		// surfaces have released slots.
		if ( m_SRVIndex == UINT(-1) )
			return false;

		// Texture has no uploaded data (e.g. DXT codec failure) — still in COPY_DEST.
		// Must transition to PSR before the shader can sample it, otherwise GPU hangs.
		// Defer SRV creation until we have a command list to issue the barrier.
		if ( m_CurrentState == D3D12_RESOURCE_STATE_COPY_DEST )
		{
			ID3D12GraphicsCommandList * cl = pDevice->getCommandList();
			if ( !cl )
				return false;
			TransitionResource( cl, m_Texture.Get(),
				D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
			m_CurrentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		}

		// Pick _SRGB SRV view for perceptual-colour textures (DIFFUSE,
		// LIGHTMAP, DARKMAP, DECALMAP) when the sRGB pipeline is on, so
		// hardware decodes from sRGB to linear at sample.  Bump / normal /
		// gloss / shader textures stay UNORM (their data is linear).
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = GetSRVFormat( m_DXGIFormat, IsSRGBColourTexture( m_eType ) );
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = m_nLevels;

		pDevice->getDevice()->CreateShaderResourceView(
			m_Texture.Get(), &srvDesc, pDevice->m_SRVStagingHeap.GetCPUHandle( m_SRVIndex ) );

		m_bSRVCreated = true;
	}

	if ( !m_bSRVCreated )
		return false;

	// Set texture-enable flags directly on the device's current material CB.
	// bindPerMaterialCB() is called by PrimitiveMaterialD3D12 AFTER setupTextures()
	// so these flags are included in the upload.
	switch ( m_eType )
	{
	case DIFFUSE:
		pDevice->m_CurrentMatCB.bEnableDiffuse = 1;
		break;
	case LIGHTMAP:
		if ( sm_bEnableLightMaps )
			pDevice->m_CurrentMatCB.bEnableLightMap = 1;
		break;
	case BUMPMAP:
		if ( sm_bEnableBumpMaps )
		{
			pDevice->m_CurrentMatCB.bEnableBumpMap = 1;
			pDevice->m_CurrentMatCB.fBumpDepth = ( m_fParams[0] > 0.0f ) ? m_fParams[0] * 1.5f : 1.5f;
		}
		break;
	case ORMMAP:
		pDevice->m_CurrentMatCB.bEnableORMMap = 1;
		break;
	case NORMALMAP:
		pDevice->m_CurrentMatCB.bEnableNormalMap = 1;
		// Per-texture Y-axis convention.  m_fParams[0] == 0 (default) is
		// OpenGL/Substance-default (+Y up); != 0 flips Y for DirectX
		// convention.  Artists can set this in the MaterialPort texture
		// dialog without re-exporting from their authoring tool.
		pDevice->m_CurrentMatCB.bFlipNormalY = ( m_fParams[0] != 0.0f ) ? 1 : 0;
		break;
	default:
		break;
	}

	// Determine texture slot based on type.
	// Slots t3 (ORM) and t4 (NORMAL) are consumed by PBR.hlsl; Default.hlsl
	// ignores them (root sig binds the descriptor table regardless, the
	// shader just doesn't reference those texture objects).  Unknown types
	// fall through to slot 0 — collides with DIFFUSE, same legacy behaviour
	// the old switch had.  -1 means "don't bind" (used for surfaces whose
	// type has no slot mapping yet).
	int nTextureSlot = 0;
	switch ( m_eType )
	{
	case DIFFUSE:	nTextureSlot = 0; break;
	case LIGHTMAP:	nTextureSlot = 1; break;
	case BUMPMAP:	nTextureSlot = 2; break;
	case ORMMAP:	nTextureSlot = 3; break;
	case NORMALMAP:	nTextureSlot = 4; break;
	default:		nTextureSlot = 0; break;
	}

	// Copy SRV from staging heap into the per-material slot group.
	// m_nSRVTextureBase is set by setupTextures() to a fresh group of 8 slots
	// (t0-t7), so consecutive materials never alias each other's descriptors.
	// Skip the copy when the destination slot already holds this exact
	// staging-heap SRV (typical when the same texture is reused across draws).
	UINT destSlot = pDevice->m_nSRVTextureBase + nTextureSlot;
	if ( pDevice->isSRVSlotCurrent( destSlot, m_SRVIndex ) )
	{
		++pDevice->m_nSRVCopiesSkipped;
	}
	else
	{
		D3D12_CPU_DESCRIPTOR_HANDLE srcHandle = pDevice->m_SRVStagingHeap.GetCPUHandle( m_SRVIndex );
		D3D12_CPU_DESCRIPTOR_HANDLE dstHandle = pDevice->getSRVCPUHandle( destSlot );
		pDevice->getDevice()->CopyDescriptorsSimple( 1, dstHandle, srcHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
		pDevice->recordSRVSlot( destSlot, m_SRVIndex );
		++pDevice->m_nSRVCopies;
	}

	// Re-bind SRV descriptor table to the base of this material's slot group
	// (skipped internally when the base is unchanged from the previous surface).
	pDevice->bindSRVTableIfChanged( pDevice->m_nSRVTextureBase );

	return true;
}

void PrimitiveSurfaceD3D12::clear()
{}

void PrimitiveSurfaceD3D12::release()
{
	// Defer GPU resource release — the texture (and any pending upload
	// buffer) may still be referenced by an in-flight command list when
	// the surface's smart-ref destructor fires from a worker / sim
	// thread.  Detach transfers the AddRef to the device's per-frame
	// retention list; the device drops it after the frame's GPU fence
	// signals.  Also covers the SRV: if m_bSRVCreated, the SRV staging
	// descriptor still points at the texture, but D3D12 staging
	// descriptors aren't lifetime-tracked — only the underlying resource
	// is, and that's what we defer.
	if ( m_Texture )      DisplayDeviceD3D12::safeDeferReleaseResource( m_pDevice, m_Texture.Detach() );
	if ( m_UploadBuffer ) DisplayDeviceD3D12::safeDeferReleaseResource( m_pDevice, m_UploadBuffer.Detach() );

	// Free staging working buffer
	delete[] m_pStagingData;
	m_pStagingData = NULL;
	m_StagingSize = 0;

	// Free each PendingMip's owned data, then clear the array
	for ( int i = 0; i < (int)m_PendingMips.size(); ++i )
		delete[] m_PendingMips[i].pData;
	m_PendingMips.release();

	// Return the SRV staging-heap slot to the free list before clearing
	// the index, otherwise the slot is leaked permanently.  Over a long
	// session — especially with material rebuilds churning surfaces —
	// this exhausts the heap until Allocate() returns UINT(-1), at which
	// point GetCPUHandle(-1) computes a wildly out-of-bounds descriptor
	// pointer (heapBase + 0xFFFFFFFF * descSize).  CreateShaderResourceView
	// then corrupts memory at that address and the next draw using that
	// SRV crashes inside the driver.  Staging descriptors aren't
	// GPU-lifetime-tracked (only the underlying ID3D12Resource is, and
	// that's already deferred above), and the staging slot is only
	// CopyDescriptorsSimple'd into shader-visible slots — those copies
	// have already happened by the time we reach release(), so freeing
	// the index here is safe.
	//
	// BUT: m_pDevice is a raw pointer that survives the device's
	// destructor.  In MFC the property view (CPropertyView) closes AFTER
	// the 3D scene MDI child, so by the time NodeComplexMesh2-owned
	// surfaces release from the document teardown, m_pDevice is
	// dangling.  Consult sm_DeviceList — same pattern as
	// safeDeferReleaseResource — to verify the device is still live
	// before touching m_SRVStagingHeap.  If it isn't, the heap is about
	// to be destroyed anyway and the slot leak is irrelevant.
	if ( m_SRVIndex != UINT(-1) && m_pDevice )
	{
		bool bDeviceAlive = false;
		for ( int i = 0; i < DisplayDeviceD3D12::sm_DeviceList.size(); ++i )
		{
			if ( DisplayDeviceD3D12::sm_DeviceList[i] == m_pDevice )
			{
				bDeviceAlive = true;
				break;
			}
		}
		if ( bDeviceAlive )
		{
			DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
			pDev->m_SRVStagingHeap.Free( m_SRVIndex );
		}
	}

	m_bSRVCreated = false;
	m_bUploaded = false;
	m_SRVIndex = UINT(-1);
	m_LockedLevel = -1;
	m_CurrentState = D3D12_RESOURCE_STATE_COPY_DEST;
}

//------------------------------------------------------------------------------------

bool PrimitiveSurfaceD3D12::mipmap() const
{
	return m_bMipMap;
}

int PrimitiveSurfaceD3D12::levels() const
{
	return m_nLevels;
}

SizeInt PrimitiveSurfaceD3D12::size() const
{
	return m_Size;
}

int PrimitiveSurfaceD3D12::pitch() const
{
	return m_Pitch;
}

RectInt PrimitiveSurfaceD3D12::rectangle() const
{
	return RectInt( 0, 0, m_Size );
}

ColorFormat::Format PrimitiveSurfaceD3D12::colorFormat() const
{
	return m_eFormat;
}

PrimitiveSurface::TextureMode PrimitiveSurfaceD3D12::textureMode() const
{
	return m_eMode;
}

//------------------------------------------------------------------------------------

inline int getMipLevels( int width, int height )
{
	int minDim = width < height ? width : height;
	int levels = 1;
	while ( minDim > 1 ) { minDim >>= 1; levels++; }
	return levels;
}

static DXGI_FORMAT GetDXGIFormat( ColorFormat::Format eFormat )
{
	switch ( eFormat )
	{
	case ColorFormat::RGB888:	return DXGI_FORMAT_B8G8R8X8_UNORM;
	case ColorFormat::RGB8888:	return DXGI_FORMAT_B8G8R8A8_UNORM;
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

// sRGB-correct pipeline support (Chunk 1).  When a colour texture is created,
// the resource is allocated as TYPELESS (where a typeless variant exists)
// so the SRV view can be cast independently to either the linear UNORM or
// the sRGB UNORM_SRGB form.  Bump / normal / gloss / shader textures hold
// data, not perceptual colour, and stay UNORM.

// Returns the typeless variant of a UNORM colour format, or the input
// format unchanged if there's no typeless equivalent.  16-bit packed
// formats (B5G6R5, B5G5R5A1, B4G4R4A4) have no typeless or sRGB variants
// and are passed through.
DXGI_FORMAT GetTypelessFormat( DXGI_FORMAT fmt )
{
	switch ( fmt )
	{
	case DXGI_FORMAT_B8G8R8A8_UNORM:	return DXGI_FORMAT_B8G8R8A8_TYPELESS;
	case DXGI_FORMAT_B8G8R8X8_UNORM:	return DXGI_FORMAT_B8G8R8X8_TYPELESS;
	case DXGI_FORMAT_R8G8B8A8_UNORM:	return DXGI_FORMAT_R8G8B8A8_TYPELESS;
	case DXGI_FORMAT_BC1_UNORM:			return DXGI_FORMAT_BC1_TYPELESS;
	case DXGI_FORMAT_BC2_UNORM:			return DXGI_FORMAT_BC2_TYPELESS;
	case DXGI_FORMAT_BC3_UNORM:			return DXGI_FORMAT_BC3_TYPELESS;
	case DXGI_FORMAT_BC7_UNORM:			return DXGI_FORMAT_BC7_TYPELESS;
	default:							return fmt;	// no typeless variant
	}
}

// Picks the SRV view format.  When bSRGB is true and the underlying format
// has an sRGB variant, return the _SRGB form so the hardware decodes to
// linear at sample.  Otherwise return the plain UNORM form so data
// textures sample untouched.
DXGI_FORMAT GetSRVFormat( DXGI_FORMAT fmt, bool bSRGB )
{
	if ( !bSRGB )
		return fmt;
	switch ( fmt )
	{
	case DXGI_FORMAT_B8G8R8A8_UNORM:	return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	case DXGI_FORMAT_B8G8R8X8_UNORM:	return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
	case DXGI_FORMAT_R8G8B8A8_UNORM:	return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	case DXGI_FORMAT_BC1_UNORM:			return DXGI_FORMAT_BC1_UNORM_SRGB;
	case DXGI_FORMAT_BC2_UNORM:			return DXGI_FORMAT_BC2_UNORM_SRGB;
	case DXGI_FORMAT_BC3_UNORM:			return DXGI_FORMAT_BC3_UNORM_SRGB;
	case DXGI_FORMAT_BC7_UNORM:			return DXGI_FORMAT_BC7_UNORM_SRGB;
	default:							return fmt;	// no _SRGB variant
	}
}

// Decides whether a texture's SRV should be _SRGB-decoded based on its
// usage type.  Only perceptual-colour textures get sRGB decoding —
// normal / bump / gloss / parallax textures store linear data and would
// be corrupted by gamma decoding.  When the global pipeline flag is off,
// always returns false (legacy behaviour).
bool IsSRGBColourTexture( PrimitiveSurface::Type eType )
{
	if ( !g_bSRGBPipeline )
		return false;
	switch ( eType )
	{
	case PrimitiveSurface::DIFFUSE:
	case PrimitiveSurface::LIGHTMAP:
	case PrimitiveSurface::DARKMAP:
	case PrimitiveSurface::DECALMAP:
		return true;
	default:
		// BUMPMAP, DETAILMAP, GLOSSMAP, NORMALMAP, PARALLAXMAP, SHADERMAP —
		// these store linear data, never gamma-decode.
		return false;
	}
}

int GetBytesPerPixel( DXGI_FORMAT format )
{
	switch ( format )
	{
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8X8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM:
		return 4;
	case DXGI_FORMAT_B5G6R5_UNORM:
	case DXGI_FORMAT_B5G5R5A1_UNORM:
	case DXGI_FORMAT_B4G4R4A4_UNORM:
		return 2;
	case DXGI_FORMAT_BC1_UNORM:
		return 0;	// block compressed - handled separately
	case DXGI_FORMAT_BC2_UNORM:
	case DXGI_FORMAT_BC3_UNORM:
	case DXGI_FORMAT_BC7_UNORM:
		return 0;	// block compressed
	default:
		return 4;
	}
}

// Returns the actual bytes per pixel of the SOURCE data (before D3D12 upload).
// RGB888/RGB888E are 24-bit on disk (3 bytes/pixel) but D3D12 needs 32-bit.
int GetNativePixelBytes( ColorFormat::Format eFormat )
{
	if ( eFormat == ColorFormat::RGB888 || eFormat == ColorFormat::RGB888E )
		return 3;
	DXGI_FORMAT dxgi = GetDXGIFormat( eFormat );
	return GetBytesPerPixel( dxgi );
}

bool IsBlockCompressed( DXGI_FORMAT format )
{
	return format == DXGI_FORMAT_BC1_UNORM ||
		   format == DXGI_FORMAT_BC2_UNORM ||
		   format == DXGI_FORMAT_BC3_UNORM ||
		   format == DXGI_FORMAT_BC7_UNORM;
}

static UINT GetRowPitch( int width, DXGI_FORMAT format )
{
	if ( IsBlockCompressed( format ) )
	{
		int blockWidth = (width + 3) / 4;
		int bytesPerBlock = (format == DXGI_FORMAT_BC1_UNORM) ? 8 : 16;
		return blockWidth * bytesPerBlock;
	}
	return width * GetBytesPerPixel( format );
}

bool PrimitiveSurfaceD3D12::initialize( int width, int height, Format eFormat, bool bMipMap /*= true*/, TextureMode eMode /*= TM_WRAP*/ )
{
	release();

	if ( width <= 0 || height <= 0 )
		return false;

	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;

	m_Size = SizeInt( width, height );
	m_eFormat = eFormat;
	m_bMipMap = bMipMap;
	m_eMode = eMode;
	m_DXGIFormat = GetDXGIFormat( eFormat );
	m_nLevels = bMipMap ? getMipLevels( width, height ) : 1;
	if ( IsBlockCompressed( m_DXGIFormat ) )
		m_Pitch = GetRowPitch( width, m_DXGIFormat );
	else
		m_Pitch = width * GetNativePixelBytes( eFormat );

	// Create the texture resource.  When the sRGB pipeline is on, allocate
	// colour-format resources as TYPELESS so the SRV view can independently
	// pick _SRGB (for perceptual-colour textures) or _UNORM (for data
	// textures like bump / normal maps).  Resource memory layout is
	// identical across the typeless cast — only view interpretation
	// differs.  Formats without a typeless variant (16-bit packed) pass
	// through unchanged.
	const DXGI_FORMAT resourceFormat = g_bSRGBPipeline
		? GetTypelessFormat( m_DXGIFormat )
		: m_DXGIFormat;

	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = (UINT16)m_nLevels;
	texDesc.Format = resourceFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	HRESULT hr = pDevice->getDevice()->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_Texture) );
	if ( FAILED(hr) )
	{
		// Some legacy 16-bit formats (B5G6R5, B5G5R5A1, B4G4R4A4) aren't supported
		// for Texture2D on all DX12 hardware.  Fall back to B8G8R8A8_UNORM
		// (or _TYPELESS in sRGB mode so the SRV view can still cast to _SRGB).
		if ( m_DXGIFormat != DXGI_FORMAT_B8G8R8A8_UNORM )
		{
			m_DXGIFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
			texDesc.Format = g_bSRGBPipeline
				? DXGI_FORMAT_B8G8R8A8_TYPELESS
				: DXGI_FORMAT_B8G8R8A8_UNORM;
			m_Pitch = width * 4;
			hr = pDevice->getDevice()->CreateCommittedResource(
				&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
				D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_Texture) );
		}
		if ( FAILED(hr) )
		{
			TRACE( "PrimitiveSurfaceD3D12::initialize() - Failed to create texture (%dx%d fmt=%d)!", width, height, (int)m_DXGIFormat );
			return false;
		}
	}

	// Allocate an SRV slot in the staging heap
	m_SRVIndex = pDevice->m_SRVStagingHeap.Allocate();
	m_CurrentState = D3D12_RESOURCE_STATE_COPY_DEST;

	return true;
}

void PrimitiveSurfaceD3D12::set( Type eType, int nIndex, int nUV, bool bFiltered, float * pParams /*= NULL*/ )
{
	// If the texture type changes after the SRV was already created, the
	// cached SRV may be wrong for sRGB-correctness — e.g. an early SRV
	// created with the default DIFFUSE type uses an _SRGB view, but a
	// later set(NORMALMAP) means the data is linear and must NOT decode.
	// Force a fresh staging slot so the per-frame slot cache (keyed on
	// m_SRVIndex) misses correctly and the new descriptor reaches the GPU
	// heap.  Reusing the old m_SRVIndex would just overwrite the staging
	// descriptor in place but the cache would still report a hit.
	if ( m_bSRVCreated && m_eType != eType
		&& IsSRGBColourTexture( m_eType ) != IsSRGBColourTexture( eType ) )
	{
		m_bSRVCreated = false;
		m_SRVIndex = UINT(-1);	// next execute() allocates a fresh staging slot
	}

	m_eType = eType;
	m_nIndex = nIndex;
	m_nUV = nUV;
	m_bFiltered = bFiltered;

	if ( pParams != NULL )
		memcpy( m_fParams, pParams, sizeof(m_fParams) );
}

byte * PrimitiveSurfaceD3D12::lock( int nLevel /*= 0*/ )
{
	if ( !m_Texture )
	{
		TRACE( "PrimitiveSurfaceD3D12::lock() - Can't lock surface, m_Texture is NULL!" );
		return NULL;
	}

	// Calculate the pitch and size for this mip level
	int mipWidth = m_Size.width >> nLevel;
	int mipHeight = m_Size.height >> nLevel;
	if ( mipWidth < 1 ) mipWidth = 1;
	if ( mipHeight < 1 ) mipHeight = 1;

	UINT sliceSize;
	if ( IsBlockCompressed( m_DXGIFormat ) )
	{
		m_Pitch = GetRowPitch( mipWidth, m_DXGIFormat );
		int blockHeight = (mipHeight + 3) / 4;
		sliceSize = m_Pitch * blockHeight;
	}
	else
	{
		// Use native pixel size so the engine writes tightly-packed source pixels.
		// For 24-bit formats (RGB888/RGB888E), native is 3 bytes vs 4 for DXGI.
		m_Pitch = mipWidth * GetNativePixelBytes( m_eFormat );
		sliceSize = m_Pitch * mipHeight;
	}

	// Reuse the staging buffer if already the right size.
	// Font rendering calls lock/unlock per character — without reuse each unlock
	// would leave a separate allocation in PendingMips.
	if ( m_pStagingData && m_StagingSize != sliceSize )
	{
		// Size changed (different mip level) — free the working buffer but keep
		// PendingMips intact.  Each PendingMip owns its data independently.
		delete[] m_pStagingData;
		m_pStagingData = NULL;
		m_StagingSize = 0;
	}

	if ( !m_pStagingData )
	{
		if ( sliceSize == 0 || sliceSize > 64 * 1024 * 1024 )
		{
			char buf[256];
			sprintf_s( buf, "PrimitiveSurfaceD3D12::lock() - Bad sliceSize=%u (size=%dx%d level=%d fmt=%d pitch=%d)!\n",
				sliceSize, m_Size.width, m_Size.height, nLevel, (int)m_eFormat, m_Pitch );
			OutputDebugStringA( buf );
			return NULL;
		}
		try {
			m_pStagingData = new byte[ sliceSize ];
		} catch ( const std::bad_alloc & ) {
			char buf[256];
			sprintf_s( buf, "PrimitiveSurfaceD3D12::lock() - bad_alloc for %u bytes (size=%dx%d level=%d)!\n",
				sliceSize, m_Size.width, m_Size.height, nLevel );
			OutputDebugStringA( buf );
			return NULL;
		}
		m_StagingSize = sliceSize;
		memset( m_pStagingData, 0, sliceSize );
	}

	m_LockedLevel = nLevel;
	return m_pStagingData;
}

bool PrimitiveSurfaceD3D12::unlock()
{
	if ( !m_pStagingData || m_LockedLevel < 0 )
	{
		TRACE( "PrimitiveSurfaceD3D12::unlock() - Not locked!" );
		return false;
	}

	AutoLock lock( &m_PendingMipsLock );

	// Each PendingMip OWNS a copy of the staging data.  This is necessary because
	// different mip levels have different sizes, so m_pStagingData gets reallocated
	// between lock(0)/unlock() and lock(1)/unlock().  Without independent copies,
	// only the last mip level survives — causing mip 0 (the visible texture) to be
	// lost and the GPU texture to sample as black.
	//
	// Update existing entry for this level if one exists (font rendering case),
	// otherwise add a new one.
	for ( int i = 0; i < (int)m_PendingMips.size(); ++i )
	{
		if ( m_PendingMips[i].mipLevel == m_LockedLevel )
		{
			// Same mip level (font rendering) — copy so staging buffer can be reused
			if ( m_PendingMips[i].dataSize != m_StagingSize )
			{
				delete[] m_PendingMips[i].pData;
				m_PendingMips[i].pData = NULL;
				m_PendingMips[i].dataSize = 0;
				try {
					m_PendingMips[i].pData = new byte[ m_StagingSize ];
				} catch ( const std::bad_alloc & ) {
					// Remove the corrupt entry so flushPendingUploads doesn't read NULL
					m_PendingMips.remove( i );
					m_LockedLevel = -1;
					return false;
				}
			}
			memcpy( m_PendingMips[i].pData, m_pStagingData, m_StagingSize );
			m_PendingMips[i].dataSize = m_StagingSize;
			m_PendingMips[i].pitch    = m_Pitch;
			m_LockedLevel = -1;
			return true;
		}
	}

	PendingMip pending;
	pending.pData    = m_pStagingData;		// transfer ownership — no copy needed
	pending.dataSize = m_StagingSize;
	pending.mipLevel = m_LockedLevel;
	pending.pitch    = m_Pitch;
	m_PendingMips.push( pending );

	m_pStagingData = NULL;					// staging buffer now owned by PendingMip
	m_StagingSize = 0;
	m_LockedLevel = -1;
	return true;
}

void PrimitiveSurfaceD3D12::flush()
{
	// No-op: texture data is uploaded lazily during execute() via flushPendingUploads().
	// Synchronous GPU uploads from the loading thread (immediateTextureUpload) cause
	// deadlocks due to cross-queue fence synchronization issues with the D3D12 debug layer.
	// PendingMips accumulate in CPU memory until the texture is first rendered, which is
	// the same behavior the engine had before the upload queue was added.
}

//------------------------------------------------------------------------------------

void PrimitiveSurfaceD3D12::flushPendingUploads( DisplayDeviceD3D12 * pDevice )
{
	AutoLock lock( &m_PendingMipsLock );

	if ( m_PendingMips.size() == 0 )
		return;

	ID3D12GraphicsCommandList * cl = pDevice->getCommandList();
	if ( !cl )
		return;

	// Transition to COPY_DEST once for all pending mips
	if ( m_CurrentState != D3D12_RESOURCE_STATE_COPY_DEST )
	{
		TransitionResource( cl, m_Texture.Get(), m_CurrentState, D3D12_RESOURCE_STATE_COPY_DEST );
		m_CurrentState = D3D12_RESOURCE_STATE_COPY_DEST;
	}

	D3D12_RESOURCE_DESC texDesc = m_Texture->GetDesc();

	for ( int i = 0; i < (int)m_PendingMips.size(); ++i )
	{
		PendingMip & mip = m_PendingMips[i];
		if ( !mip.pData || mip.dataSize == 0 )
			continue;

		UINT64 uploadSize = 0;
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
		UINT numRows = 0;
		UINT64 rowSizeInBytes = 0;
		pDevice->getDevice()->GetCopyableFootprints( &texDesc, mip.mipLevel, 1, 0,
			&footprint, &numRows, &rowSizeInBytes, &uploadSize );

		// Use ring buffer for the upload data
		UploadRingBuffer::Allocation alloc = pDevice->allocateDynamic( (UINT)uploadSize, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT );
		if ( !alloc.cpuAddress )
			continue;	// ring buffer overflow — skip this mip, will retry next frame

		byte * pDst = (byte *)alloc.cpuAddress + footprint.Offset;
		byte * pSrc = mip.pData;
		UINT srcRowPitch = mip.pitch;
		UINT dstRowPitch = footprint.Footprint.RowPitch;

		int nativeBPP = GetNativePixelBytes( m_eFormat );
		int dxgiBPP   = GetBytesPerPixel( m_DXGIFormat );
		bool bExpand = !IsBlockCompressed( m_DXGIFormat ) && (nativeBPP != dxgiBPP) && (nativeBPP > 0) && (dxgiBPP > 0);

		for ( UINT row = 0; row < numRows; ++row )
		{
			byte * pSrcRow = pSrc + row * srcRowPitch;
			byte * pDstRow = pDst + row * dstRowPitch;

			if ( bExpand )
			{
				// Expand e.g. 3-byte BGR → 4-byte BGRX
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

		// Patch the footprint offset to be relative to the ring buffer allocation
		footprint.Offset = alloc.offset;

		D3D12_TEXTURE_COPY_LOCATION dst = {};
		dst.pResource = m_Texture.Get();
		dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dst.SubresourceIndex = mip.mipLevel;

		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = pDevice->m_DynamicVB[ pDevice->m_nFrameIndex ].GetResource();
		src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint = footprint;

		cl->CopyTextureRegion( &dst, 0, 0, 0, &src, nullptr );

		// Mark this mip as consumed so cleanup loop knows it was uploaded
		delete[] mip.pData;
		mip.pData = NULL;
		mip.dataSize = 0;
	}

	// Remove consumed entries (pData == NULL after successful upload).
	// Skipped mips (ring buffer overflow) still have valid pData — keep them for retry.
	for ( int i = (int)m_PendingMips.size() - 1; i >= 0; --i )
	{
		if ( m_PendingMips[i].pData == NULL )
			m_PendingMips.remove( i );
	}

	// Transition back to PSR so shaders can sample the texture
	TransitionResource( cl, m_Texture.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	m_CurrentState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

	m_bUploaded = true;
	// SRV creation is the caller's responsibility (execute() handles it after we return).
	// Creating it here would race with m_SRVIndex==UINT(-1) and write OOB into the staging heap.
}

//------------------------------------------------------------------------------------
// EOF
