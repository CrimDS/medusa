/*
	PrimitiveSurfaceD3D12.h
	(c)2024 Palestar
*/

#ifndef PRIMITIVE_SURFACE_D3D12_H
#define PRIMITIVE_SURFACE_D3D12_H

#include "Display/PrimitiveSurface.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"
#include "Standard/CriticalSection.h"

//----------------------------------------------------------------------------

class PrimitiveSurfaceD3D12 : public PrimitiveSurface
{
public:
	typedef Reference< PrimitiveSurfaceD3D12 >	Ref;

	PrimitiveSurfaceD3D12();

	// DevicePrimitive interface
	virtual bool			execute();
	virtual void			clear();
	virtual void			release();

	// PrimitiveSurface interface
	virtual bool			mipmap() const;
	virtual int				levels() const;
	virtual SizeInt			size() const;
	virtual int				pitch() const;
	virtual RectInt			rectangle() const;
	virtual Format			colorFormat() const;
	virtual TextureMode		textureMode() const;

	// Mutators
	virtual bool			initialize( int width, int height, Format eFormat, bool bMipMap = true, TextureMode eMode = TM_WRAP );
	virtual void			set( Type eType, int nIndex, int nUV, bool bFiltered, float * pParams = NULL );
	virtual byte *			lock( int nLevel = 0 );
	virtual bool			unlock();
	virtual void			flush();

	// Called from execute() to flush deferred mip uploads into the open command list
	void					flushPendingUploads( DisplayDeviceD3D12 * pDevice );

	// Data
	Type					m_eType;
	int						m_nIndex;
	int						m_nUV;
	bool					m_bFiltered;
	float					m_fParams[ MAX_TEXTURE_PARAMS ];

	SizeInt					m_Size;
	Format					m_eFormat;
	bool					m_bMipMap;
	TextureMode				m_eMode;
	int						m_nLevels;
	int						m_Pitch;

	ComPtr<ID3D12Resource>	m_Texture;
	ComPtr<ID3D12Resource>	m_UploadBuffer;
	UINT					m_SRVIndex;			// index in the SRV staging heap
	bool					m_bSRVCreated;
	bool					m_bUploaded;		// true if texture has been uploaded to GPU

	// Staging CPU buffer for lock/unlock
	byte *					m_pStagingData;
	UINT					m_StagingSize;
	int						m_LockedLevel;
	DXGI_FORMAT				m_DXGIFormat;

	// Tracks the current D3D12 resource state to issue correct barriers on re-upload
	D3D12_RESOURCE_STATES	m_CurrentState;

	// Deferred upload: staging data for each mip level pending GPU upload
	struct PendingMip {
		byte *	pData;
		UINT	dataSize;
		int		mipLevel;
		UINT	pitch;
	};
	Array< PendingMip >		m_PendingMips;		// mips waiting to be uploaded in execute()

	// Serializes mutations of m_PendingMips between the producer (unlock,
	// usually on the Broker loader thread; in the font path on the render
	// thread itself) and the consumer (flushPendingUploads on the render
	// thread).  Engine convention is meant to keep them apart, but fonts can
	// re-lock mid-frame so the producer side is observably re-entrant against
	// rendering — protect the shared array.
	mutable CriticalSection	m_PendingMipsLock;
};

//----------------------------------------------------------------------------

#endif

//------------------------------------------------------------------------------------
// EOF
