/*
	PrimitiveLineListD3D12.h
	(c)2024 Palestar
*/

#ifndef PRIMITIVE_LINE_LIST_D3D12_H
#define PRIMITIVE_LINE_LIST_D3D12_H

#include "Display/PrimitiveLineList.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

//------------------------------------------------------------------------------------

class PrimitiveLineListD3D12 : public PrimitiveLineList
{
public:
	PrimitiveLineListD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int lineCount );
	bool			initialize( int lineCount, const Line * pLines );

	// DevicePrimitive / PrimitiveLineList interface
	Line *			lock();
	void			unlock();

private:
	int					m_LineCount;
	ComPtr<ID3D12Resource> 	m_VB;
};

//----------------------------------------------------------------------------

class PrimitiveLineListLD3D12 : public PrimitiveLineListL
{
public:
	PrimitiveLineListLD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int lineCount );
	bool			initialize( int lineCount, const LineL * pLines );

	LineL *			lock();
	void			unlock();

private:
	int					m_LineCount;
	ComPtr<ID3D12Resource> 	m_VB;
	LineL *				m_pLines; // CPU-side buffer for lock/unlock
};

//----------------------------------------------------------------------------

class PrimitiveLineListDTLD3D12 : public PrimitiveLineListDTL
{
public:
	PrimitiveLineListDTLD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int lineCount );
	bool			initialize( int lineCount, const LineTL * pLines );
	LineTL *		lock();
	void			unlock();

private:
	LineTL *		m_pLines;
	int				m_LineCount;
};

//----------------------------------------------------------------------------

#endif

//------------------------------------------------------------------------------------
// EOF
