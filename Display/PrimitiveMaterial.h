/*
	PrimitiveMaterial.h

	Material primitive for the display device
	- Use addSurface() to attach the textures/shaders to the primitive, once attached they remain attached unless removed with removeSurface().
	- Use addChild() to attach geometry and transforms to this material. Once this material has been executed, all children primitives
	are removed. If you use DisplayDevice::push(), the surface/shader will attach to the last pushed material automatically.
	
	(c)2005 Palestar, Richard Lyle
*/

#ifndef PRIMITIVE_MATERIAL_H
#define PRIMITIVE_MATERIAL_H

#include "Display/DisplayDevice.h"
#include "Display/PrimitiveSurface.h"
#include "MedusaDll.h"

//----------------------------------------------------------------------------

class DLL PrimitiveMaterial : public DevicePrimitive
{
public:
	DECLARE_DEVICE_PRIMITIVE();

	// Options
	static bool				sm_bEnableSpecular;				// enable specular highlighting
	static int				sm_nMaxLights;

	// Types
	typedef Reference<PrimitiveMaterial>		Ref;
	typedef PrimitiveSurface::Type				SurfaceType;

	enum Blending
	{
		NONE,
		ALPHA,
		ALPHA_INV,	
		ADDITIVE,
		ADDITIVE_INV,
	};
	enum FilterMode
	{
		FILTER_OFF,
		FILTER_ON,

		DEFAULT = FILTER_ON,
	};

	// Accessors
	virtual int					pass() const = 0;							// render pass
	virtual Color				diffuse() const = 0;						// Material properties
	virtual Color				ambient() const = 0;
	virtual Color				emissive() const = 0;
	virtual Color				specular() const = 0;
	virtual float				specularPower() const = 0;

	virtual Blending			blending() const = 0;
	virtual bool				doubleSided() const = 0;
	virtual bool				lightEnabled() const = 0;
	virtual FilterMode			filterMode() const = 0;
	virtual const char *		shader() const = 0;

	// Mutators
	virtual void				setPass( int nPass ) = 0;			// set the render pass for this material
	virtual	void				setMaterial( Color diffuse, Color ambient, 
									Color emissive, Color specular, 
									float specularPower ) = 0;
	virtual void				setBlending( Blending blending ) = 0;
	virtual void				setDoubleSided( bool doubleSided ) = 0;
	virtual void				setLightEnable( bool enable ) = 0;
	virtual void				setFilterMode( FilterMode nMode ) = 0;
	virtual void				setShader( const char * pShader ) = 0;
	// Force depth-write on even when the blend mode is non-opaque.  Default
	// (alpha-blend = no depth-write) is correct for additive particles and
	// most translucent geometry.  But for the cloak shader specifically, we
	// want the front silhouette to occlude the back side so the rim shimmer
	// doesn't bleed through the body.  Default no-op for backends that
	// don't implement it.
	virtual void				setForceDepthWrite( bool bForce ) {}

	// Metal-rough PBR scalars.  Top-level material values; per-texel
	// ORM map (if present) multiplies these.  Defaults are tuned so a
	// material with no PBR maps and no authored overrides looks like
	// "matte grey plastic" — exactly what unauthored fallback should be.
	// Default no-op for backends that don't implement PBR; the D3D12
	// backend overrides this and feeds CBPerMaterial.
	virtual void				setPBRMaterial( float roughness, float metallic, float ao ) {}

	virtual int					addSurface( PrimitiveSurface * pSurface, 
									SurfaceType eType, int nIndex, int nUV, float * pParams ) = 0;
	virtual void				removeSurface( int n ) = 0;
	virtual void				clearSurfaces() = 0;				// remove all surfaces

	virtual int					addChild( DevicePrimitive * pChild ) = 0;
	virtual void				removeChild( int n ) = 0;
	virtual void				clearChildren() = 0;				// remove all child primitives

	virtual void				shadowPass() = 0;				// execute all child primitives for shadows

	// Helpers
	static Ref create( DisplayDevice * pDevice, Color diffuse, Color ambient, Color emissive,
		Color specular, float specularPower, Blending blending )
	{
		// create the primitive
		Ref pPrimitive;
		pDevice->create( pPrimitive );

		// setup the primitive
		pPrimitive->setMaterial( diffuse, ambient, emissive, specular, specularPower );
		pPrimitive->setBlending( blending );
		pPrimitive->setPass( blending != NONE ? DisplayDevice::SECONDARY : DisplayDevice::PRIMARY );

		return(pPrimitive);
	}
	static Ref create( DisplayDevice * pDevice, Blending blending, bool lightEnable = true, bool bDoubleSided = false )
	{
		// create the primitive
		Ref pPrimitive;
		pDevice->create( pPrimitive );
		// setup the primitive
		pPrimitive->setBlending( blending );
		pPrimitive->setLightEnable( lightEnable );
		pPrimitive->setDoubleSided( bDoubleSided );
		pPrimitive->setPass( blending != NONE ? DisplayDevice::SECONDARY : DisplayDevice::PRIMARY );

		// push the primitive
		return pPrimitive;
	}
	static void push( DisplayDevice * pDevice, Color diffuse, Color ambient, Color emissive,
		Color specular, float specularPower, Blending blending )
	{
		pDevice->push( create( pDevice, diffuse, ambient, emissive, specular, specularPower, blending ) );
	}
	static void push( DisplayDevice * pDevice, Color color, Blending blending )
	{
		pDevice->push( create( pDevice, color, color, color, color, 0.0f, blending) );
	}
	static void push( DisplayDevice * pDevice, Blending blending, bool lightEnable = true, bool bDoubleSided = false )
	{
		pDevice->push( create( pDevice, blending, lightEnable, bDoubleSided ) );
	}
	static void push( DisplayDevice * pDevice, PrimitiveMaterial * pMaterial )
	{
		pDevice->push( pMaterial );
	}
};

//----------------------------------------------------------------------------



#endif

//----------------------------------------------------------------------------
// EOF
