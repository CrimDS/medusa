/*
	Progress.cpp
	(c)2026 Palestar
*/

#define MEDUSA_DLL
#include "Standard/Progress.h"

//---------------------------------------------------------------------------------------------------

Progress::Reporter Progress::sm_pReporter = 0;

void Progress::setReporter( Reporter pFn )
{
	sm_pReporter = pFn;
}

void Progress::report( int current, int total, const char * pStatus )
{
	if ( sm_pReporter )
		sm_pReporter( current, total, pStatus );
}

//---------------------------------------------------------------------------------------------------
// EOF
