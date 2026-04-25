/*
	PrimitiveLineStripD3D12.cpp
	(c)2024 Palestar
*/

#include "DisplayD3D12/PrimitiveLineStripD3D12.h"
#include "DisplayD3D12/PrimitiveFactory.h"

static ComPtr<ID3D12Resource> CreateUploadBuffer( ID3D12Device * pDev, const void * pData, UINT sz )
{
	if (!pDev || sz == 0) return nullptr;
	D3D12_HEAP_PROPERTIES hp = {}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC rd = {}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	rd.Width = sz; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
	rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ComPtr<ID3D12Resource> buf;
	if (FAILED(pDev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buf)))) return nullptr;
	if (pData) { void*p=nullptr; D3D12_RANGE rr={0,0}; buf->Map(0,&rr,&p); if(p){memcpy(p,pData,sz); buf->Unmap(0,nullptr);} }
	return buf;
}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveLineStripD3D12 );
PrimitiveLineStripD3D12::PrimitiveLineStripD3D12() : m_VertexCount(0) {}

bool PrimitiveLineStripD3D12::execute()
{
	if (m_VertexCount > 1 && m_VB) {
		DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
		ID3D12GraphicsCommandList * cl = pDev->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = m_VB->GetGPUVirtualAddress();
		vbv.SizeInBytes = m_VertexCount * sizeof(Vertex);
		vbv.StrideInBytes = sizeof(Vertex);
		pDev->bindPSO( PSOKey::IL_VERTEX, PSOKey::TOPO_LINE );
		cl->IASetVertexBuffers(0,1,&vbv);
		cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);
		cl->DrawInstanced(m_VertexCount,1,0,0);
		DisplayDevice::sm_nLinesRendered += m_VertexCount - 1;
	}
	return true;
}

void PrimitiveLineStripD3D12::clear() {}
void PrimitiveLineStripD3D12::release() {
	if ( m_VB && m_pDevice ) ((DisplayDeviceD3D12 *)m_pDevice)->deferReleaseResource( m_VB.Detach() );
	else m_VB.Reset();
	m_VertexCount = 0;
}

bool PrimitiveLineStripD3D12::initialize(int vertexCount, const Vertex * pVerts)
{
	release();
	DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
	m_VertexCount = vertexCount;
	m_VB = CreateUploadBuffer(pDev->getDevice(), pVerts, vertexCount * sizeof(Vertex));
	return m_VB != nullptr;
}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveLineStripLD3D12 );
PrimitiveLineStripLD3D12::PrimitiveLineStripLD3D12() : m_VertexCount(0) {}

bool PrimitiveLineStripLD3D12::execute()
{
	if (m_VertexCount > 1 && m_VB) {
		DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
		ID3D12GraphicsCommandList * cl = pDev->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = m_VB->GetGPUVirtualAddress();
		vbv.SizeInBytes = m_VertexCount * sizeof(VertexL);
		vbv.StrideInBytes = sizeof(VertexL);
		pDev->bindPSO( PSOKey::IL_VERTEXL, PSOKey::TOPO_LINE );
		cl->IASetVertexBuffers(0,1,&vbv);
		cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);
		cl->DrawInstanced(m_VertexCount,1,0,0);
		DisplayDevice::sm_nLinesRendered += m_VertexCount - 1;
	}
	return true;
}

void PrimitiveLineStripLD3D12::clear() {}
void PrimitiveLineStripLD3D12::release() {
	if ( m_VB && m_pDevice ) ((DisplayDeviceD3D12 *)m_pDevice)->deferReleaseResource( m_VB.Detach() );
	else m_VB.Reset();
	m_VertexCount = 0;
}

bool PrimitiveLineStripLD3D12::initialize(int vertexCount, const VertexL * pVerts)
{
	release();
	DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
	m_VertexCount = vertexCount;
	m_VB = CreateUploadBuffer(pDev->getDevice(), pVerts, vertexCount * sizeof(VertexL));
	return m_VB != nullptr;
}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveLineStripDLD3D12 );
PrimitiveLineStripDLD3D12::PrimitiveLineStripDLD3D12() : m_pVertices(NULL), m_VertexCount(0) {}

bool PrimitiveLineStripDLD3D12::execute()
{
	if (m_VertexCount > 1 && m_pVertices) {
		DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
		UINT dataSize = m_VertexCount * sizeof(VertexL);
		UploadRingBuffer::Allocation alloc = pDev->allocateDynamic(dataSize, sizeof(VertexL));
		memcpy(alloc.cpuAddress, m_pVertices, dataSize);
		ID3D12GraphicsCommandList * cl = pDev->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = alloc.gpuAddress; vbv.SizeInBytes = dataSize; vbv.StrideInBytes = sizeof(VertexL);
		pDev->bindPSO( PSOKey::IL_VERTEXL, PSOKey::TOPO_LINE );
		cl->IASetVertexBuffers(0,1,&vbv);
		cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);
		cl->DrawInstanced(m_VertexCount,1,0,0);
		DisplayDevice::sm_nLinesRendered += m_VertexCount - 1;
	}
	return true;
}

void PrimitiveLineStripDLD3D12::clear() {}
void PrimitiveLineStripDLD3D12::release() { delete[] m_pVertices; m_pVertices = NULL; m_VertexCount = 0; }
bool PrimitiveLineStripDLD3D12::initialize(int cnt) { release(); m_pVertices = new VertexL[cnt]; m_VertexCount = cnt; return true; }
bool PrimitiveLineStripDLD3D12::initialize(int cnt, const VertexL * p) { initialize(cnt); memcpy(m_pVertices,p,cnt*sizeof(VertexL)); return true; }
VertexL * PrimitiveLineStripDLD3D12::lock() { return m_pVertices; }
void PrimitiveLineStripDLD3D12::unlock() {}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveLineStripTLD3D12 );
PrimitiveLineStripTLD3D12::PrimitiveLineStripTLD3D12() : m_VertexCount(0) {}

bool PrimitiveLineStripTLD3D12::execute()
{
	if (m_VertexCount > 1 && m_VB) {
		DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
		ID3D12GraphicsCommandList * cl = pDev->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = m_VB->GetGPUVirtualAddress();
		vbv.SizeInBytes = m_VertexCount * sizeof(VertexTL);
		vbv.StrideInBytes = sizeof(VertexTL);
		pDev->bindPSO( PSOKey::IL_VERTEXTL, PSOKey::TOPO_LINE );
		cl->IASetVertexBuffers(0,1,&vbv);
		cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);
		cl->DrawInstanced(m_VertexCount,1,0,0);
		DisplayDevice::sm_nLinesRendered += m_VertexCount - 1;
	}
	return true;
}

void PrimitiveLineStripTLD3D12::clear() {}
void PrimitiveLineStripTLD3D12::release() {
	if ( m_VB && m_pDevice ) ((DisplayDeviceD3D12 *)m_pDevice)->deferReleaseResource( m_VB.Detach() );
	else m_VB.Reset();
	m_VertexCount = 0;
}

bool PrimitiveLineStripTLD3D12::initialize(int vertexCount, const VertexTL * pVerts)
{
	release();
	DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
	m_VertexCount = vertexCount;
	m_VB = CreateUploadBuffer(pDev->getDevice(), pVerts, vertexCount * sizeof(VertexTL));
	return m_VB != nullptr;
}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveLineStripDTLD3D12 );
PrimitiveLineStripDTLD3D12::PrimitiveLineStripDTLD3D12() : m_pVertices(NULL), m_VertexCount(0) {}

bool PrimitiveLineStripDTLD3D12::execute()
{
	if (m_VertexCount > 1 && m_pVertices) {
		DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
		UINT dataSize = m_VertexCount * sizeof(VertexTL);
		UploadRingBuffer::Allocation alloc = pDev->allocateDynamic(dataSize, sizeof(VertexTL));
		memcpy(alloc.cpuAddress, m_pVertices, dataSize);
		ID3D12GraphicsCommandList * cl = pDev->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = alloc.gpuAddress; vbv.SizeInBytes = dataSize; vbv.StrideInBytes = sizeof(VertexTL);
		pDev->bindPSO( PSOKey::IL_VERTEXTL, PSOKey::TOPO_LINE );
		cl->IASetVertexBuffers(0,1,&vbv);
		cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);
		cl->DrawInstanced(m_VertexCount,1,0,0);
		DisplayDevice::sm_nLinesRendered += m_VertexCount - 1;
	}
	return true;
}

void PrimitiveLineStripDTLD3D12::clear() {}
void PrimitiveLineStripDTLD3D12::release() { delete[] m_pVertices; m_pVertices = NULL; m_VertexCount = 0; }
bool PrimitiveLineStripDTLD3D12::initialize(int cnt) { release(); m_pVertices = new VertexTL[cnt]; m_VertexCount = cnt; return true; }
bool PrimitiveLineStripDTLD3D12::initialize(int cnt, const VertexTL * p) { initialize(cnt); memcpy(m_pVertices,p,cnt*sizeof(VertexTL)); return true; }
VertexTL * PrimitiveLineStripDTLD3D12::lock() { return m_pVertices; }
void PrimitiveLineStripDTLD3D12::unlock() {}

//------------------------------------------------------------------------------------
// EOF
