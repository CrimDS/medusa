// ChoosePorts.cpp : implementation file
//

#include "stdafx.h"
#include "ResourcerDoc.h"
#include "Resource.h"
#include "ChoosePorts.h"

#include "Standard/Library.h"

#include <set>

/////////////////////////////////////////////////////////////////////////////
// CChoosePorts dialog


CChoosePorts::CChoosePorts(CWnd* pParent /*=NULL*/)
	: CDialog(CChoosePorts::IDD, pParent)
{
	//{{AFX_DATA_INIT(CChoosePorts)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}


void CChoosePorts::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CChoosePorts)
	DDX_Control(pDX, IDC_PORT_LIST, m_PortList);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CChoosePorts, CDialog)
	//{{AFX_MSG_MAP(CChoosePorts)
	ON_BN_CLICKED(IDC_ADD, OnAdd)
	ON_BN_CLICKED(IDC_REMOVE, OnRemove)
	ON_BN_CLICKED(IDC_BUTTON1, OnRemoveAll)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CChoosePorts message handlers

BOOL CChoosePorts::OnInitDialog() 
{
	CDialog::OnInitDialog();
	
	for(int i=0;i<Port::s_PortLibPaths.size();i++)
		m_PortList.InsertItem( 0, Port::s_PortLibPaths[i] );
	
	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CChoosePorts::OnOK()
{
	// Snapshot the current loaded paths so we can detect newly-added
	// entries and LoadLibrary them on the spot.  Previously this only
	// updated the in-memory list; DLLs got loaded at startup from the
	// registry, and the registry only got written at clean shutdown
	// (Port::unloadPortLibs).  Result: add a DLL → click OK → expect
	// it to take effect → it doesn't, and if the app then crashes
	// before clean exit, the path is lost too.
	std::set< CString > sOld;
	for ( int i = 0; i < Port::s_PortLibPaths.size(); ++i )
		sOld.insert( Port::s_PortLibPaths[i] );

	// Replace the in-memory list with whatever the user typed/picked.
	Port::s_PortLibPaths.release();
	for ( int i = 0; i < m_PortList.GetItemCount(); ++i )
		Port::s_PortLibPaths.push( m_PortList.GetItemText( i, 0 ) );

	// Load any path that wasn't already loaded.  We don't unload removed
	// entries — DLL unload would leave dangling factory pointers and
	// half-destructed widget instances in any document already open.
	// Removed entries simply won't be reloaded next start.
	for ( int i = 0; i < Port::s_PortLibPaths.size(); ++i )
	{
		const CString & sPath = Port::s_PortLibPaths[i];
		if ( sOld.find( sPath ) != sOld.end() )
			continue;	// already loaded

		Library * pLib = new Library();
		if ( ! pLib->load( sPath ) )
		{
			MessageBox( _T("Failed to load library!"), sPath, MB_OK | MB_ICONWARNING );
			delete pLib;
			continue;
		}
		Port::s_PortLibs.push( pLib );
	}

	// Persist immediately so a later abnormal exit doesn't lose the
	// settings.  Mirror the format that Port::loadPortLibs reads.
	if ( CWinApp * pApp = AfxGetApp() )
	{
		const TCHAR * SECTION = TEXT("Settings");
		pApp->WriteProfileInt( SECTION, TEXT("LibPathCount"), Port::s_PortLibPaths.size() );
		CString keyString;
		for ( int i = 0; i < Port::s_PortLibPaths.size(); ++i )
		{
			keyString.Format( TEXT("LibPath%d"), i );
			pApp->WriteProfileString( SECTION, keyString, Port::s_PortLibPaths[i] );
		}
	}

	CDialog::OnOK();
}

void CChoosePorts::OnAdd() 
{
#ifndef _DEBUG
    CFileDialog dialog(TRUE, NULL, NULL, 
		OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT, 
		_T("Ports|*Port.dll|Libraries|*.dll||"));
#else
    CFileDialog dialog(TRUE, NULL, NULL, 
		OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT, 
		_T("Ports|*PortD.dll|Libraries|*.dll||"));
#endif

	TCHAR fileBuffer[ 16 * 1024 + 1];
	fileBuffer[0] = 0;

	dialog.m_ofn.lpstrFile = fileBuffer;
	dialog.m_ofn.nMaxFile = 16 * 1024;

	if (dialog.DoModal() == IDOK)
	{
		POSITION pos = dialog.GetStartPosition();
		while( pos != NULL )
		{
			CString portFile = dialog.GetNextPathName( pos );
			m_PortList.InsertItem( 0, portFile );
		}
    }
}

void CChoosePorts::OnRemove() 
{
	int removeItem = m_PortList.GetNextItem(-1, LVNI_SELECTED );
	m_PortList.DeleteItem( removeItem );
}

void CChoosePorts::OnRemoveAll() 
{
	m_PortList.DeleteAllItems();
}
