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
	PrimitiveSetTransform::Ref
							m_TopTransform;

	bool					m_bPushed;

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
