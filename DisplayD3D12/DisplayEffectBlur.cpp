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

// NOTE: DisplayEffectBlurD3D12 is an UNIMPLEMENTED STUB.  The DX9 path
// implemented motion blur (cross-frame ping-pong via m_pPreviousTexture).
// The DX12 port was scaffolded but never finished — pre/postRender no-op
// and report success, so scenes that request blur silently get nothing.
// IMPLEMENT_FACTORY is left in so the registration matches the engine's
// effect catalog; if you decide to drop the effect entirely, remove that
// macro and audit asset/scene references to "blur".

bool DisplayEffectBlurD3D12::preRender( DisplayDevice * /*pDevice*/ )
{
	return true;
}

bool DisplayEffectBlurD3D12::postRender( DisplayDevice * /*pDevice*/ )
{
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
