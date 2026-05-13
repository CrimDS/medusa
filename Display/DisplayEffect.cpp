/**
	@file DisplayEffect.cpp

	(c)2009 Palestar Inc
	@author Richard Lyle @date 9/5/2010 3:22:15 PM
*/

#include "DisplayEffect.h"

//---------------------------------------------------------------------------------------------------

IMPLEMENT_ABSTRACT_FACTORY( DisplayEffect, Widget );

// Default no-op base implementation.  Out-of-line (rather than inline in
// the header) so the symbol is actually compiled into and exported from
// Medusa.dll — consumers in other DLLs (DisplayD3D12.dll's effect
// hierarchy) reference it via __declspec(dllimport) in their vtables
// when they don't override this method.  See header for full rationale.
void DisplayEffect::onDeviceShutdown()
{
}

//---------------------------------------------------------------------------------------------------
//EOF
