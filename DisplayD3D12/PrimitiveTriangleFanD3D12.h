/*
	PrimitiveTriangleFanD3D12.h
	(c)2024 Palestar
	Note: DX12 doesn't support triangle fans, so we convert to triangle lists.
*/

#ifndef PRIMITIVE_TRIANGLE_FAN_D3D12_H
#define PRIMITIVE_TRIANGLE_FAN_D3D12_H

#include "Display/PrimitiveTriangleFan.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

//------------------------------------------------------------------------------------

class PrimitiveTriangleFanD3D12 : public PrimitiveTriangleFan
{
public:
	PrimitiveTriangleFanD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int vertexCount, const Vertex * pVerticies );

private:
	int							m_TriangleCount;
	ComPtr<ID3D12Resource>		m_VB;
};

//----------------------------------------------------------------------------

class PrimitiveTriangleFanDD3D12 : public PrimitiveTriangleFanD
{
public:
	PrimitiveTriangleFanDD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int vertexCount );
	bool			initialize( int vertexCount, const Vertex * pVerticies );
	Vertex *		lock();
	void			unlock();

private:
	Vertex *		m_pVertices;
	int				m_VertexCount;
};

//----------------------------------------------------------------------------

class PrimitiveTriangleFanLD3D12 : public PrimitiveTriangleFanL
{
public:
	PrimitiveTriangleFanLD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( int vertexCount, const VertexL * pVerticies );

private:
	int							m_TriangleCount;
	ComPtr<ID3D12Resource>		m_VB;
};

//----------------------------------------------------------------------------

class PrimitiveTriangleFanDLD3D12 : public PrimitiveTriangleFanDL
{
public:
	PrimitiveTriangleFanDLD3D12();

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

class PrimitiveTriangleFanDTLD3D12 : public PrimitiveTriangleFanDTL
{
public:
	PrimitiveTriangleFanDTLD3D12();

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
