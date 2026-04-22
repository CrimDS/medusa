/*
	PrimitiveMaterialD3D12.h
	(c)2024 Palestar
*/

#ifndef PRIMITIVE_MATERIAL_D3D12_H
#define PRIMITIVE_MATERIAL_D3D12_H

#include "Display/PrimitiveMaterial.h"
#include "Display/PrimitiveSetTransform.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

//----------------------------------------------------------------------------

class PrimitiveMaterialD3D12 : public PrimitiveMaterial
{
public:
	PrimitiveMaterialD3D12();

	// DevicePrimitive interface
	bool					execute();
	void					clear();
	void					release();

	// PrimitiveMaterial interface
	virtual int				pass() const;
	virtual Color			diffuse() const;
	virtual Color			ambient() const;
	virtual Color			emissive() const;
	virtual Color			specular() const;
	virtual float			specularPower() const;
	virtual Blending		blending() const;
	virtual bool			doubleSided() const;
	virtual bool			lightEnabled() const;
	virtual FilterMode		filterMode() const;
	virtual const char *	shader() const;

	virtual void			setPass( int nPass );
	virtual void			setMaterial( Color diffuse, Color ambient,
								Color emissive, Color specular,
								float specularPower );
	virtual void			setBlending( Blending blending );
	virtual void			setDoubleSided( bool doubleSided );
	virtual void			setLightEnable( bool enable );
	virtual void			setFilterMode( FilterMode nMode );
	virtual void			setShader( const char * pShader );

	virtual int				addSurface( PrimitiveSurface * pSurface,
								SurfaceType eType, int nIndex, int nUV, float * pParams );
	virtual void			removeSurface( int n );
	virtual void			clearSurfaces();

	virtual int				addChild( DevicePrimitive * pPrimitive );
	virtual void			removeChild( int n );
	virtual void			clearChildren();

	virtual void			shadowPass();

	// Mutators
	void					setShader( ShaderD3D12::Ref pShader );
	void					clearShaders();

	// Types
	struct Surface
	{
		PrimitiveSurface::Ref		m_pSurface;
		SurfaceType					m_eType;
		int							m_nIndex;
		int							m_nUV;
		float						m_fParams[ PrimitiveSurface::MAX_TEXTURE_PARAMS ];
	};

	// Data
	int						m_nPass;
	Blending				m_Blending;
	Color					m_Diffuse;
	Color					m_Ambient;
	Color					m_Emissive;
	Color					m_Specular;
	float					m_SpecularPower;
	bool					m_DoubleSided;
	bool					m_LightEnable;
	FilterMode				m_nFilterMode;

	bool					m_bUpdateShaders;
	CharString				m_sShader;
	ShaderD3D12::Ref		m_pShader;

	Array< Surface >		m_Surfaces;
	Array< DevicePrimitive::Ref >
							m_Children;
	// Parallel array recording the preRender child index that was active
	// when each m_Children entry was added.  Used by executeChildren to
	// stable-sort children into traversal order before rendering, so a
	// shared material whose addChild calls came from multiple workers
	// (in lock-acquire order) still draws in the same order serial mode
	// would produce.  Index -1 means the entry was added outside parallel
	// zone dispatch (main-thread path) — those preserve their push order
	// because all -1 ties tie-break stable.
	Array< int >			m_ChildOrder;
	PrimitiveSetTransform::Ref
							m_TopTransform;

	bool					m_bPushed;

	// Lowest child index that has tried to push this material during the
	// current frame's parallel preRender dispatch.  Tracked under
	// RenderContext::sm_StateLock so mergeWorkerStacks can place the material
	// at the position serial rendering would give it (the first child that
	// uses it), even when a later-indexed child won the push race.  Initialised
	// to INT_MAX; reset alongside m_bPushed in clear() and release().
	// INT_MAX means "not touched this frame" (or only touched outside parallel
	// dispatch, where tl_nPreRenderChildIndex is -1 — see push() guard).
	int						m_nFirstClaimChild;

	void					setupBlending();
	bool					setupTextures();
	bool					executeChildren();

	// Helpers
	static ShaderFloat4		makeShaderFloat4( const Color & src );
	static Color			makeColor( const ShaderFloat4 & src );
	static int				sortSurfaces( Surface p1, Surface p2 );
};

//----------------------------------------------------------------------------

#endif

//------------------------------------------------------------------------------------
// EOF
