// Resourcer.cpp : Defines the class behaviors for the application.
//

#include "stdafx.h"
#include "Resourcer.h"
#include "MainFrm.h"
#include "Resource.h"

#include "Debug/Error.h"
#include "Standard/Progress.h"
#include "Standard/Registry.h"
#include "Standard/Process.h"
#include "Tools/ResourcerDoc/ResourcerDoc.h"
#include "Tools/ResourcerDoc/Port.h"
#include "Tools/ResourcerDoc/ProgressDlg.h"
#include ".\resourcer.h"

#pragma warning(disable:4800)	// D:\Projects\Hydra\Tools\Resourcer\Resourcer.cpp(96) : warning C4800: 'unsigned int' : forcing value to bool 'true' or 'false' (performance warning)

/////////////////////////////////////////////////////////////////////////////
// CResourcerApp

BEGIN_MESSAGE_MAP(CResourcerApp, CWinApp)
	//{{AFX_MSG_MAP(CResourcerApp)
	ON_COMMAND(ID_APP_ABOUT, OnAppAbout)
	//}}AFX_MSG_MAP
	// Standard file based document commands
	ON_COMMAND(ID_FILE_NEW, CWinApp::OnFileNew)
	ON_COMMAND(ID_FILE_OPEN, CWinApp::OnFileOpen)
	// Standard print setup command
	ON_COMMAND(ID_FILE_PRINT_SETUP, CWinApp::OnFilePrintSetup)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CResourcerApp construction

CResourcerApp::CResourcerApp()
{
	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CResourcerApp object

CResourcerApp theApp;

/////////////////////////////////////////////////////////////////////////////
// CResourcerApp initialization

const char *CResourcerAppSection = "CResourcerApp";

BOOL CResourcerApp::InitInstance()
{
#ifdef _DEBUG
	// this stops the massive memory dumps on program exit..
	_CrtSetReportMode( _CRT_WARN, 0 );
#endif

	SetProcessErrorMode( EM_DIALOG );

#ifndef _DEBUG
	char exe[ MAX_PATH ];
	GetModuleFileName( GetModuleHandle(NULL), exe, MAX_PATH );
	char path[ MAX_PATH ];
	strcpy( path, exe );

	// remove the EXE from the path
	char * pLastSlash = strrchr( path, '\\' );
	if ( pLastSlash == NULL )
	{
		MessageBox( NULL, "Error: Failed to parse the local path!", path, MB_OK );
		return FALSE;
	}
	*pLastSlash = 0;

	// set the current directory to be the same as this exe
	SetCurrentDirectory( path );
#endif

	new FileReactor( "Resourcer.log" );
#if defined(_DEBUG)
	new DebugReactor();
#endif

	AfxEnableControlContainer();

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	//  of your final executable, you should remove from the following
	//  the specific initialization routines you do not need.

	// Change the registry key under which our settings are stored.
	// TODO: You should modify this string to be something appropriate
	// such as the name of your company or organization.
#ifdef _DEBUG
	// Use a separate registry section in Debug so the LibPath0..N port-DLL
	// list (which holds *D.dll paths in Debug, *.dll paths in Release) does
	// not stomp the other config's list, and Debug doesn't accidentally try
	// to load Release-built DLLs (causing both Medusa.dll and MedusaD.dll
	// to coexist in-process with split factory singletons).
	SetRegistryKey(_T("Palestar (Debug)"));
#else
	SetRegistryKey(_T("Palestar"));
#endif

	LoadStdProfileSettings(8);  // Load standard INI file options (including MRU)

	// Wire the global Progress hook so long-running operations (BC7
	// encode, image import, etc.) drive the modeless CProgressDlg
	// instead of leaving the UI looking frozen.
	Progress::setReporter( &ProgressBridge::report );

	// Force-load DisplayD3D12 at startup so its codecs (including
	// ImageCodecBC7D3D12) are registered with the Factory before the
	// user does anything that needs them.  Without this, BC7 encode
	// fails with "Failed to set pixel format" if you open an ImagePort
	// before opening a 3D scene (the 3D scene path is what otherwise
	// drags DisplayD3D12.dll in via the render context).  DisplayD3D9
	// loads earlier as a transitive dependency of Render3D, hence DXT
	// codecs already work without this.
#ifdef _DEBUG
	::LoadLibrary( _T("DisplayD3D12D.dll") );
#else
	::LoadLibrary( _T("DisplayD3D12.dll") );
#endif

	// add the document templates to this application
	CResourcerDoc::addDocTemplate();
	Port::addDocTemplate();

	// load the DLL's
	Port::loadPortLibs();

	// create main MDI Frame window
	CMainFrame* pMainFrame = new CMainFrame;
	if (!pMainFrame->LoadFrame(IDR_MAINFRAME))
		return FALSE;
	m_pMainWnd = pMainFrame;

	// Enable drag/drop open
	m_pMainWnd->DragAcceptFiles();

	// Enable DDE Execute open
	EnableShellOpen();
	RegisterShellFileTypes(TRUE);

	// Parse command line for standard shell commands, DDE, file open
	CCommandLineInfo cmdInfo;
	ParseCommandLine(cmdInfo);

	// Dispatch commands specified on the command line
	if (cmdInfo.m_nShellCommand != CCommandLineInfo::FileNew)
		if (!ProcessShellCommand(cmdInfo))
			return FALSE;

	// The main window has been initialized, so show and update it.
	//pMainFrame->ShowWindow(m_nCmdShow);
	pMainFrame->ShowWindow(SW_SHOWMAXIMIZED);
	pMainFrame->UpdateWindow();


	return TRUE;
}

int CResourcerApp::ExitInstance() 
{
	// release all cached assets
	Broker::flushCache();
	// release the reflection system
	Type::release();
	TypeCopy::release();
	// unload the ports
	Port::unloadPortLibs();

	return CWinApp::ExitInstance();
}

int CResourcerApp::Run()
{
#ifndef _DEBUG
	try {
#endif
		return CWinApp::Run();
#ifndef _DEBUG
	}
	catch( ExceptionBase e )
	{
		ProcessError( e.message(), e.file(), e.line() );
	}
#endif
	return 0;
}

//-------------------------------------------------------------------------------
// CAboutDlg dialog used for App About

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAboutDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	//{{AFX_MSG(CAboutDlg)
		// No message handlers
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
		// No message handlers
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

// App command to run the dialog
void CResourcerApp::OnAppAbout()
{
	CAboutDlg aboutDlg;
	aboutDlg.DoModal();
}

//-------------------------------------------------------------------------------
// EOF
