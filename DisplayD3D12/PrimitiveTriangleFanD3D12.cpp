/*
	PrimitiveTriangleFanD3D12.cpp
	DX12 does NOT support triangle fans - convert to triangle lists.
	Fan v[0],v[1],...,v[N-1] -> triangles (v[0],v[i+1],v[i+2]) for i=0..N-3
	(c)2024 Palestar
*/

#include "DisplayD3D12/PrimitiveTriangleFanD3D12.h"
#include "DisplayD3D12/PrimitiveFactory.h"

//------------------------------------------------------------------------------------

static ComPtr<ID3D12Resource> CreateUploadBuffer( ID3D12Device * pDevice, const void * pData, UINT dataSize )
{
	if ( !pDevice || dataSize == 0 ) return nullptr;
	D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC rd = {}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rd.Width = dataSize; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
	rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ComPtr<ID3D12Resource> buf;
	if ( FAILED(pDevice->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buf))) ) return nullptr;
	if ( pData ) {
		void * p = nullptr; D3D12_RANGE rr = {0,0};
		buf->Map(0, &rr, &p); if (p) { memcpy(p, pData, dataSize); buf->Unmap(0, nullptr); }
	}
	return buf;
}

// Convert fan to triangle list (generic template)
template<typename V>
static void ConvertFanToList( const V * pFan, int fanCount, V * pOut )
{
	int triCount = fanCount - 2;
	for ( int i = 0; i < triCount; i++ )
	{
		pOut[i*3+0] = pFan[0];
		pOut[i*3+1] = pFan[i+1];
		pOut[i*3+2] = pFan[i+2];
	}
}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleFanD3D12 );

PrimitiveTriangleFanD3D12::PrimitiveTriangleFanD3D12() : m_TriangleCount(0) {}

bool PrimitiveTriangleFanD3D12::execute()
{
	if ( m_TriangleCount > 0 && m_VB )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = m_VB->GetGPUVirtualAddress();
		vbv.SizeInBytes = m_TriangleCount * 3 * sizeof(Vertex);
		vbv.StrideInBytes = sizeof(Vertex);
		pDevice->bindPSO( PSOKey::IL_VERTEX, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers(0, 1, &vbv);
		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmdList->DrawInstanced(m_TriangleCount * 3, 1, 0, 0);
		DisplayDevice::sm_nTrianglesRendered += m_TriangleCount;
	}
	return true;
}

void PrimitiveTriangleFanD3D12::clear() {}
void PrimitiveTriangleFanD3D12::release() {
	if ( m_VB && m_pDevice ) ((DisplayDeviceD3D12 *)m_pDevice)->deferReleaseResource( m_VB.Detach() );
	else m_VB.Reset();
	m_TriangleCount = 0;
}

bool PrimitiveTriangleFanD3D12::initialize( int vertexCount, const Vertex * pVerts )
{
	release();
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
	m_TriangleCount = vertexCount - 2;
	if ( m_TriangleCount <= 0 ) return false;

	int outVerts = m_TriangleCount * 3;
	Vertex * pConverted = new Vertex[outVerts];
	ConvertFanToList(pVerts, vertexCount, pConverted);
	m_VB = CreateUploadBuffer(pDevice->getDevice(), pConverted, outVerts * sizeof(Vertex));
	delete[] pConverted;
	return m_VB != nullptr;
}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleFanDD3D12 );

PrimitiveTriangleFanDD3D12::PrimitiveTriangleFanDD3D12() : m_pVertices(NULL), m_VertexCount(0) {}

bool PrimitiveTriangleFanDD3D12::execute()
{
	if ( m_VertexCount > 2 && m_pVertices )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		int triCount = m_VertexCount - 2;
		UINT dataSize = triCount * 3 * sizeof(Vertex);
		UploadRingBuffer::Allocation alloc = pDevice->allocateDynamic(dataSize, sizeof(Vertex));
		ConvertFanToList(m_pVertices, m_VertexCount, (Vertex *)alloc.cpuAddress);

		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = alloc.gpuAddress;
		vbv.SizeInBytes = dataSize;
		vbv.StrideInBytes = sizeof(Vertex);
		pDevice->bindPSO( PSOKey::IL_VERTEX, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers(0, 1, &vbv);
		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmdList->DrawInstanced(triCount * 3, 1, 0, 0);
		DisplayDevice::sm_nTrianglesRendered += triCount;
	}
	return true;
}

void PrimitiveTriangleFanDD3D12::clear() {}
void PrimitiveTriangleFanDD3D12::release() { delete[] m_pVertices; m_pVertices = NULL; m_VertexCount = 0; }

bool PrimitiveTriangleFanDD3D12::initialize( int vertexCount )
{
	release(); m_pVertices = new Vertex[vertexCount]; m_VertexCount = vertexCount; return true;
}

bool PrimitiveTriangleFanDD3D12::initialize( int vertexCount, const Vertex * pVerts )
{
	initialize(vertexCount); memcpy(m_pVertices, pVerts, vertexCount * sizeof(Vertex)); return true;
}

Vertex * PrimitiveTriangleFanDD3D12::lock() { return m_pVertices; }
void PrimitiveTriangleFanDD3D12::unlock() {}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleFanLD3D12 );

PrimitiveTriangleFanLD3D12::PrimitiveTriangleFanLD3D12() : m_TriangleCount(0) {}

bool PrimitiveTriangleFanLD3D12::execute()
{
	if ( m_TriangleCount > 0 && m_VB )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = m_VB->GetGPUVirtualAddress();
		vbv.SizeInBytes = m_TriangleCount * 3 * sizeof(VertexL);
		vbv.StrideInBytes = sizeof(VertexL);
		pDevice->bindPSO( PSOKey::IL_VERTEXL, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers(0, 1, &vbv);
		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmdList->DrawInstanced(m_TriangleCount * 3, 1, 0, 0);
		DisplayDevice::sm_nTrianglesRendered += m_TriangleCount;
	}
	return true;
}

void PrimitiveTriangleFanLD3D12::clear() {}
void PrimitiveTriangleFanLD3D12::release() {
	if ( m_VB && m_pDevice ) ((DisplayDeviceD3D12 *)m_pDevice)->deferReleaseResource( m_VB.Detach() );
	else m_VB.Reset();
	m_TriangleCount = 0;
}

bool PrimitiveTriangleFanLD3D12::initialize( int vertexCount, const VertexL * pVerts )
{
	release();
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
	m_TriangleCount = vertexCount - 2;
	if ( m_TriangleCount <= 0 ) return false;
	int outVerts = m_TriangleCount * 3;
	VertexL * pConverted = new VertexL[outVerts];
	ConvertFanToList(pVerts, vertexCount, pConverted);
	m_VB = CreateUploadBuffer(pDevice->getDevice(), pConverted, outVerts * sizeof(VertexL));
	delete[] pConverted;
	return m_VB != nullptr;
}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleFanDLD3D12 );

PrimitiveTriangleFanDLD3D12::PrimitiveTriangleFanDLD3D12() : m_pVertices(NULL), m_VertexCount(0) {}

bool PrimitiveTriangleFanDLD3D12::execute()
{
	if ( m_VertexCount > 2 && m_pVertices )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		int triCount = m_VertexCount - 2;
		UINT dataSize = triCount * 3 * sizeof(VertexL);
		UploadRingBuffer::Allocation alloc = pDevice->allocateDynamic(dataSize, sizeof(VertexL));
		ConvertFanToList(m_pVertices, m_VertexCount, (VertexL *)alloc.cpuAddress);
		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = alloc.gpuAddress; vbv.SizeInBytes = dataSize; vbv.StrideInBytes = sizeof(VertexL);
		pDevice->bindPSO( PSOKey::IL_VERTEXL, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers(0, 1, &vbv);
		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmdList->DrawInstanced(triCount * 3, 1, 0, 0);
		DisplayDevice::sm_nTrianglesRendered += triCount;
	}
	return true;
}

void PrimitiveTriangleFanDLD3D12::clear() {}
void PrimitiveTriangleFanDLD3D12::release() { delete[] m_pVertices; m_pVertices = NULL; m_VertexCount = 0; }
bool PrimitiveTriangleFanDLD3D12::initialize( int vertexCount ) { release(); m_pVertices = new VertexL[vertexCount]; m_VertexCount = vertexCount; return true; }
bool PrimitiveTriangleFanDLD3D12::initialize( int vertexCount, const VertexL * pVerts ) { initialize(vertexCount); memcpy(m_pVertices, pVerts, vertexCount * sizeof(VertexL)); return true; }
VertexL * PrimitiveTriangleFanDLD3D12::lock() { return m_pVertices; }
void PrimitiveTriangleFanDLD3D12::unlock() {}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleFanDTLD3D12 );

PrimitiveTriangleFanDTLD3D12::PrimitiveTriangleFanDTLD3D12() : m_pVertices(NULL), m_VertexCount(0) {}

bool PrimitiveTriangleFanDTLD3D12::execute()
{
	if ( m_VertexCount > 2 && m_pVertices )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		int triCount = m_VertexCount - 2;
		UINT dataSize = triCount * 3 * sizeof(VertexTL);
		UploadRingBuffer::Allocation alloc = pDevice->allocateDynamic(dataSize, sizeof(VertexTL));
		ConvertFanToList(m_pVertices, m_VertexCount, (VertexTL *)alloc.cpuAddress);
		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = alloc.gpuAddress; vbv.SizeInBytes = dataSize; vbv.StrideInBytes = sizeof(VertexTL);
		pDevice->bindPSO( PSOKey::IL_VERTEXTL, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers(0, 1, &vbv);
		pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCmdList->DrawInstanced(triCount * 3, 1, 0, 0);
		DisplayDevice::sm_nTrianglesRendered += triCount;
	}
	return true;
}

void PrimitiveTriangleFanDTLD3D12::clear() {}
void PrimitiveTriangleFanDTLD3D12::release() { delete[] m_pVertices; m_pVertices = NULL; m_VertexCount = 0; }
bool PrimitiveTriangleFanDTLD3D12::initialize( int vertexCount ) { release(); m_pVertices = new VertexTL[vertexCount]; m_VertexCount = vertexCount; return true; }
bool PrimitiveTriangleFanDTLD3D12::initialize( int vertexCount, const VertexTL * pVerts ) { initialize(vertexCount); memcpy(m_pVertices, pVerts, vertexCount * sizeof(VertexTL)); return true; }
VertexTL * PrimitiveTriangleFanDTLD3D12::lock() { return m_pVertices; }
void PrimitiveTriangleFanDTLD3D12::unlock() {}

//------------------------------------------------------------------------------------
// EOF
