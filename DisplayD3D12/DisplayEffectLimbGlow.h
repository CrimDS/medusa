/*
	DisplayEffectLimbGlow.h - D3D12 version
	Cinematic limb-glow effect: bright halo at the silhouette of a foreground
	celestial body when the sun is at, or close to behind, that body's
	screen-space disc.  Composited additively into the HDR scene RT before
	bloom.
	(c)2024 Palestar
*/

#ifndef DISPLAY_EFFECT_LIMBGLOW_D3D12_H
#define DISPLAY_EFFECT_LIMBGLOW_D3D12_H

#include "Display/DisplayEffect.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

//---------------------------------------------------------------------------------------------------

class DisplayEffectLimbGlowD3D12 : public DisplayEffect
{
public:
	DECLARE_WIDGET_CLASS();

	DisplayEffectLimbGlowD3D12();
	virtual ~DisplayEffectLimbGlowD3D12();

	virtual bool			preRender( DisplayDevice * pDevice );
	virtual bool			postRender( DisplayDevice * pDevice );
	virtual void			release();

	// Tuning (matches CBLimbGlow layout in LimbGlow.hlsl).
	float					m_fWeight;			// rim glow intensity scale (0.3 - 1.0 typical)
	float					m_fExposure;		// output scale

private:
	bool					initLimbGlow( DisplayDeviceD3D12 * pDevice );
	void					drawFullscreenTriangle( DisplayDeviceD3D12 * pDevice );

	// Compiled shader blobs (from LimbGlow.hlsl)
	ComPtr<ID3DBlob>		m_pVSBlob;
	ComPtr<ID3DBlob>		m_pPSGlow;			// PS_LimbGlow — per-occluder rim halo into half-res RT
	ComPtr<ID3DBlob>		m_pPSComposite;		// PS_Composite — rim glow RT → scene RT (additive)

	ComPtr<ID3D12RootSignature>		m_pRootSig;
	ComPtr<ID3D12PipelineState>		m_pGlowPSO;			// scene → rim-glow RT, no blend
	ComPtr<ID3D12PipelineState>		m_pCompositePSO;	// rim-glow RT → scene RT, ONE+ONE additive

	// Half-res rim glow accumulation RT
	ComPtr<ID3D12Resource>	m_pGlowRT;
	UINT					m_nGlowRTVIndex;
	UINT					m_nGlowSRVIndex;	// staging heap index

	SizeInt					m_LastSize;
	SizeInt					m_GlowSize;
	int						m_LastShaderDetail;
	bool					m_bInitialized;
	bool					m_bFailed;
};

//---------------------------------------------------------------------------------------------------

#endif

// EOF
