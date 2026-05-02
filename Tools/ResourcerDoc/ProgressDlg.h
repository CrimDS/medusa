/*
	ProgressDlg.h  (ProgressBridge — bridges Progress::report to the
	existing MFC CProgressDlg in ProgDlg.h)

	The Resourcer's InitInstance registers ProgressBridge::report as the
	global Progress::Reporter callback.  Long-running operations (BC7 /
	DXT encode, image import) call Progress::report; this bridge lazily
	creates a singleton CProgressDlg, updates its status text + bar, and
	pumps Windows messages so the host UI stays responsive.  When the
	work completes (current >= total), the dialog is destroyed.
	(c)2026 Palestar
*/

#ifndef PROGRESS_BRIDGE_H
#define PROGRESS_BRIDGE_H

#include "Tools/ResourcerDoc/ResourcerDll.h"

//---------------------------------------------------------------------------------------------------

class DLL ProgressBridge
{
public:
	// Reporter for Progress::setReporter — main-thread only.
	static void			report( int current, int total, const char * pStatus );

	// Tear down the singleton on app exit.
	static void			shutdown();
};

#endif

//---------------------------------------------------------------------------------------------------
// EOF
