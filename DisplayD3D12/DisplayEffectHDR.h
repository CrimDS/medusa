/*
	DisplayEffectHDR.h - D3D12 version
	Karis/Unreal-style bloom post-processing effect — progressive mip-chain
	downsample (13-tap filtered) + tent upsample.
	(c)2024 Palestar
*/

#ifndef DISPLAY_EFFECT_HDR_D3D12_H
#define DISPLAY_EFFECT_HDR_D3D12_H

#include "Display/DisplayEffect.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

//---------------------------------------------------------------------------------------------------

class DisplayEffectHDRD3D12 : public DisplayEffect
{
public:
	DECLARE_WIDGET_CLASS();

	// Bloom mip chain depth.  6 mips at 1080p covers roughly half-screen bloom
	// spread (widest mip = 1/64 screen width).  8 is a hard cap that still
	// leaves the smallest mip at ~15 px on a 4K target.
	enum { MAX_MIPS = 8 };

	DisplayEffectHDRD3D12();
	virtual ~DisplayEffectHDRD3D12();

	virtual bool			preRender( DisplayDevice * pDevice );
	virtual bool			postRender( DisplayDevice * pDevice );
	virtual void			release();

	// Tuning parameters
	int						m_nMipCount;			// active bloom mip count (clamped to MAX_MIPS)
	float					m_fBloomScale;			// final composite intensity (re-read from settings per frame)
	float					m_fBrightThreshold;		// luminance threshold for bright pass

private:
	bool					initBloom( DisplayDeviceD3D12 * pDevice );
	void					drawFullscreenTriangle( DisplayDeviceD3D12 * pDevice );

	// Compiled shader blobs (from PostProcess.hlsl with different entry points)
	ComPtr<ID3DBlob>		m_pVSBlob;
	ComPtr<ID3DBlob>		m_pPSBrightPass;
	ComPtr<ID3DBlob>		m_pPSDownsample;
	ComPtr<ID3DBlob>		m_pPSUpsample;
	ComPtr<ID3DBlob>		m_pPSScale;

	// Root signature and PSOs for bloom passes
	ComPtr<ID3D12RootSignature>		m_pBloomRootSig;
	ComPtr<ID3D12PipelineState>		m_pBrightPassPSO;		// writes mip0, no blend
	ComPtr<ID3D12PipelineState>		m_pDownsamplePSO;		// writes mip[i], no blend
	ComPtr<ID3D12PipelineState>		m_pUpsamplePSO;			// writes mip[i-1], ONE+ONE additive
	ComPtr<ID3D12PipelineState>		m_pAdditivePSO;			// final composite into scene RT

	// Bloom mip chain (one RT per mip, independently sized RTVs + SRVs)
	ComPtr<ID3D12Resource>	m_pMipRTs[MAX_MIPS];
	UINT					m_nMipRTVIndex[MAX_MIPS];
	UINT					m_nMipSRVIndex[MAX_MIPS];		// staging heap indices
	SizeInt					m_MipSizes[MAX_MIPS];

	SizeInt					m_LastSize;
	int						m_LastShaderDetail;
	bool					m_bInitialized;
	bool					m_bBloomFailed;
};

//---------------------------------------------------------------------------------------------------

#endif

// EOF
