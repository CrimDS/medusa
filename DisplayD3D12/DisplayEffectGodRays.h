/*
	DisplayEffectGodRays.h - D3D12 version
	Volumetric light scattering post-process (radial blur from sun's
	screen-space position), composited additively into the HDR scene RT.
	(c)2024 Palestar
*/

#ifndef DISPLAY_EFFECT_GODRAYS_D3D12_H
#define DISPLAY_EFFECT_GODRAYS_D3D12_H

#include "Display/DisplayEffect.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

//---------------------------------------------------------------------------------------------------

class DisplayEffectGodRaysD3D12 : public DisplayEffect
{
public:
	DECLARE_WIDGET_CLASS();

	DisplayEffectGodRaysD3D12();
	virtual ~DisplayEffectGodRaysD3D12();

	virtual bool			preRender( DisplayDevice * pDevice );
	virtual bool			postRender( DisplayDevice * pDevice );
	virtual void			release();

	// Tuning (matches CBGodRays layout in GodRays.hlsl)
	float					m_fDensity;			// step spacing along ray (0.6 - 1.0 typical)
	float					m_fWeight;			// per-sample weight (0.3 - 0.6)
	float					m_fDecay;			// per-sample attenuation (0.95 - 0.99)
	float					m_fExposure;		// output scale
	float					m_fEclipseStrength;	// multiplicative darken applied to foreground occluders near the sun before the additive ray composite, so occluders read as silhouettes against the glow instead of being bleached by subsequent HDR bloom bleed. 0 = no eclipse (old behaviour), 1 = occluders at sunUV go fully black.

private:
	bool					initGodRays( DisplayDeviceD3D12 * pDevice );
	void					drawFullscreenTriangle( DisplayDeviceD3D12 * pDevice );

	// Compiled shader blobs (from GodRays.hlsl)
	ComPtr<ID3DBlob>		m_pVSBlob;
	ComPtr<ID3DBlob>		m_pPSRays;
	ComPtr<ID3DBlob>		m_pPSEclipse;
	ComPtr<ID3DBlob>		m_pPSComposite;

	ComPtr<ID3D12RootSignature>		m_pRootSig;
	ComPtr<ID3D12PipelineState>		m_pRaysPSO;			// scene → rays RT, no blend
	ComPtr<ID3D12PipelineState>		m_pEclipsePSO;		// scene RT, DST *= SRC multiplicative darken (occluder eclipse)
	ComPtr<ID3D12PipelineState>		m_pCompositePSO;	// rays RT → scene RT, ONE+ONE additive

	// Quarter-res ray accumulation RT
	ComPtr<ID3D12Resource>	m_pRaysRT;
	UINT					m_nRaysRTVIndex;
	UINT					m_nRaysSRVIndex;	// staging heap index

	SizeInt					m_LastSize;
	SizeInt					m_RaysSize;
	bool					m_bInitialized;
	bool					m_bFailed;
};

//---------------------------------------------------------------------------------------------------

#endif

// EOF
