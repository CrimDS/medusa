/*
	PrimitiveLineListD3D12.cpp
	(c)2024 Palestar
*/

#include "DisplayD3D12/PrimitiveLineListD3D12.h"
#include "DisplayD3D12/PrimitiveFactory.h"

// CreateUploadBuffer helper now lives in D3D12Helpers.h

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveLineListD3D12 );
PrimitiveLineListD3D12::PrimitiveLineListD3D12() : m_LineCount(0) {}

bool PrimitiveLineListD3D12::execute()
{
	if (m_LineCount > 0 && m_VB) {
		DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
		if ( !pDev->isCommandListOpen() )
			return true;
		ID3D12GraphicsCommandList * cl = pDev->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = m_VB->GetGPUVirtualAddress();
		vbv.SizeInBytes = m_LineCount * 2 * sizeof(Vertex);
		vbv.StrideInBytes = sizeof(Vertex);
		pDev->bindPSO( PSOKey::IL_VERTEX, PSOKey::TOPO_LINE );
		cl->IASetVertexBuffers(0,1,&vbv);
		cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		cl->DrawInstanced(m_LineCount*2,1,0,0);
		DisplayDevice::sm_nLinesRendered += m_LineCount;
	}
	return true;
}

void PrimitiveLineListD3D12::clear() {}
void PrimitiveLineListD3D12::release() {
	if ( m_VB ) DisplayDeviceD3D12::safeDeferReleaseResource( m_pDevice, m_VB.Detach() );
	m_LineCount = 0;
}

bool PrimitiveLineListD3D12::initialize(int lineCount) { return initialize(lineCount, NULL); }

bool PrimitiveLineListD3D12::initialize(int lineCount, const Line * pLines)
{
	release();
	DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
	m_LineCount = lineCount;
	if (pLines)
		m_VB = CreateUploadBuffer(pDev->getDevice(), pLines, lineCount * sizeof(Line));
	return true;
}

// Provide lock/unlock to satisfy base class pure virtuals. This primitive is a "static" variant
// so lock/unlock are no-ops and return NULL.
Line * PrimitiveLineListD3D12::lock()
{
	return nullptr;
}

void PrimitiveLineListD3D12::unlock()
{
	// no-op
}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveLineListLD3D12 );
PrimitiveLineListLD3D12::PrimitiveLineListLD3D12() : m_LineCount(0), m_pLines(NULL) {}

bool PrimitiveLineListLD3D12::execute()
{
	if (m_LineCount > 0 && m_VB) {
		DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
		if ( !pDev->isCommandListOpen() )
			return true;
		ID3D12GraphicsCommandList * cl = pDev->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = m_VB->GetGPUVirtualAddress();
		vbv.SizeInBytes = m_LineCount * 2 * sizeof(VertexL);
		vbv.StrideInBytes = sizeof(VertexL);
		pDev->bindPSO( PSOKey::IL_VERTEXL, PSOKey::TOPO_LINE );
		cl->IASetVertexBuffers(0,1,&vbv);
		cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		cl->DrawInstanced(m_LineCount*2,1,0,0);
		DisplayDevice::sm_nLinesRendered += m_LineCount;
	}
	return true;
}

void PrimitiveLineListLD3D12::clear() {}
void PrimitiveLineListLD3D12::release() {
	if ( m_VB ) DisplayDeviceD3D12::safeDeferReleaseResource( m_pDevice, m_VB.Detach() );
	delete[] m_pLines; m_pLines = NULL; m_LineCount = 0;
}

bool PrimitiveLineListLD3D12::initialize(int lineCount) { 
	release();
	m_LineCount = lineCount;
	if ( lineCount > 0 )
	{
		m_pLines = new LineL[lineCount];
	}
	return true; 
}

bool PrimitiveLineListLD3D12::initialize(int lineCount, const LineL * pLines)
{
	release();
	DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
	m_LineCount = lineCount;
	if (pLines)
		m_VB = CreateUploadBuffer(pDev->getDevice(), pLines, lineCount * sizeof(LineL));
	else
	{
		if ( lineCount > 0 )
			m_pLines = new LineL[lineCount];
	}
	return true;
}

LineL * PrimitiveLineListLD3D12::lock()
{
	return m_pLines;
}

void PrimitiveLineListLD3D12::unlock()
{
	// When unlocked, if we have CPU data, upload it to the GPU buffer so execute() can use it
	if ( m_pLines && m_LineCount > 0 )
	{
		DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
		// create/update the upload buffer with the CPU data
		m_VB = CreateUploadBuffer(pDev->getDevice(), m_pLines, m_LineCount * sizeof(LineL));
	}
}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveLineListDTLD3D12 );
PrimitiveLineListDTLD3D12::PrimitiveLineListDTLD3D12() : m_pLines(NULL), m_LineCount(0) {}

bool PrimitiveLineListDTLD3D12::execute()
{
	if (m_LineCount > 0 && m_pLines) {
		DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
		if ( !pDev->isCommandListOpen() )
			return true;
		ID3D12GraphicsCommandList * cl = pDev->getCommandList();
		UINT dataSize = m_LineCount * 2 * sizeof(VertexTL);
		UploadRingBuffer::Allocation alloc = pDev->allocateDynamic( dataSize, sizeof(VertexTL) );
		memcpy( alloc.cpuAddress, m_pLines, dataSize );
		D3D12_VERTEX_BUFFER_VIEW vbv = {};
		vbv.BufferLocation = alloc.gpuAddress;
		vbv.SizeInBytes = dataSize;
		vbv.StrideInBytes = sizeof(VertexTL);
		pDev->bindPSO( PSOKey::IL_VERTEXTL, PSOKey::TOPO_LINE );
		cl->IASetVertexBuffers(0,1,&vbv);
		cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		cl->DrawInstanced(m_LineCount*2,1,0,0);
		DisplayDevice::sm_nLinesRendered += m_LineCount;
	}
	return true;
}

void PrimitiveLineListDTLD3D12::clear() {}
void PrimitiveLineListDTLD3D12::release() { delete[] m_pLines; m_pLines = NULL; m_LineCount = 0; }

bool PrimitiveLineListDTLD3D12::initialize(int lineCount)
{
	release();
	m_LineCount = lineCount;
	if ( lineCount > 0 )
		m_pLines = new LineTL[lineCount];
	return true;
}

bool PrimitiveLineListDTLD3D12::initialize(int lineCount, const LineTL * pLines)
{
	release();
	m_LineCount = lineCount;
	if ( lineCount > 0 )
	{
		m_pLines = new LineTL[lineCount];
		if (pLines)
			memcpy( m_pLines, pLines, lineCount * sizeof(LineTL) );
	}
	return true;
}

LineTL * PrimitiveLineListDTLD3D12::lock()
{
	return m_pLines;
}

void PrimitiveLineListDTLD3D12::unlock()
{
	// Dynamic TL lines are uploaded each frame in execute()
}

//------------------------------------------------------------------------------------
// EOF
