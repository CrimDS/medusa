/*
	DisplayEffectExposure.h - D3D12 version
	Auto-exposure post-process — samples scene RT, computes geometric-mean
	luminance, EMA-blends toward a target exposure multiplier, and publishes
	the result as a 1x1 R32F SRV that DisplayDeviceD3D12::applyFXAA binds at
	t1 for pre-tonemap scaling.
	(c)2024 Palestar
*/

#ifndef DISPLAY_EFFECT_EXPOSURE_D3D12_H
#define DISPLAY_EFFECT_EXPOSURE_D3D12_H

#include "Display/DisplayEffect.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

//---------------------------------------------------------------------------------------------------

class DisplayEffectExposureD3D12 : public DisplayEffect
{
public:
	DECLARE_WIDGET_CLASS();

	DisplayEffectExposureD3D12();
	virtual ~DisplayEffectExposureD3D12();

	virtual bool			preRender( DisplayDevice * pDevice );
	virtual bool			postRender( DisplayDevice * pDevice );
	virtual void			release();
	virtual void			onDeviceShutdown();

	// Tuning (match CBExposure layout in Exposure.hlsl)
	float					m_fAdaptRate;		// per-second EMA rate — effect scales by dt to per-call
	float					m_fKey;				// middle-grey target (Reinhard 0.18 default)
	float					m_fMinExposure;		// clamp floor
	float					m_fMaxExposure;		// clamp ceiling

private:
	bool					initExposure( DisplayDeviceD3D12 * pDevice );
	void					freeOwnedDescriptors();		// returns RTV/SRV slots to the device's heaps

	ComPtr<ID3DBlob>		m_pVSBlob;
	ComPtr<ID3DBlob>		m_pPSAdapt;

	ComPtr<ID3D12RootSignature>		m_pRootSig;
	ComPtr<ID3D12PipelineState>		m_pAdaptPSO;

	// Ping-pong 1x1 R32F exposure RTs.  Frame N reads m_pExposureRT[N%2]
	// (previous written by frame N-1) and writes m_pExposureRT[(N+1)%2].
	ComPtr<ID3D12Resource>	m_pExposureRT[2];
	UINT					m_nExposureRTVIndex[2];
	UINT					m_nExposureSRVIndex[2];		// staging heap indices

	int						m_nFrameIdx;		// counter for ping-pong selection
	double					m_fLastTickSec;		// wall-clock of previous postRender (for dt)
	bool					m_bInitialized;
	bool					m_bFailed;

	// Cached device for the destructor's freeOwnedDescriptors() — see HDR.h.
	DisplayDeviceD3D12 *	m_pCachedDevice;
};

//---------------------------------------------------------------------------------------------------

#endif

// EOF
