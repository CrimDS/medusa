/*
	PrimitiveMaterialD3D12.cpp
	(c)2024 Palestar
*/

#include "Debug/Assert.h"
#include "Debug/Trace.h"
#include "Debug/Profile.h"
#include "Standard/AutoLock.h"
#include "Render3D/RenderContext.h"
#include "PrimitiveMaterialD3D12.h"
#include "PrimitiveSurfaceD3D12.h"
#include "PrimitiveFactory.h"
#include "DisplayD3D12/PrimitiveSetTransformD3D12.h"

#include <algorithm>
#include <climits>
#include <vector>

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveMaterialD3D12 );

PrimitiveMaterialD3D12::PrimitiveMaterialD3D12()
{
	m_nPass = DisplayDevice::PRIMARY;
	m_Blending = NONE;
	m_DoubleSided = false;
	m_LightEnable = true;
	m_bPushed = false;
	m_nFirstClaimChild = INT_MAX;
	m_nFilterMode = FILTER_ON;
	m_bUpdateShaders = false;
	m_bSurfacesSortDirty = false;

	// PBR scalar defaults — matte grey plastic.  Overridden via
	// setPBRMaterial from Material::createDevicePrimitives.
	m_Roughness = 0.5f;
	m_Metallic = 0.0f;
	m_AO = 1.0f;

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

	// PBR is shader-gated: the bound shader's name decides whether the PBR
	// fields in CBPerMaterial are consumed.  Cheap string compare here; the
	// CB is written every draw so per-draw branching is the wrong axis to
	// optimise on.  Anything that opts in by naming PBR.hlsl gets the PBR
	// scalars + ORM/normal slot bindings.  Everything else (Default.hlsl,
	// PassThrough, Star, Cloak, Planet, ...) ignores them as plain padding.
	const bool bShaderIsPBR = ( m_sShader.length() > 0 ) &&
		( strcmp( m_sShader, "Shaders/PBR.hlsl" ) == 0 );

	// Determine if we use the passthrough or full lighting shader.
	// If a custom shader is explicitly set, always use the full pipeline so it gets loaded.
	//
	// SECONDARY pass (translucent particles, beams, trails) skips the per-light
	// pipeline by default — see DisplayDevice::sm_bLightSecondaryPass.  The
	// per-light loop below renders geometry once per light, so for ~3 active
	// lights and ~87 translucent materials/frame that's ~348 executes/frame
	// vs 87 with passthrough.  Translucent geometry is almost always emissive
	// and doesn't read meaningfully different under per-light shading; the
	// flag is exposed if a particular emitter actually needs it.
	bool bHasCustomShader = (m_sShader.length() > 0);
	bool bUsePassthrough = !bHasCustomShader
		&& (DisplayDevice::sm_bUseFixedFunction
			|| !m_LightEnable
			|| lights.size() == 0
			|| m_Blending == PrimitiveMaterial::ADDITIVE
			|| (m_nPass == DisplayDevice::SECONDARY && !DisplayDevice::sm_bLightSecondaryPass));

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
		pDevice->m_CurrentMatCB.fMatRoughness = m_Roughness;
		pDevice->m_CurrentMatCB.fMatMetallic = m_Metallic;
		pDevice->m_CurrentMatCB.fMatAO = m_AO;
		pDevice->m_CurrentMatCB.bEnablePBR = bShaderIsPBR ? 1 : 0;

		setupBlending();
		{
			PROFILE_START( "Material::passthrough:setupTextures" );
			bool ok = setupTextures();
			PROFILE_END();
			if ( !ok )
				return false;
		}
		{
			PROFILE_START( "Material::passthrough:bindMatCB" );
			pDevice->bindPerMaterialCB( pDevice->m_CurrentMatCB );
			PROFILE_END();
		}
		{
			PROFILE_START( "Material::passthrough:executeChildren" );
			bool ok = executeChildren();
			PROFILE_END();
			if ( !ok )
				return false;
		}
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
		pDevice->m_CurrentMatCB.fMatRoughness = m_Roughness;
		pDevice->m_CurrentMatCB.fMatMetallic = m_Metallic;
		pDevice->m_CurrentMatCB.fMatAO = m_AO;
		pDevice->m_CurrentMatCB.bEnablePBR = bShaderIsPBR ? 1 : 0;

		if ( !setupTextures() )
			return false;

		// PBR IBL — device-owned BRDF LUT (t5) + prefiltered env cube (t6),
		// copied into this material's slab once and reused across light passes.
		// bEnableSpecIBL gates the shader's indirect-specular sample; cleared
		// alongside bEnableAmbient on additive passes so indirect doesn't
		// double-count.
		const bool bSpecIBLActive = bShaderIsPBR && pDevice->m_bPBRIBLReady
			&& pDevice->m_nBRDFLUTSRVStagingIndex != UINT(-1)
			&& pDevice->m_nEnvCubeSRVStagingIndex != UINT(-1);
		if ( bSpecIBLActive )
		{
			pDevice->m_CurrentMatCB.bEnableSpecIBL = 1;

			// BRDF LUT → t5
			UINT lutSlot = pDevice->m_nSRVTextureBase + 5;
			if ( pDevice->isSRVSlotCurrent( lutSlot, pDevice->m_nBRDFLUTSRVStagingIndex ) )
			{
				++pDevice->m_nSRVCopiesSkipped;
			}
			else
			{
				D3D12_CPU_DESCRIPTOR_HANDLE srcHandle = pDevice->m_SRVStagingHeap.GetCPUHandle( pDevice->m_nBRDFLUTSRVStagingIndex );
				D3D12_CPU_DESCRIPTOR_HANDLE dstHandle = pDevice->getSRVCPUHandle( lutSlot );
				pDevice->getDevice()->CopyDescriptorsSimple( 1, dstHandle, srcHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
				pDevice->recordSRVSlot( lutSlot, pDevice->m_nBRDFLUTSRVStagingIndex );
				++pDevice->m_nSRVCopies;
			}

			// Env cube → t6 (TextureCube SRV — type matches HLSL declaration)
			UINT cubeSlot = pDevice->m_nSRVTextureBase + 6;
			if ( pDevice->isSRVSlotCurrent( cubeSlot, pDevice->m_nEnvCubeSRVStagingIndex ) )
			{
				++pDevice->m_nSRVCopiesSkipped;
			}
			else
			{
				D3D12_CPU_DESCRIPTOR_HANDLE srcHandle = pDevice->m_SRVStagingHeap.GetCPUHandle( pDevice->m_nEnvCubeSRVStagingIndex );
				D3D12_CPU_DESCRIPTOR_HANDLE dstHandle = pDevice->getSRVCPUHandle( cubeSlot );
				pDevice->getDevice()->CopyDescriptorsSimple( 1, dstHandle, srcHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
				pDevice->recordSRVSlot( cubeSlot, pDevice->m_nEnvCubeSRVStagingIndex );
				++pDevice->m_nSRVCopies;
			}
		}

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
				// vLightDiffuse / vLightSpecular are author-time sRGB.  In sRGB-correct
				// mode they multiply linear texture samples in the shader, so they
				// must arrive linear.  srgbColorToLinear() leaves alpha untouched
				// (alpha is already a linear opacity, not a perceptual colour).
				// g_fLightIntensityScale boosts the linearised RGB to recover the
				// "apparent light energy" the legacy gamma-wrong path delivered —
				// keeps lit faces bright relative to dark space without lifting
				// ambient.
				ShaderFloat4 ldif = srgbColorToLinear( ShaderFloat4( light.r, light.g, light.b, light.a ) );
				ShaderFloat4 lspc = srgbColorToLinear( ShaderFloat4( light.specR, light.specG, light.specB, light.specA ) );
				ldif.x *= g_fLightIntensityScale; ldif.y *= g_fLightIntensityScale; ldif.z *= g_fLightIntensityScale;
				lspc.x *= g_fLightIntensityScale; lspc.y *= g_fLightIntensityScale; lspc.z *= g_fLightIntensityScale;
				lightCB.vLightDiffuse  = ldif;
				lightCB.vLightSpecular = lspc;
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

					// Copy shadow map SRV into slot t7, skipping the copy when that
					// slot already holds this exact staging index.
					UINT smDestSlot = pDevice->m_nSRVTextureBase + 7;
					if ( pDevice->isSRVSlotCurrent( smDestSlot, pDevice->m_nShadowMapSRVStagingIndex ) )
					{
						++pDevice->m_nSRVCopiesSkipped;
					}
					else
					{
						D3D12_CPU_DESCRIPTOR_HANDLE srcHandle = pDevice->m_SRVStagingHeap.GetCPUHandle( pDevice->m_nShadowMapSRVStagingIndex );
						D3D12_CPU_DESCRIPTOR_HANDLE dstHandle = pDevice->getSRVCPUHandle( smDestSlot );
						pDevice->getDevice()->CopyDescriptorsSimple( 1, dstHandle, srcHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
						pDevice->recordSRVSlot( smDestSlot, pDevice->m_nShadowMapSRVStagingIndex );
						++pDevice->m_nSRVCopies;
					}

					// Re-bind SRV table (no-op when the base is unchanged from the
					// previous surface/material bind within this material's slot group).
					pDevice->bindSRVTableIfChanged( pDevice->m_nSRVTextureBase );
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
					pDevice->m_CurrentMatCB.bEnableSpecIBL = 0;
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
		// Lock the per-frame deferred list — clear() can run on worker threads
		// when smart-ref chains release primitives during parallel work.
		AutoLock lock( &pDevice->m_DeferredPrimsLock );
		Array< DevicePrimitive::Ref > & deferred = pDevice->m_DeferredPrimitives[ pDevice->m_nFrameIndex ];
		for ( int i = 0; i < m_Children.size(); ++i )
		{
			if ( m_Children[i] )
				deferred.push( m_Children[i] );
		}
	}
	m_Children.release();
	m_ChildOrder.release();
	m_TopTransform = NULL;
	m_bPushed = false;
	m_nFirstClaimChild = INT_MAX;
}

void PrimitiveMaterialD3D12::release()
{
	m_Children.release();
	m_ChildOrder.release();
	m_Surfaces.release();
	m_TopTransform = NULL;
	m_bPushed = false;
	m_nFirstClaimChild = INT_MAX;

	m_nPass = DisplayDevice::PRIMARY;
	m_Blending = NONE;
	m_DoubleSided = false;
	m_LightEnable = true;
	m_nFilterMode = FILTER_ON;
	m_bUpdateShaders = false;
	m_bForceDepthWrite = false;
	m_bSurfacesSortDirty = false;
	m_pShader = NULL;
	m_sShader = "";

	m_Roughness = 0.5f;
	m_Metallic = 0.0f;
	m_AO = 1.0f;

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

void PrimitiveMaterialD3D12::setForceDepthWrite( bool bForce )
{
	m_bForceDepthWrite = bForce;
}

void PrimitiveMaterialD3D12::setPBRMaterial( float roughness, float metallic, float ao )
{
	m_Roughness = roughness;
	m_Metallic = metallic;
	m_AO = ao;
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

	m_bSurfacesSortDirty = true;
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
	// Tag every addChild with the current parallel-dispatch child index so
	// executeChildren can stable-sort into traversal order.  -1 on the main
	// thread / outside parallel dispatch — ties resolve stably to push order.
	const int nChildOrder = RenderContext::currentPreRenderChildIndex();

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
				const int nLast = m_Children.size() - 1;
				const int nLastOrder = ( nLast < m_ChildOrder.size() ) ? m_ChildOrder[ nLast ] : -1;
				// Only collapse back-to-back transforms when they came from the
				// SAME caller (same preRender child index).  In serial mode both
				// are -1 and the original peephole still fires.  In parallel mode
				// two workers can push transforms back-to-back for *different*
				// children; collapsing those drops the first child's transform
				// and renders its geometry with the second child's matrix —
				// visible as the same mesh at two rotations.
				if ( nLastOrder == nChildOrder )
				{
					m_Children[ nLast ] = pPrimitive;
					if ( nLast < m_ChildOrder.size() )
						m_ChildOrder[ nLast ] = nChildOrder;
					return nLast;
				}
			}
		}
	}

	m_Children.push( pPrimitive );
	m_ChildOrder.push( nChildOrder );
	return m_Children.size() - 1;
}

void PrimitiveMaterialD3D12::removeChild( int n )
{
	if ( m_Children.isValid( n ) )
	{
		m_Children.remove( n );
		if ( n < m_ChildOrder.size() )
			m_ChildOrder.remove( n );
	}
}

void PrimitiveMaterialD3D12::clearChildren()
{
	m_Children.release();
	m_ChildOrder.release();
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
	pDevice->m_bCurrentForceDepthWrite = m_bForceDepthWrite;
}

bool PrimitiveMaterialD3D12::setupTextures()
{
	// Reset texture enables on the current material CB before surfaces set them
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
	pDevice->m_CurrentMatCB.bEnableDiffuse    = 0;
	pDevice->m_CurrentMatCB.bEnableLightMap   = 0;
	pDevice->m_CurrentMatCB.bEnableBumpMap    = 0;
	pDevice->m_CurrentMatCB.bEnableORMMap     = 0;
	pDevice->m_CurrentMatCB.bEnableNormalMap  = 0;
	pDevice->m_CurrentMatCB.bEnableSpecIBL    = 0;
	pDevice->m_CurrentMatCB.bFlipNormalY      = 0;
	pDevice->m_nTextureStage = 0;

	// Allocate 8 fresh contiguous SRV slots for this material draw (t0-t7).
	// Slots 0-2 are diffuse/lightmap/bumpmap; slot 7 is shadow map.
	// Must cover the full descriptor table range declared in the root signature (8 slots).
	// Wrap BEFORE allocation to ensure all 8 slots fit within the heap.
	// Slots 0-7 are permanent null SRVs; allocate from slot 8 onward.
	//
	// Lock-free CAS loop: claim a contiguous 8-slot range, wrapping to slot 8
	// if the current head can't fit 8 more slots.  Wrap-on-exhaust requires
	// read-modify-write rather than a simple fetch_add.  Single-threaded
	// callers spin at most once.  When the ring wraps, earlier materials'
	// descriptors get overwritten — same trade-off as the old serial version.
	const UINT slabBase = (UINT)pDevice->m_nFrameIndex * DisplayDeviceD3D12::MAX_SRV_DESCRIPTORS;
	const UINT slabEnd  = slabBase + DisplayDeviceD3D12::MAX_SRV_DESCRIPTORS;
	UINT current = pDevice->m_nSRVFrameOffset.load( std::memory_order_relaxed );
	UINT base;
	for (;;)
	{
		const bool wrap = ( current + 8 > slabEnd );
		base = wrap ? ( slabBase + 8u ) : current;
		const UINT next = base + 8;
		if ( pDevice->m_nSRVFrameOffset.compare_exchange_weak(
				current, next,
				std::memory_order_acq_rel, std::memory_order_relaxed ) )
		{
			if ( wrap )
			{
				static bool s_warned = false;
				if ( !s_warned )
				{
					s_warned = true;
					TRACE( "WARN: SRV ring wrap in material setupTextures at frame-offset=%u "
						"(MAX=%u). Mid-frame wrap = texture corruption.",
						current, (UINT)DisplayDeviceD3D12::MAX_SRV_DESCRIPTORS );
				}
			}
			break;
		}
	}
	pDevice->m_nSRVTextureBase = base;

	// Lazy-sort surfaces if any were added since the last execute.
	// Cheaper than re-sorting on every addSurface() (which used to be
	// O(N log N) per call = O(N^2 log N) to populate a material).
	if ( m_bSurfacesSortDirty )
	{
		m_Surfaces.qsort( sortSurfaces );
		m_bSurfacesSortDirty = false;
	}

	for ( int i = 0; i < m_Surfaces.size(); i++ )
	{
		Surface & surface = m_Surfaces[i];
		if ( !surface.m_pSurface.valid() )
			continue;
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

	const int nChildren = m_Children.size();

	// Build an index permutation that visits children in traversal order (by
	// m_ChildOrder, stable).  When sm_bParallelPreRender is on, two workers
	// adding children to the same shared material interleave in lock-acquire
	// order — flicker on SECONDARY-pass transparency.  Sorting by m_ChildOrder
	// restores the order serial rendering produces.  When parallel is off,
	// every entry is -1 and stable_sort is a near no-op on already-sorted data.
	//
	// executeChildren runs on the render thread only; a thread_local scratch
	// avoids the heap alloc that used to fire per material per frame.
	thread_local std::vector<int> order;
	order.resize( nChildren );
	for ( int i = 0; i < nChildren; ++i )
		order[i] = i;
	if ( nChildren == m_ChildOrder.size() )
	{
		std::stable_sort( order.begin(), order.end(),
			[this]( int a, int b )
			{
				return m_ChildOrder[a] < m_ChildOrder[b];
			} );
	}

	for ( int pos = 0; pos < nChildren; ++pos )
	{
		const int i = order[pos];
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
	// Author-time material colours (vMatDiffuse/Specular/Ambient/Emissive)
	// are sRGB.  In the sRGB-correct pipeline they multiply linearised
	// texture samples and accumulate into the linear scene RT, so they
	// must arrive linear too.  Alpha is already linear (opacity), so
	// linearise RGB only.  srgbToLinear() is a no-op when the pipeline is
	// disabled, which makes this call site safe regardless of the global
	// flag.
	const float inv = 1.0f / 255.0f;
	return srgbColorToLinear( ShaderFloat4(
		src.m_R * inv, src.m_G * inv, src.m_B * inv, src.m_A * inv ) );
}

Color PrimitiveMaterialD3D12::makeColor( const ShaderFloat4 & src )
{
	return Color( (byte)(src.x * 255.0f), (byte)(src.y * 255.0f), (byte)(src.z * 255.0f), (byte)(src.w * 255.0f) );
}

int PrimitiveMaterialD3D12::sortSurfaces( Surface p1, Surface p2 )
{
	static int SURFACE_SORT_ORDER[] =
	{
		2, // DIFFUSE
		3, // LIGHTMAP
		0, // BUMPMAP (legacy heightfield — bind first so PBR shader can override)
		4, // DARKMAP
		5, // DETAILMAP
		6, // GLOSSMAP
		1, // NORMALMAP (tangent-space PBR normal — bind early)
		7, // PARALLAXMAP
		8, // DECALMAP
		9, // SHADERMAP
		10,// ORMMAP
	};

	return SURFACE_SORT_ORDER[ p1.m_eType ] - SURFACE_SORT_ORDER[ p2.m_eType ];
}

//------------------------------------------------------------------------------------
// EOF
