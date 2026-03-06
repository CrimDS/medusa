/*
	DisplayEffectBlur.h - D3D12 version
	(c)2024 Palestar
*/

#ifndef DISPLAY_EFFECT_BLUR_D3D12_H
#define DISPLAY_EFFECT_BLUR_D3D12_H

#include "Display/DisplayEffect.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

//---------------------------------------------------------------------------------------------------

class DisplayEffectBlurD3D12 : public DisplayEffect
{
public:
	DECLARE_WIDGET_CLASS();

	DisplayEffectBlurD3D12();
	virtual ~DisplayEffectBlurD3D12();

	virtual bool			preRender( DisplayDevice * pDevice );
	virtual bool			postRender( DisplayDevice * pDevice );
	virtual void			release();

	float					m_fBlur;

	ComPtr<ID3D12Resource>	m_pBlurRT;
	ComPtr<ID3D12Resource>	m_pPreviousTexture;
	UINT					m_nBlurRTVIndex;
	UINT					m_nBlurSRVIndex;
	UINT					m_nPrevSRVIndex;
	SizeInt					m_LastSize;
	bool					m_bInitialized;
};

//---------------------------------------------------------------------------------------------------

#endif

// EOF
