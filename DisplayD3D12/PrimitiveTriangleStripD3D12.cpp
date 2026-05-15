/*
	PrimitiveTriangleStripD3D12.cpp
	(c)2024 Palestar
*/

#include "DisplayD3D12/PrimitiveTriangleStripD3D12.h"
#include "DisplayD3D12/PrimitiveFactory.h"

//------------------------------------------------------------------------------------
// CreateUploadBuffer helper now lives in D3D12Helpers.h
//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleStripD3D12 );

PrimitiveTriangleStripD3D12::PrimitiveTriangleStripD3D12() : m_TriangleCount(0) {}

bool PrimitiveTriangleStripD3D12::execute()
{
	if ( m_TriangleCount > 0 && m_VB )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();
		int vertCount = m_TriangleCount + 2;
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = m_VB->GetGPUVirtualAddress();
		vbv.SizeInBytes = vertCount * sizeof(Vertex);
		vbv.StrideInBytes = sizeof(Vertex);
		pDevice->bindPSO( PSOKey::IL_VERTEX, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers(0, 1, &vbv);
		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		pCmdList->DrawInstanced(vertCount, 1, 0, 0);
		DisplayDevice::sm_nTrianglesRendered += m_TriangleCount;
	}
	return true;
}

void PrimitiveTriangleStripD3D12::clear() {}
void PrimitiveTriangleStripD3D12::release() {
	if ( m_VB ) DisplayDeviceD3D12::safeDeferReleaseResource( m_pDevice, m_VB.Detach() );
	m_TriangleCount = 0;
}

bool PrimitiveTriangleStripD3D12::initialize( int vertexCount, const Vertex * pVerts )
{
	release();
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
	m_TriangleCount = vertexCount - 2;
	if ( m_TriangleCount <= 0 ) return false;
	m_VB = CreateUploadBuffer( pDevice->getDevice(), pVerts, vertexCount * sizeof(Vertex) );
	return m_VB != nullptr;
}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleStripLD3D12 );

PrimitiveTriangleStripLD3D12::PrimitiveTriangleStripLD3D12() : m_TriangleCount(0) {}

bool PrimitiveTriangleStripLD3D12::execute()
{
	if ( m_TriangleCount > 0 && m_VB )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();
		int vertCount = m_TriangleCount + 2;
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = m_VB->GetGPUVirtualAddress();
		vbv.SizeInBytes = vertCount * sizeof(VertexL);
		vbv.StrideInBytes = sizeof(VertexL);
		pDevice->bindPSO( PSOKey::IL_VERTEXL, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers(0, 1, &vbv);
		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		pCmdList->DrawInstanced(vertCount, 1, 0, 0);
		DisplayDevice::sm_nTrianglesRendered += m_TriangleCount;
	}
	return true;
}

void PrimitiveTriangleStripLD3D12::clear() {}
void PrimitiveTriangleStripLD3D12::release() {
	if ( m_VB ) DisplayDeviceD3D12::safeDeferReleaseResource( m_pDevice, m_VB.Detach() );
	m_TriangleCount = 0;
}

bool PrimitiveTriangleStripLD3D12::initialize( int vertexCount, const VertexL * pVerts )
{
	release();
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
	m_TriangleCount = vertexCount - 2;
	if ( m_TriangleCount <= 0 ) return false;
	m_VB = CreateUploadBuffer( pDevice->getDevice(), pVerts, vertexCount * sizeof(VertexL) );
	return m_VB != nullptr;
}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleStripDLD3D12 );

PrimitiveTriangleStripDLD3D12::PrimitiveTriangleStripDLD3D12() : m_pVertices(NULL), m_VertexCount(0) {}

bool PrimitiveTriangleStripDLD3D12::execute()
{
	if ( m_VertexCount > 2 && m_pVertices )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		UINT dataSize = m_VertexCount * sizeof(VertexL);
		UploadRingBuffer::Allocation alloc = pDevice->allocateDynamic(dataSize, sizeof(VertexL));
		memcpy(alloc.cpuAddress, m_pVertices, dataSize);
		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = alloc.gpuAddress;
		vbv.SizeInBytes = dataSize;
		vbv.StrideInBytes = sizeof(VertexL);
		pDevice->bindPSO( PSOKey::IL_VERTEXL, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers(0, 1, &vbv);
		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		pCmdList->DrawInstanced(m_VertexCount, 1, 0, 0);
		DisplayDevice::sm_nTrianglesRendered += m_VertexCount - 2;
	}
	return true;
}

void PrimitiveTriangleStripDLD3D12::clear() {}
void PrimitiveTriangleStripDLD3D12::release() { delete[] m_pVertices; m_pVertices = NULL; m_VertexCount = 0; }

bool PrimitiveTriangleStripDLD3D12::initialize( int vertexCount )
{
	release(); m_pVertices = new VertexL[vertexCount]; m_VertexCount = vertexCount; return true;
}

bool PrimitiveTriangleStripDLD3D12::initialize( int vertexCount, const VertexL * pVerts )
{
	initialize(vertexCount); memcpy(m_pVertices, pVerts, vertexCount * sizeof(VertexL)); return true;
}

VertexL * PrimitiveTriangleStripDLD3D12::lock() { return m_pVertices; }
void PrimitiveTriangleStripDLD3D12::unlock() {}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleStripDTLD3D12 );

PrimitiveTriangleStripDTLD3D12::PrimitiveTriangleStripDTLD3D12() : m_pVertices(NULL), m_VertexCount(0) {}

bool PrimitiveTriangleStripDTLD3D12::execute()
{
	if ( m_VertexCount > 2 && m_pVertices )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		UINT dataSize = m_VertexCount * sizeof(VertexTL);
		UploadRingBuffer::Allocation alloc = pDevice->allocateDynamic(dataSize, sizeof(VertexTL));
		memcpy(alloc.cpuAddress, m_pVertices, dataSize);
		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = alloc.gpuAddress;
		vbv.SizeInBytes = dataSize;
		vbv.StrideInBytes = sizeof(VertexTL);
		pDevice->bindPSO( PSOKey::IL_VERTEXTL, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers(0, 1, &vbv);
		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		pCmdList->DrawInstanced(m_VertexCount, 1, 0, 0);
		DisplayDevice::sm_nTrianglesRendered += m_VertexCount - 2;
	}
	return true;
}

void PrimitiveTriangleStripDTLD3D12::clear() {}
void PrimitiveTriangleStripDTLD3D12::release() { delete[] m_pVertices; m_pVertices = NULL; m_VertexCount = 0; }

bool PrimitiveTriangleStripDTLD3D12::initialize( int vertexCount )
{
	release(); m_pVertices = new VertexTL[vertexCount]; m_VertexCount = vertexCount; return true;
}

bool PrimitiveTriangleStripDTLD3D12::initialize( int vertexCount, const VertexTL * pVerts )
{
	initialize(vertexCount); memcpy(m_pVertices, pVerts, vertexCount * sizeof(VertexTL)); return true;
}

VertexTL * PrimitiveTriangleStripDTLD3D12::lock() { return m_pVertices; }
void PrimitiveTriangleStripDTLD3D12::unlock() {}

//------------------------------------------------------------------------------------
// EOF
