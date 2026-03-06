/*
	DisplayEffectBlur.cpp - D3D12 version
	(c)2024 Palestar
*/

#include "DisplayEffectBlur.h"

//---------------------------------------------------------------------------------------------------

IMPLEMENT_FACTORY( DisplayEffectBlurD3D12, DisplayEffect );

DisplayEffectBlurD3D12::DisplayEffectBlurD3D12() :
	m_fBlur( 0.95f ),
	m_nBlurRTVIndex( UINT(-1) ),
	m_nBlurSRVIndex( UINT(-1) ),
	m_nPrevSRVIndex( UINT(-1) ),
	m_LastSize( 0, 0 ),
	m_bInitialized( false )
{}

DisplayEffectBlurD3D12::~DisplayEffectBlurD3D12()
{
	release();
}

bool DisplayEffectBlurD3D12::preRender( DisplayDevice * pDevice )
{
	// TODO: Implement blur pre-render for D3D12
	// This should redirect rendering to an offscreen target
	return true;
}

bool DisplayEffectBlurD3D12::postRender( DisplayDevice * pDevice )
{
	// TODO: Implement blur post-render for D3D12
	// This should blend current frame with previous frame
	return true;
}

void DisplayEffectBlurD3D12::release()
{
	m_pBlurRT.Reset();
	m_pPreviousTexture.Reset();
	m_bInitialized = false;
}

//---------------------------------------------------------------------------------------------------
// EOF
