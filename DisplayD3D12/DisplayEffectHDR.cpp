/*
	DisplayEffectHDR.cpp - D3D12 version
	(c)2024 Palestar
*/

#include "DisplayEffectHDR.h"

//---------------------------------------------------------------------------------------------------

IMPLEMENT_FACTORY( DisplayEffectHDRD3D12, DisplayEffect );

DisplayEffectHDRD3D12::DisplayEffectHDRD3D12() :
	m_nBloomLevels( 4 ),
	m_nBloomSize( 256 ),
	m_nHDRRTVIndex( UINT(-1) ),
	m_nHDRSRVIndex( UINT(-1) ),
	m_LastSize( 0, 0 ),
	m_bInitialized( false )
{
	memset( m_nBloomRTVIndex, 0xff, sizeof(m_nBloomRTVIndex) );
	memset( m_nBloomSRVIndex, 0xff, sizeof(m_nBloomSRVIndex) );
}

DisplayEffectHDRD3D12::~DisplayEffectHDRD3D12()
{
	release();
}

bool DisplayEffectHDRD3D12::preRender( DisplayDevice * pDevice )
{
	// TODO: Implement HDR pre-render for D3D12
	// This should redirect rendering to an HDR render target
	return true;
}

bool DisplayEffectHDRD3D12::postRender( DisplayDevice * pDevice )
{
	// TODO: Implement HDR post-render for D3D12
	// This should perform bright pass extraction, gaussian blur, and bloom combine
	return true;
}

void DisplayEffectHDRD3D12::release()
{
	m_pHDRRenderTarget.Reset();
	m_pBloomTextures[0].Reset();
	m_pBloomTextures[1].Reset();
	m_bInitialized = false;
}

//---------------------------------------------------------------------------------------------------
// EOF
