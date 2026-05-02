/*
	ProgressDlg.cpp
	(c)2026 Palestar
*/

#include "stdafx.h"
#include "resource.h"		// CG_IDD_PROGRESS / CG_IDC_PROGDLG_* (used by ProgDlg.h's enum IDD)
#include "ProgressDlg.h"
#include "ProgDlg.h"		// existing MFC CProgressDlg (Component Gallery)

//---------------------------------------------------------------------------------------------------

static CProgressDlg *	s_pDlg = 0;
static DWORD			s_dwOpStartTick = 0;	// when the current op started reporting
static bool				s_bInOp = false;		// reports have been arriving
static int				s_nLastTotal = 0;
static const DWORD		SHOW_AFTER_MS = 250;	// don't pop a dialog for ops shorter than this

static void EnsureCreated()
{
	if ( s_pDlg )
		return;
	s_pDlg = new CProgressDlg();
	if ( ! s_pDlg->Create( AfxGetMainWnd() ) )
	{
		delete s_pDlg;
		s_pDlg = 0;
		return;
	}
	s_pDlg->SetRange( 0, 1000 );
}

void ProgressBridge::report( int current, int total, const char * pStatus )
{
	const DWORD now = ::GetTickCount();

	// Treat each report as part of the current "op".  Op start = first
	// report after the previous op completed (s_bInOp was reset).  We
	// keep the dialog object alive once created — just hide/show it
	// across ops — so back-to-back medium ops (e.g. mip chain encode,
	// many buffers in one load) don't flicker create/destroy.  Short
	// ops never trip SHOW_AFTER_MS so the dialog stays hidden.
	if ( ! s_bInOp )
	{
		s_dwOpStartTick = now;
		s_bInOp = true;
	}

	const bool bDone = ( total > 0 && current >= total );
	const DWORD elapsed = now - s_dwOpStartTick;

	if ( bDone )
	{
		// MUST destroy (not hide) so CProgressDlg::~CProgressDlg's
		// ReEnableParent() fires.  OnInitDialog disables the parent
		// when modeless-Create is called, and only the destructor
		// re-enables.  Hiding leaves the parent stuck disabled, so
		// the user can't click the main window after the op
		// completes.
		ProgressBridge::shutdown();
		s_bInOp = false;
		return;
	}

	// Suppress the dialog for ops that finish before the threshold.
	if ( elapsed < SHOW_AFTER_MS )
		return;

	EnsureCreated();
	if ( ! s_pDlg )
		return;

	CString sLabel;
	if ( pStatus && pStatus[0] )
		sLabel = pStatus;
	else
		sLabel = _T("Working...");

	if ( total > 0 )
	{
		CString sExtra;
		sExtra.Format( _T("  %d / %d"), current, total );
		sLabel += sExtra;
	}

	s_pDlg->SetStatus( sLabel );

	int pos = 0;
	if ( total > 0 )
		pos = (int)(( (long long)current * 1000 ) / total);
	s_pDlg->SetPos( pos );

	s_nLastTotal = total;
}

void ProgressBridge::shutdown()
{
	if ( s_pDlg )
	{
		// CProgressDlg's destructor calls DestroyWindow; deleting the C++
		// object owns both halves of the lifetime.  No PostNcDestroy
		// override → no `delete this`, so we delete here explicitly.
		delete s_pDlg;
		s_pDlg = 0;
	}
}

//---------------------------------------------------------------------------------------------------
// EOF
