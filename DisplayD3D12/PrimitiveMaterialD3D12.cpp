/*
	PrimitiveMaterialD3D12.cpp
	(c)2024 Palestar
*/

#include "Debug/Assert.h"
#include "Debug/Trace.h"
#include "PrimitiveMaterialD3D12.h"
#include "PrimitiveSurfaceD3D12.h"
#include "PrimitiveFactory.h"
#include "DisplayD3D12/PrimitiveSetTransformD3D12.h"

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveMaterialD3D12 );

PrimitiveMaterialD3D12::PrimitiveMaterialD3D12()
{
	m_nPass = DisplayDevice::PRIMARY;
	m_Blending = NONE;
	m_DoubleSided = false;
	m_LightEnable = true;
	m_bPushed = false;
	m_nFilterMode = FILTER_ON;
	m_bUpdateShaders = false;

	// NOTE: Color(r,g,b) defaults alpha to 0 — use 4-arg form with alpha=255.
	setMaterial( Color(255,255,255,255), Color(255,255,255,255),
		Color(0,0,0,255), Color(255,255,255,255), 0.0f );
}

//------------------------------------------------------------------------------------

bool PrimitiveMaterialD3D12::execute()
{
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
	if ( !pDevice )
		return false;

	// In DX12, pipeline state is set via PSOs - we configure the PSO key here
	// and let the device create/cache the appropriate PSO

	DisplayDeviceD3D12::LightMap & lights = pDevice->m_Lights;

	// Determine if we use the passthrough or full lighting shader.
	// If a custom shader is explicitly set, always use the full pipeline so it gets loaded.
	bool bHasCustomShader = (m_sShader.length() > 0);
	bool bUsePassthrough = !bHasCustomShader
		&& (DisplayDevice::sm_bUseFixedFunction
			|| !m_LightEnable
			|| lights.size() == 0
			|| m_Blending == PrimitiveMaterial::ADDITIVE);

	// Diagnostic: log custom shader materials
	if ( bHasCustomShader )
	{
		static int s_nCustomLog = 0;
		if ( s_nCustomLog < 30 )
		{
			TRACE( "Material::execute CUSTOM: shader='%s', pass=%d, passthrough=%d, lightEn=%d, lights=%d, blend=%d, children=%d, doubleSided=%d",
				(const char *)m_sShader, m_nPass, bUsePassthrough ? 1 : 0,
				m_LightEnable ? 1 : 0,
				(int)lights.size(),
				(int)m_Blending,
				m_Children.size(),
				m_DoubleSided ? 1 : 0 );
			++s_nCustomLog;
		}
	}

	if ( bUsePassthrough )
	{
		pDevice->m_bUsingFixedFunction = true;

		// Load custom shader if one was requested (e.g. Star.hlsl)
		if ( m_bUpdateShaders )
		{
			m_bUpdateShaders = false;
			m_pShader = NULL;
			if ( m_sShader.length() > 0 )
				m_pShader = pDevice->getShader( m_sShader );
		}

		// Use passthrough shader - just transforms and applies texture.
		// Set m_pMatShader to null so bindPSO's getOrCreatePSO fallback switch
		// picks the layout-compatible shader (PassThrough for IL_VERTEX,
		// PassThroughL for IL_VERTEXL) — avoids CREATEINPUTLAYOUT_MISSINGELEMENT
		// when PassThroughL (COLOR0) is paired with an IL_VERTEX layout (no COLOR).
		if ( m_pShader.valid() && m_pShader != pDevice->m_pDefaultShader )
			pDevice->m_pMatShader = m_pShader;
		else
			pDevice->m_pMatShader = nullptr;

		// Initialize per-material CB; texture enables filled by setupTextures()
		pDevice->m_CurrentMatCB = {};
		pDevice->m_CurrentMatCB.vMatDiffuse = makeShaderFloat4( m_Diffuse );
		pDevice->m_CurrentMatCB.vMatAmbient = makeShaderFloat4( m_Ambient );
		pDevice->m_CurrentMatCB.vMatEmissive = makeShaderFloat4( m_Emissive );
		pDevice->m_CurrentMatCB.vMatSpecular = makeShaderFloat4( m_Specular );
		pDevice->m_CurrentMatCB.fMatSpecularPower = m_SpecularPower;
		pDevice->m_CurrentMatCB.bEnableAmbient = 1;

		setupBlending();
		if ( !setupTextures() )
			return false;
		pDevice->bindPerMaterialCB( pDevice->m_CurrentMatCB );
		if ( !executeChildren() )
			return false;
	}
	else
	{
		// Per-light rendering with full shader pipeline
		if ( m_bUpdateShaders || !m_pShader.valid() || m_pShader->released()
			|| (m_sShader.length() > 0 && m_pShader == pDevice->m_pDefaultShader) )
		{
			m_bUpdateShaders = false;
			m_pShader = NULL;
			if ( m_sShader.length() > 0 )
				m_pShader = pDevice->getShader( m_sShader );
			if ( !m_pShader.valid() )
				m_pShader = pDevice->m_pDefaultShader;
		}

		if ( !m_pShader.valid() || m_pShader->released() )
			return false;

		pDevice->m_bUsingFixedFunction = false;
		pDevice->m_pMatShader = m_pShader;
		setupBlending();

		// Initialize per-material CB; texture enables filled by setupTextures()
		pDevice->m_CurrentMatCB = {};
		pDevice->m_CurrentMatCB.vMatDiffuse = makeShaderFloat4( m_Diffuse );
		pDevice->m_CurrentMatCB.vMatAmbient = makeShaderFloat4( m_Ambient );
		pDevice->m_CurrentMatCB.vMatEmissive = makeShaderFloat4( m_Emissive );
		pDevice->m_CurrentMatCB.vMatSpecular = makeShaderFloat4( m_Specular );
		pDevice->m_CurrentMatCB.fMatSpecularPower = m_SpecularPower;
		pDevice->m_CurrentMatCB.bEnableAmbient = 1;

		if ( !setupTextures() )
			return false;

		DisplayDeviceD3D12::ShadowPassList::iterator iShadowPass = pDevice->m_ShadowPassList.begin();

		// Render once per light (or once with no lights if there are none)
		// Custom shaders that don't need lighting skip per-light rendering
		int nLightCount = 0;
		if ( lights.size() == 0 || (bHasCustomShader && !m_LightEnable) )
		{
			// No lights — render once with an empty light CB
			CBPerLight lightCB = {};
			pDevice->m_CurrentMatCB.bEnableShadowMap = 0;
			pDevice->bindPerMaterialCB( pDevice->m_CurrentMatCB );
			pDevice->bindPerLightCB( lightCB );

			if ( !executeChildren() )
				return false;
		}
		else
		{
			for ( DisplayDeviceD3D12::LightMap::iterator iLight = lights.begin();
				iLight != lights.end(); ++iLight, ++nLightCount )
			{
				if ( nLightCount >= sm_nMaxLights )
					break;

				DisplayDeviceD3D12::LightInfo & light = iLight->second;

				// Setup light constant buffer
				CBPerLight lightCB = {};
				lightCB.nLightType = light.type;
				lightCB.vLightDiffuse = ShaderFloat4( light.r, light.g, light.b, light.a );
				lightCB.vLightSpecular = ShaderFloat4( light.specR, light.specG, light.specB, light.specA );
				lightCB.vLightPosition = ShaderFloat4( light.posX, light.posY, light.posZ, 0.0f );
				lightCB.vLightDirection = ShaderFloat4( light.dirX, light.dirY, light.dirZ, 0.0f );

				if ( light.type == 1 )	// point light
					lightCB.vAttenuation = ShaderFloat4( light.att0, light.att1, light.att2, 0.0f );

				// Shadow map binding — fill cascade view-proj matrices and copy SRV into t7
				if ( iShadowPass != pDevice->m_ShadowPassList.end() && pDevice->m_pShadowMapDepth
					&& pDevice->m_nShadowMapSRVStagingIndex != UINT(-1) )
				{
					pDevice->m_CurrentMatCB.bEnableShadowMap = 1;

					// Consume all cascade passes for this light
					for ( int c = 0; c < NUM_SHADOW_CASCADES && iShadowPass != pDevice->m_ShadowPassList.end(); ++c, ++iShadowPass )
					{
						DisplayDeviceD3D12::ShadowPass & pass = *iShadowPass;
						XMMATRIX lv = XMLoadFloat4x4( &pass.m_LightView );
						XMMATRIX lp = XMLoadFloat4x4( &pass.m_LightProj );
						lightCB.mCascadeViewProj[c] = ShaderMatrix( lv * lp );
					}

					// Fill cascade split distances
					lightCB.vCascadeSplits = ShaderFloat4(
						pDevice->m_fShadowRadius * 0.08f,
						pDevice->m_fShadowRadius * 0.24f,
						pDevice->m_fShadowRadius * 0.60f,
						pDevice->m_fShadowRadius * 1.0f );

					// Copy shadow map SRV into slot t7
					UINT smDestSlot = pDevice->m_nSRVTextureBase + 7;
					D3D12_CPU_DESCRIPTOR_HANDLE srcHandle = pDevice->m_SRVStagingHeap.GetCPUHandle( pDevice->m_nShadowMapSRVStagingIndex );
					D3D12_CPU_DESCRIPTOR_HANDLE dstHandle = pDevice->getSRVCPUHandle( smDestSlot );
					pDevice->getDevice()->CopyDescriptorsSimple( 1, dstHandle, srcHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

					// Re-bind SRV table
					ID3D12GraphicsCommandList * cl = pDevice->getCommandList();
					if ( cl )
						cl->SetGraphicsRootDescriptorTable( 4, pDevice->getSRVGPUHandle( pDevice->m_nSRVTextureBase ) );
				}
				else
				{
					pDevice->m_CurrentMatCB.bEnableShadowMap = 0;
				}

				pDevice->bindPerMaterialCB( pDevice->m_CurrentMatCB );
				pDevice->bindPerLightCB( lightCB );

				// Execute children (geometry)
				if ( !executeChildren() )
					return false;

				// After the first light, switch to additive blending for subsequent passes
				if ( nLightCount == 0 )
				{
					pDevice->m_CurrentMatCB.bEnableAmbient = 0;
					pDevice->m_nCurrentBlend = 3;	// ADDITIVE: SRC_ALPHA + ONE
					pDevice->m_bCurrentDoubleSided = m_DoubleSided;
				}
			}
		}
	}

	return true;
}

void PrimitiveMaterialD3D12::clear()
{
	// Move children to the device's per-frame deferred list instead of releasing
	// immediately.  Child primitives may own D3D12 resources (vertex buffers,
	// textures) that are still referenced by in-flight command list commands.
	// The deferred list is cleared in beginScene after the GPU fence confirms
	// the frame is complete.
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
	if ( pDevice && m_Children.size() > 0 )
	{
		Array< DevicePrimitive::Ref > & deferred = pDevice->m_DeferredPrimitives[ pDevice->m_nFrameIndex ];
		for ( int i = 0; i < m_Children.size(); ++i )
		{
			if ( m_Children[i] )
				deferred.push( m_Children[i] );
		}
	}
	m_Children.release();
	m_TopTransform = NULL;
	m_bPushed = false;
}

void PrimitiveMaterialD3D12::release()
{
	m_Children.release();
	m_Surfaces.release();
	m_TopTransform = NULL;
	m_bPushed = false;

	m_nPass = DisplayDevice::PRIMARY;
	m_Blending = NONE;
	m_DoubleSided = false;
	m_LightEnable = true;
	m_nFilterMode = FILTER_ON;
	m_bUpdateShaders = false;
	m_pShader = NULL;
	m_sShader = "";

	setMaterial( Color(255,255,255,255), Color(255,255,255,255),
		Color(0,0,0,255), Color(255,255,255,255), 0.0f );
}

//------------------------------------------------------------------------------------

int PrimitiveMaterialD3D12::pass() const { return m_nPass; }
Color PrimitiveMaterialD3D12::diffuse() const { return m_Diffuse; }
Color PrimitiveMaterialD3D12::ambient() const { return m_Ambient; }
Color PrimitiveMaterialD3D12::emissive() const { return m_Emissive; }
Color PrimitiveMaterialD3D12::specular() const { return m_Specular; }
float PrimitiveMaterialD3D12::specularPower() const { return m_SpecularPower; }
PrimitiveMaterial::Blending PrimitiveMaterialD3D12::blending() const { return m_Blending; }
bool PrimitiveMaterialD3D12::doubleSided() const { return m_DoubleSided; }
bool PrimitiveMaterialD3D12::lightEnabled() const { return m_LightEnable; }
PrimitiveMaterial::FilterMode PrimitiveMaterialD3D12::filterMode() const { return m_nFilterMode; }
const char * PrimitiveMaterialD3D12::shader() const { return m_sShader; }

//------------------------------------------------------------------------------------

void PrimitiveMaterialD3D12::setPass( int nPass ) { m_nPass = nPass; }

void PrimitiveMaterialD3D12::setMaterial( Color diffuse, Color ambient,
	Color emissive, Color specular, float specularPower )
{
	m_Diffuse = diffuse;
	m_Ambient = ambient;
	m_Emissive = emissive;
	m_Specular = specular;
	m_SpecularPower = specularPower;
}

void PrimitiveMaterialD3D12::setBlending( Blending blending ) { m_Blending = blending; }
void PrimitiveMaterialD3D12::setDoubleSided( bool doubleSided ) { m_DoubleSided = doubleSided; }
void PrimitiveMaterialD3D12::setLightEnable( bool enable ) { m_LightEnable = enable; }
void PrimitiveMaterialD3D12::setFilterMode( FilterMode nMode ) { m_nFilterMode = nMode; }

void PrimitiveMaterialD3D12::setShader( const char * pShader )
{
	m_sShader = pShader != NULL ? pShader : "";
	m_bUpdateShaders = true;
}

int PrimitiveMaterialD3D12::addSurface( PrimitiveSurface * pSurface,
	SurfaceType eType, int nIndex, int nUV, float * pParams )
{
	Surface & surface = m_Surfaces.push();
	surface.m_pSurface = pSurface;
	surface.m_eType = eType;
	surface.m_nIndex = nIndex;
	surface.m_nUV = nUV;

	if ( pParams != NULL )
		memcpy( surface.m_fParams, pParams, sizeof(surface.m_fParams) );

	m_Surfaces.qsort( sortSurfaces );
	return m_Surfaces.size() - 1;
}

void PrimitiveMaterialD3D12::removeSurface( int n )
{
	if ( m_Surfaces.isValid( n ) )
		m_Surfaces.remove( n );
}

void PrimitiveMaterialD3D12::clearSurfaces()
{
	m_Surfaces.release();
}

int PrimitiveMaterialD3D12::addChild( DevicePrimitive * pPrimitive )
{
	if ( pPrimitive->primitiveKey() == PrimitiveSetTransform::staticPrimitiveKey() )
	{
		if ( m_TopTransform == pPrimitive )
			return -1;

		m_TopTransform = (PrimitiveSetTransform *)pPrimitive;

		if ( m_Children.size() > 0 )
		{
			DevicePrimitive * pLastPrimitive = m_Children.last();
			if ( pLastPrimitive->primitiveKey() == PrimitiveSetTransform::staticPrimitiveKey() )
			{
				m_Children[ m_Children.size() - 1 ] = pPrimitive;
				return m_Children.size() - 1;
			}
		}
	}

	m_Children.push( pPrimitive );
	return m_Children.size() - 1;
}

void PrimitiveMaterialD3D12::removeChild( int n )
{
	if ( m_Children.isValid( n ) )
		m_Children.remove( n );
}

void PrimitiveMaterialD3D12::clearChildren()
{
	m_Children.release();
}

void PrimitiveMaterialD3D12::shadowPass()
{
	for ( int i = 0; i < m_Children.size(); i++ )
	{
		DevicePrimitive * pPrimitive = m_Children[i];
		if ( !pPrimitive )
			continue;
		if ( pPrimitive->primitiveKey() == PrimitiveSurface::staticPrimitiveKey() ||
			 pPrimitive->primitiveKey() == PrimitiveMaterial::staticPrimitiveKey() )
			continue;

		pPrimitive->setDevice( device() );
		if ( !pPrimitive->execute() )
			TRACE( "PrimitiveMaterialD3D12::shadowPass() - Child primitive failed!" );
	}
}

//---------------------------------------------------------------------------------------------------

void PrimitiveMaterialD3D12::setShader( ShaderD3D12::Ref pShader )
{
	m_pShader = pShader;
	m_sShader = pShader.valid() ? pShader->shaderName() : "";
}

void PrimitiveMaterialD3D12::clearShaders()
{
	m_pShader = NULL;
	m_sShader = "";
}

//---------------------------------------------------------------------------------------------------

void PrimitiveMaterialD3D12::setupBlending()
{
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
	// Blending enum values match PSOKey blendMode: NONE=0, ALPHA=1, ALPHA_INV=2, ADDITIVE=3, ADDITIVE_INV=4
	pDevice->m_nCurrentBlend = (UINT)m_Blending;
	pDevice->m_bCurrentDoubleSided = m_DoubleSided;
}

bool PrimitiveMaterialD3D12::setupTextures()
{
	// Reset texture enables on the current material CB before surfaces set them
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
	pDevice->m_CurrentMatCB.bEnableDiffuse  = 0;
	pDevice->m_CurrentMatCB.bEnableLightMap = 0;
	pDevice->m_CurrentMatCB.bEnableBumpMap  = 0;
	pDevice->m_nTextureStage = 0;

	// Allocate 8 fresh contiguous SRV slots for this material draw (t0-t7).
	// Slots 0-2 are diffuse/lightmap/bumpmap; slot 7 is shadow map.
	// Must cover the full descriptor table range declared in the root signature (8 slots).
	// Wrap BEFORE allocation to ensure all 8 slots fit within the heap.
	if ( pDevice->m_nSRVFrameOffset + 8 > DisplayDeviceD3D12::MAX_SRV_DESCRIPTORS )
		pDevice->m_nSRVFrameOffset = 8;
	pDevice->m_nSRVTextureBase = pDevice->m_nSRVFrameOffset;
	pDevice->m_nSRVFrameOffset += 8;

	for ( int i = 0; i < m_Surfaces.size(); i++ )
	{
		Surface & surface = m_Surfaces[i];
		surface.m_pSurface->setDevice( device() );
		surface.m_pSurface->set( surface.m_eType,
			surface.m_nIndex,
			surface.m_nUV,
			m_nFilterMode == FILTER_ON,
			surface.m_fParams );

		if ( !surface.m_pSurface->execute() )
		{
			// Don't abort the entire material — skip this texture and continue.
			// The geometry should still render (untextured) rather than disappear.
			continue;
		}
	}

	return true;
}

bool PrimitiveMaterialD3D12::executeChildren()
{
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;

	for ( int i = 0; i < m_Children.size(); i++ )
	{
		DevicePrimitive * pPrimitive = m_Children[i];
		if ( !pPrimitive )
			continue;

		// If we hit a surface child (e.g. font surface pushed via push()), reset
		// texture stages, execute the surface, then re-upload the material CB so
		// the draw calls that follow get the correct bEnableDiffuse flag.
		if ( pPrimitive->primitiveKey() == PrimitiveSurface::staticPrimitiveKey() )
		{
			setupTextures();
			pPrimitive->setDevice( device() );
			pPrimitive->execute();		// surface failure is non-fatal
			pDevice->bindPerMaterialCB( pDevice->m_CurrentMatCB );
			continue;
		}

		pPrimitive->setDevice( device() );
		if ( !pPrimitive->execute() )
			return false;
	}

	return true;
}

//----------------------------------------------------------------------------

ShaderFloat4 PrimitiveMaterialD3D12::makeShaderFloat4( const Color & src )
{
	const float inv = 1.0f / 255.0f;
	return ShaderFloat4( src.m_R * inv, src.m_G * inv, src.m_B * inv, src.m_A * inv );
}

Color PrimitiveMaterialD3D12::makeColor( const ShaderFloat4 & src )
{
	return Color( (byte)(src.x * 255.0f), (byte)(src.y * 255.0f), (byte)(src.z * 255.0f), (byte)(src.w * 255.0f) );
}

int PrimitiveMaterialD3D12::sortSurfaces( Surface p1, Surface p2 )
{
	static int SURFACE_SORT_ORDER[] =
	{
		1, // DIFFUSE
		2, // LIGHTMAP
		0, // BUMPMAP
		3, // DARKMAP
		4, // DETAILMAP
		5, // GLOSSMAP
		6, // NORMALMAP
		7, // PARALLAXMAP
		8, // DECALMAP
		9, // SHADERMAP
	};

	return SURFACE_SORT_ORDER[ p1.m_eType ] - SURFACE_SORT_ORDER[ p2.m_eType ];
}

//------------------------------------------------------------------------------------
// EOF
