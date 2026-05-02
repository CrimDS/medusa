/*
	Progress.h

	Global progress-reporting hook.  Long-running operations (BC7/DXT
	encode, image import, etc.) call Progress::report(); the host app
	(Resourcer / DarkSpace) registers a Reporter callback at startup
	that updates a UI element and pumps Windows messages so the app
	doesn't appear hung.  When no Reporter is registered, report() is
	a cheap no-op.
	(c)2026 Palestar
*/

#ifndef PROGRESS_H
#define PROGRESS_H

#include "MedusaDll.h"

//---------------------------------------------------------------------------------------------------

class DLL Progress
{
public:
	// pCurrent/total: 0..total ratio.  total==0 means indeterminate.
	// pStatus: short label, e.g. "BC7 encoding".  May be NULL.
	typedef void ( *Reporter )( int current, int total, const char * pStatus );

	static void			setReporter( Reporter pFn );
	static void			report( int current, int total, const char * pStatus );

private:
	static Reporter		sm_pReporter;
};

#endif

//---------------------------------------------------------------------------------------------------
// EOF
