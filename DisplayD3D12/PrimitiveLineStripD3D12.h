/*
	PrimitiveLineStripD3D12.h
	(c)2024 Palestar
*/

#ifndef PRIMITIVE_LINE_STRIP_D3D12_H
#define PRIMITIVE_LINE_STRIP_D3D12_H

#include "Display/PrimitiveLineStrip.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

//------------------------------------------------------------------------------------

class PrimitiveLineStripD3D12 : public PrimitiveLineStrip
{
public:
	PrimitiveLineStripD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int vertexCount, const Vertex * pVerticies );

private:
	int							m_VertexCount;
	ComPtr<ID3D12Resource>		m_VB;
};

//----------------------------------------------------------------------------

class PrimitiveLineStripLD3D12 : public PrimitiveLineStripL
{
public:
	PrimitiveLineStripLD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int vertexCount, const VertexL * pVerticies );

private:
	int							m_VertexCount;
	ComPtr<ID3D12Resource>		m_VB;
};

//----------------------------------------------------------------------------

class PrimitiveLineStripDLD3D12 : public PrimitiveLineStripDL
{
public:
	PrimitiveLineStripDLD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int vertexCount );
	bool			initialize( int vertexCount, const VertexL * pVerticies );
	VertexL *		lock();
	void			unlock();

private:
	VertexL *		m_pVertices;
	int				m_VertexCount;
};

//----------------------------------------------------------------------------

class PrimitiveLineStripTLD3D12 : public PrimitiveLineStripTL
{
public:
	PrimitiveLineStripTLD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int vertexCount, const VertexTL * pVerticies );

private:
	int							m_VertexCount;
	ComPtr<ID3D12Resource>		m_VB;
};

//----------------------------------------------------------------------------

class PrimitiveLineStripDTLD3D12 : public PrimitiveLineStripDTL
{
public:
	PrimitiveLineStripDTLD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int vertexCount );
	bool			initialize( int vertexCount, const VertexTL * pVerticies );
	VertexTL *		lock();
	void			unlock();

private:
	VertexTL *		m_pVertices;
	int				m_VertexCount;
};

//----------------------------------------------------------------------------

#endif

//------------------------------------------------------------------------------------
// EOF
