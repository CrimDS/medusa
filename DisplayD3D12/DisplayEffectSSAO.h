/*
	DisplayEffectSSAO.h - D3D12 version
	Screen-Space Ambient Occlusion post-processing effect.
	(c)2024 Palestar
*/

#ifndef DISPLAY_EFFECT_SSAO_D3D12_H
#define DISPLAY_EFFECT_SSAO_D3D12_H

#include "Display/DisplayEffect.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

//---------------------------------------------------------------------------------------------------

class DisplayEffectSSAOD3D12 : public DisplayEffect
{
public:
	DECLARE_WIDGET_CLASS();

	DisplayEffectSSAOD3D12();
	virtual ~DisplayEffectSSAOD3D12();

	virtual bool			preRender( DisplayDevice * pDevice );
	virtual bool			postRender( DisplayDevice * pDevice );
	virtual void			release();
	virtual void			onDeviceShutdown();

	// Tuning parameters
	float					m_fRadius;			// sample radius in view-space units
	float					m_fBias;			// depth bias
	float					m_fIntensity;		// AO strength (0..2)

private:
	bool					initSSAO( DisplayDeviceD3D12 * pDevice );
	void					drawFullscreenTriangle( DisplayDeviceD3D12 * pDevice );
	void					freeOwnedDescriptors();		// returns RTV/SRV slots to the device's heaps

	// Shader blobs
	ComPtr<ID3DBlob>		m_pVSBlob;
	ComPtr<ID3DBlob>		m_pPSSSAO;
	ComPtr<ID3DBlob>		m_pPSBlur;
	ComPtr<ID3DBlob>		m_pPSApply;

	// Root signature and PSOs
	ComPtr<ID3D12RootSignature>		m_pSSAORootSig;
	ComPtr<ID3D12PipelineState>		m_pSSAOPSO;
	ComPtr<ID3D12PipelineState>		m_pBlurPSO;
	ComPtr<ID3D12PipelineState>		m_pApplyPSO;	// multiplicative composite

	// AO render targets (half-res, R8)
	ComPtr<ID3D12Resource>	m_pAOTextures[2];	// [0] raw AO, [1] blurred AO
	UINT					m_nAORTVIndex[2];
	UINT					m_nAOSRVIndex[2];	// staging heap

	SizeInt					m_LastSize;
	SizeInt					m_AOSize;
	bool					m_bInitialized;
	bool					m_bFailed;

	// Cached device for the destructor's freeOwnedDescriptors() — see HDR.h
	// for the full rationale.  Set in initSSAO.
	DisplayDeviceD3D12 *	m_pCachedDevice;
};

//---------------------------------------------------------------------------------------------------

#endif

// EOF
