/*
	PrimitiveTriangleListD3D12.h
	(c)2024 Palestar
*/

#ifndef PRIMITIVE_TRIANGLE_LIST_D3D12_H
#define PRIMITIVE_TRIANGLE_LIST_D3D12_H

#include "Display/PrimitiveTriangleList.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

//------------------------------------------------------------------------------------

class PrimitiveTriangleListD3D12 : public PrimitiveTriangleList
{
public:
	PrimitiveTriangleListD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int triangleCount, const Triangle * pTriangles );

private:
	int							m_VBSize;
	ComPtr<ID3D12Resource>		m_VB;
};

//----------------------------------------------------------------------------

class PrimitiveTriangleListDD3D12 : public PrimitiveTriangleListD
{
public:
	PrimitiveTriangleListDD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int triangleCount );
	bool			initialize( int triangleCount, const Triangle * pTriangles );
	Triangle *		lock();
	void			unlock();

private:
	Triangle *		m_pTriangles;
	int				m_TriangleCount;
};

//----------------------------------------------------------------------------

class PrimitiveTriangleListID3D12 : public PrimitiveTriangleListI
{
public:
	PrimitiveTriangleListID3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int triangleCount, const Triangle * pTriangles );
	bool			initialize( int triangleCount, const word * pTriangles,
								int vertexCount, const Vertex * pVerticies );
	bool			initialize( int triangleCount, const word * pTriangles,
								PrimitiveTriangleListI * pVerts );

	// Data
	int							m_Triangles;
	int							m_Verts;
	ComPtr<ID3D12Resource>		m_VB;
	ComPtr<ID3D12Resource>		m_IB;
};

//------------------------------------------------------------------------------------

class PrimitiveTriangleListLD3D12 : public PrimitiveTriangleListL
{
public:
	PrimitiveTriangleListLD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int triangleCount, const TriangleL * pTriangles );

private:
	int							m_VBSize;
	ComPtr<ID3D12Resource>		m_VB;
};

//----------------------------------------------------------------------------

class PrimitiveTriangleListDLD3D12 : public PrimitiveTriangleListDL
{
public:
	PrimitiveTriangleListDLD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int triangleCount );
	bool			initialize( int triangleCount, const TriangleL * pTriangles );
	TriangleL *		lock();
	void			unlock();

private:
	TriangleL *		m_pTriangles;
	int				m_TriangleCount;
};

//----------------------------------------------------------------------------

class PrimitiveTriangleListTLD3D12 : public PrimitiveTriangleListTL
{
public:
	PrimitiveTriangleListTLD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int triangleCount, const TriangleTL * pTriangles );

private:
	int							m_VBSize;
	ComPtr<ID3D12Resource>		m_VB;
};

//----------------------------------------------------------------------------

class PrimitiveTriangleListDTLD3D12 : public PrimitiveTriangleListDTL
{
public:
	PrimitiveTriangleListDTLD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int triangleCount );
	bool			initialize( int triangleCount, const TriangleTL * pTriangles );
	TriangleTL *	lock();
	void			unlock();

private:
	TriangleTL *	m_pTriangles;
	int				m_TriangleCount;
};

//----------------------------------------------------------------------------

#endif

//------------------------------------------------------------------------------------
// EOF
