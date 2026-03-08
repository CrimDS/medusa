/*
	DisplayEffectHDR.h - D3D12 version
	Bloom post-processing effect.
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

	DisplayEffectHDRD3D12();
	virtual ~DisplayEffectHDRD3D12();

	virtual bool			preRender( DisplayDevice * pDevice );
	virtual bool			postRender( DisplayDevice * pDevice );
	virtual void			release();

	// Tuning parameters
	int						m_nBloomLevels;			// number of blur iterations
	int						m_nBloomSize;			// bloom RT divisor (e.g. 4 = 1/4 screen)
	float					m_fBloomScale;			// bloom intensity
	float					m_fBrightThreshold;		// luminance threshold for bright pass

private:
	bool					initBloom( DisplayDeviceD3D12 * pDevice );
	void					drawFullscreenTriangle( DisplayDeviceD3D12 * pDevice );

	// Compiled shader blobs (from PostProcess.hlsl with different entry points)
	ComPtr<ID3DBlob>		m_pVSBlob;
	ComPtr<ID3DBlob>		m_pPSBrightPass;
	ComPtr<ID3DBlob>		m_pPSHorzBlur;
	ComPtr<ID3DBlob>		m_pPSVertBlur;
	ComPtr<ID3DBlob>		m_pPSScale;

	// Root signature and PSOs for bloom passes
	ComPtr<ID3D12RootSignature>		m_pBloomRootSig;
	ComPtr<ID3D12PipelineState>		m_pBrightPassPSO;
	ComPtr<ID3D12PipelineState>		m_pHorzBlurPSO;
	ComPtr<ID3D12PipelineState>		m_pVertBlurPSO;
	ComPtr<ID3D12PipelineState>		m_pAdditivePSO;		// additive composite

	// Bloom render targets (ping-pong, 1/N screen size)
	ComPtr<ID3D12Resource>	m_pBloomTextures[2];
	UINT					m_nBloomRTVIndex[2];
	UINT					m_nBloomSRVIndex[2];		// staging heap indices

	SizeInt					m_LastSize;
	SizeInt					m_BloomSize;
	bool					m_bInitialized;
	bool					m_bBloomFailed;
};

//---------------------------------------------------------------------------------------------------

#endif

// EOF
