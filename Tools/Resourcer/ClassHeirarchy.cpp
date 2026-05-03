// ClassHeirarchy.cpp : implementation file
//

#include "stdafx.h"
#include "Resourcer.h"
#include "ClassHeirarchy.h"

#include "Factory/Factory.h"
#include "Factory/Widget.h"



/////////////////////////////////////////////////////////////////////////////
// CClassHeirarchy dialog


CClassHeirarchy::CClassHeirarchy(CWnd* pParent /*=NULL*/)
	: CDialog(CClassHeirarchy::IDD, pParent), m_ppOwnerSlot( NULL )
{
	//{{AFX_DATA_INIT(CClassHeirarchy)
		// NOTE: the ClassWizard will add member initialization here
	//}}AFX_DATA_INIT
}


void CClassHeirarchy::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CClassHeirarchy)
	DDX_Control(pDX, IDC_TREE1, m_Tree);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CClassHeirarchy, CDialog)
	//{{AFX_MSG_MAP(CClassHeirarchy)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CClassHeirarchy message handlers

BOOL CClassHeirarchy::OnInitDialog() 
{
	CDialog::OnInitDialog();
	
	Factory *pFactory = Factory::findFactory( CLASS_KEY(Widget) );
	HTREEITEM root = m_Tree.InsertItem( CharString(pFactory->className()) + " : (" + pFactory->classKey().string() + ")" );

	buildTree( root, CLASS_KEY(Widget) );
			
	return TRUE;  // return TRUE unless you set the focus to a control
	              // EXCEPTION: OCX Property Pages should return FALSE
}

void CClassHeirarchy::buildTree( HTREEITEM parent, const ClassKey &parentKey )
{
	for(int i=0;i<Factory::typeCount( parentKey );i++)
	{
		ClassKey key = Factory::type( parentKey, i );
		Factory *pFactory = Factory::findFactory( key );

		HTREEITEM item = m_Tree.InsertItem( CharString(pFactory->className()) + " : (" + pFactory->classKey().string() + ")", parent );

		buildTree( item, key );
	}
}

//---------------------------------------------------------------------------
// Modeless lifetime: dialog is heap-allocated by CMainFrame and deletes
// itself once Windows finishes destroying its HWND.  OnOK / OnCancel
// (default Enter / Esc handlers) need to call DestroyWindow rather than
// EndDialog because we never DoModal'd; PostNcDestroy then cleans up the
// C++ object.
//---------------------------------------------------------------------------

void CClassHeirarchy::OnOK()
{
	DestroyWindow();
}

void CClassHeirarchy::OnCancel()
{
	DestroyWindow();
}

void CClassHeirarchy::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	// Clear the owning frame's back-pointer first so a subsequent menu
	// click creates a fresh dialog instead of touching freed memory.
	if ( m_ppOwnerSlot != NULL )
		*m_ppOwnerSlot = NULL;
	delete this;
}

