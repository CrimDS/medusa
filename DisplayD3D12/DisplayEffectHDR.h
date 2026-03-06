/*
	DisplayEffectHDR.h - D3D12 version
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

	int						m_nBloomLevels;
	int						m_nBloomSize;

	ComPtr<ID3D12Resource>	m_pHDRRenderTarget;
	ComPtr<ID3D12Resource>	m_pBloomTextures[2];
	UINT					m_nHDRRTVIndex;
	UINT					m_nHDRSRVIndex;
	UINT					m_nBloomRTVIndex[2];
	UINT					m_nBloomSRVIndex[2];
	SizeInt					m_LastSize;
	bool					m_bInitialized;
};

//---------------------------------------------------------------------------------------------------

#endif

// EOF
