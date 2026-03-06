/*
	PrimitiveTriangleStripD3D12.h
	(c)2024 Palestar
	Note: DX12 doesn't support triangle strips natively in the same way,
	so we convert strips to triangle lists during initialize().
*/

#ifndef PRIMITIVE_TRIANGLE_STRIP_D3D12_H
#define PRIMITIVE_TRIANGLE_STRIP_D3D12_H

#include "Display/PrimitiveTriangleStrip.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

//------------------------------------------------------------------------------------

class PrimitiveTriangleStripD3D12 : public PrimitiveTriangleStrip
{
public:
	PrimitiveTriangleStripD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int vertexCount, const Vertex * pVerticies );

private:
	int							m_TriangleCount;
	ComPtr<ID3D12Resource>		m_VB;
};

//----------------------------------------------------------------------------

class PrimitiveTriangleStripLD3D12 : public PrimitiveTriangleStripL
{
public:
	PrimitiveTriangleStripLD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int vertexCount, const VertexL * pVerticies );

private:
	int							m_TriangleCount;
	ComPtr<ID3D12Resource>		m_VB;
};

//----------------------------------------------------------------------------

class PrimitiveTriangleStripDLD3D12 : public PrimitiveTriangleStripDL
{
public:
	PrimitiveTriangleStripDLD3D12();

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

class PrimitiveTriangleStripDTLD3D12 : public PrimitiveTriangleStripDTL
{
public:
	PrimitiveTriangleStripDTLD3D12();

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
