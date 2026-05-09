/*
	DisplayEffectLensFlare.h - D3D12 version
	Anamorphic lens flare: wide horizontal streak + subtler vertical
	secondary at the sun's screen UV, plus a tight central halo.
	Composited additively into the HDR scene RT before bloom.  Renders
	only when the sun is on-screen and not occluded by a foreground body
	at its screen UV (a single depth-buffer sample at sunUV gates the
	pass).  When the sun is occluded, LimbGlow renders the silhouette
	rim glow instead, so the two effects naturally hand off.
	(c)2026 Palestar
*/

#ifndef DISPLAY_EFFECT_LENSFLARE_D3D12_H
#define DISPLAY_EFFECT_LENSFLARE_D3D12_H

#include "Display/DisplayEffect.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

//---------------------------------------------------------------------------------------------------

class DisplayEffectLensFlareD3D12 : public DisplayEffect
{
public:
	DECLARE_WIDGET_CLASS();

	DisplayEffectLensFlareD3D12();
	virtual ~DisplayEffectLensFlareD3D12();

	virtual bool			preRender( DisplayDevice * pDevice );
	virtual bool			postRender( DisplayDevice * pDevice );
	virtual void			release();

	// Tuning (matches CBLensFlare layout in LensFlare.hlsl).
	float					m_fIntensity;		// overall flare brightness — bloom amplifies the result, so this is conservative

private:
	bool					initLensFlare( DisplayDeviceD3D12 * pDevice );
	void					drawFullscreenTriangle( DisplayDeviceD3D12 * pDevice );

	// Compiled shader blobs (from LensFlare.hlsl)
	ComPtr<ID3DBlob>		m_pVSBlob;
	ComPtr<ID3DBlob>		m_pPSFlare;

	ComPtr<ID3D12RootSignature>		m_pRootSig;
	ComPtr<ID3D12PipelineState>		m_pFlarePSO;	// scene RT, ONE+ONE additive

	SizeInt					m_LastSize;
	bool					m_bInitialized;
	bool					m_bFailed;
};

//---------------------------------------------------------------------------------------------------

#endif

// EOF
