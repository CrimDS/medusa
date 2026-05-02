/*
	PrimitiveTriangleListD3D12.cpp
	(c)2024 Palestar
*/

#include "DisplayD3D12/PrimitiveTriangleListD3D12.h"
#include "DisplayD3D12/PrimitiveFactory.h"

//------------------------------------------------------------------------------------
// Helper: create an UPLOAD heap buffer and copy data into it

static ComPtr<ID3D12Resource> CreateUploadBuffer( ID3D12Device * pDevice, const void * pData, UINT dataSize )
{
	if ( !pDevice || dataSize == 0 )
		return nullptr;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC resDesc = {};
	resDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resDesc.Width = dataSize;
	resDesc.Height = 1;
	resDesc.DepthOrArraySize = 1;
	resDesc.MipLevels = 1;
	resDesc.SampleDesc.Count = 1;
	resDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ComPtr<ID3D12Resource> buffer;
	HRESULT hr = pDevice->CreateCommittedResource( &heapProps, D3D12_HEAP_FLAG_NONE,
		&resDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer) );
	if ( FAILED(hr) )
		return nullptr;

	if ( pData )
	{
		void * pMapped = nullptr;
		D3D12_RANGE readRange = { 0, 0 };
		buffer->Map( 0, &readRange, &pMapped );
		if ( pMapped )
		{
			memcpy( pMapped, pData, dataSize );
			buffer->Unmap( 0, nullptr );
		}
	}

	return buffer;
}

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleListD3D12 );

PrimitiveTriangleListD3D12::PrimitiveTriangleListD3D12() : m_VBSize(0)
{}

bool PrimitiveTriangleListD3D12::execute()
{
	int triangleCount = m_VBSize / 3;
	if ( triangleCount > 0 && m_VB )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();

		D3D12_VERTEX_BUFFER_VIEW vbView = {};
		vbView.BufferLocation = m_VB->GetGPUVirtualAddress();
		vbView.SizeInBytes = m_VBSize * sizeof(Vertex);
		vbView.StrideInBytes = sizeof(Vertex);

		pDevice->bindPSO( PSOKey::IL_VERTEX, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers( 0, 1, &vbView );
		pCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		pCmdList->DrawInstanced( m_VBSize, 1, 0, 0 );
		DisplayDevice::sm_nTrianglesRendered += triangleCount;
	}
	return true;
}

void PrimitiveTriangleListD3D12::clear()
{}

void PrimitiveTriangleListD3D12::release()
{
	// Defer GPU resource release until the in-flight frame's fence has
	// signalled.  This primitive can be torn down on the SimThread (e.g.
	// NodeComplexMesh2::invalidate from NounPlanet::postInitialize), and
	// the main render thread may still have a VBV referencing m_VB in a
	// not-yet-executed command list.  Detach() transfers the AddRef
	// directly to the device's deferred list.
	if ( m_VB )
		DisplayDeviceD3D12::safeDeferReleaseResource( m_pDevice, m_VB.Detach() );
	m_VBSize = 0;
}

bool PrimitiveTriangleListD3D12::initialize( int triangleCount, const Triangle * pTriangles )
{
	release();
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
	m_VBSize = triangleCount * 3;
	dword vbSize = m_VBSize * sizeof(Vertex);
	m_VB = CreateUploadBuffer( pDevice->getDevice(), pTriangles, vbSize );
	return m_VB != nullptr;
}

//----------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleListDD3D12 );

PrimitiveTriangleListDD3D12::PrimitiveTriangleListDD3D12() : m_pTriangles(NULL), m_TriangleCount(0)
{}

bool PrimitiveTriangleListDD3D12::execute()
{
	if ( m_TriangleCount > 0 && m_pTriangles )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		UINT dataSize = m_TriangleCount * sizeof(Triangle);
		UploadRingBuffer::Allocation alloc = pDevice->allocateDynamic( dataSize, sizeof(Vertex) );
		memcpy( alloc.cpuAddress, m_pTriangles, dataSize );

		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbView = {};
		vbView.BufferLocation = alloc.gpuAddress;
		vbView.SizeInBytes = dataSize;
		vbView.StrideInBytes = sizeof(Vertex);

		pDevice->bindPSO( PSOKey::IL_VERTEX, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers( 0, 1, &vbView );
		pCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		pCmdList->DrawInstanced( m_TriangleCount * 3, 1, 0, 0 );
		DisplayDevice::sm_nTrianglesRendered += m_TriangleCount;
	}
	return true;
}

void PrimitiveTriangleListDD3D12::clear()
{}

void PrimitiveTriangleListDD3D12::release()
{
	delete[] m_pTriangles;
	m_pTriangles = NULL;
	m_TriangleCount = 0;
}

bool PrimitiveTriangleListDD3D12::initialize( int triangleCount )
{
	release();
	m_pTriangles = new Triangle[ triangleCount ];
	m_TriangleCount = triangleCount;
	return true;
}

bool PrimitiveTriangleListDD3D12::initialize( int triangleCount, const Triangle * pTriangles )
{
	initialize( triangleCount );
	memcpy( m_pTriangles, pTriangles, sizeof(Triangle) * triangleCount );
	return true;
}

Triangle * PrimitiveTriangleListDD3D12::lock()
{
	return m_pTriangles;
}

void PrimitiveTriangleListDD3D12::unlock()
{}

//----------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleListID3D12 );

PrimitiveTriangleListID3D12::PrimitiveTriangleListID3D12() : m_Triangles(0), m_Verts(0)
{}

bool PrimitiveTriangleListID3D12::execute()
{
	if ( m_Triangles > 0 && m_VB && m_IB )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();

		D3D12_VERTEX_BUFFER_VIEW vbView = {};
		vbView.BufferLocation = m_VB->GetGPUVirtualAddress();
		vbView.SizeInBytes = m_Verts * sizeof(Vertex);
		vbView.StrideInBytes = sizeof(Vertex);

		D3D12_INDEX_BUFFER_VIEW ibView = {};
		ibView.BufferLocation = m_IB->GetGPUVirtualAddress();
		ibView.SizeInBytes = m_Triangles * 3 * sizeof(word);
		ibView.Format = DXGI_FORMAT_R16_UINT;

		pDevice->bindPSO( PSOKey::IL_VERTEX, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers( 0, 1, &vbView );
		pCmdList->IASetIndexBuffer( &ibView );
		pCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		pCmdList->DrawIndexedInstanced( m_Triangles * 3, 1, 0, 0, 0 );
		DisplayDevice::sm_nTrianglesRendered += m_Triangles;
	}
	return true;
}

void PrimitiveTriangleListID3D12::clear()
{}

void PrimitiveTriangleListID3D12::release()
{
	// Defer release — m_VB / m_IB may still be bound in a main-thread
	// command list when NodeComplexMesh2::invalidate runs on SimThread.
	// This is THE primitive used for procedurally-subdivided planet meshes
	// (NodeComplexMesh2 holds an Array<PrimitiveTriangleListI::Ref>).
	// safeDeferReleaseResource handles the case where m_pDevice is dangling
	// (device already destroyed) — see DisplayDeviceD3D12.h for context.
	if ( m_VB ) DisplayDeviceD3D12::safeDeferReleaseResource( m_pDevice, m_VB.Detach() );
	if ( m_IB ) DisplayDeviceD3D12::safeDeferReleaseResource( m_pDevice, m_IB.Detach() );
	m_Triangles = 0;
	m_Verts = 0;
}

bool PrimitiveTriangleListID3D12::initialize( int triangleCount, const Triangle * pTriangles )
{
	release();
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;

	m_Triangles = triangleCount;

	Array< Vertex > verts;
	Array< word > vbi( m_Triangles * 3 );

	int index = 0;
	for ( int i = 0; i < triangleCount; i++ )
		for ( int j = 0; j < 3; j++ )
		{
			const Vertex & vert = pTriangles[i].v[j];
			vbi[ index ] = 0xffff;

			for ( int k = 0; k < verts.size(); k++ )
				if ( (verts[k].position - vert.position).magnitude2() < 0.001f &&
					(verts[k].normal - vert.normal).magnitude2() < 0.001f &&
					fabs(verts[k].u - vert.u) < 0.001f &&
					fabs(verts[k].v - vert.v) < 0.001f )
				{
					vbi[ index ] = k;
					break;
				}

			if ( vbi[ index ] == 0xffff )
			{
				verts.push( vert );
				vbi[ index ] = verts.size() - 1;
			}
			index++;
		}

	m_Verts = verts.size();

	m_VB = CreateUploadBuffer( pDevice->getDevice(), &verts[0], m_Verts * sizeof(Vertex) );
	if ( !m_VB )
		return false;

	m_IB = CreateUploadBuffer( pDevice->getDevice(), &vbi[0], vbi.size() * sizeof(word) );
	return m_IB != nullptr;
}

bool PrimitiveTriangleListID3D12::initialize( int triangleCount, const word * pTriangles,
	int vertexCount, const Vertex * pVerticies )
{
	release();
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;

	m_Triangles = triangleCount;
	m_Verts = vertexCount;

	m_VB = CreateUploadBuffer( pDevice->getDevice(), pVerticies, m_Verts * sizeof(Vertex) );
	if ( !m_VB )
		return false;

	m_IB = CreateUploadBuffer( pDevice->getDevice(), pTriangles, m_Triangles * 3 * sizeof(word) );
	return m_IB != nullptr;
}

bool PrimitiveTriangleListID3D12::initialize( int triangleCount, const word * pTriangles,
	PrimitiveTriangleListI * pVerts )
{
	release();
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;

	m_Triangles = triangleCount;
	PrimitiveTriangleListID3D12 * pOther = (PrimitiveTriangleListID3D12 *)pVerts;
	m_Verts = pOther->m_Verts;
	m_VB = pOther->m_VB;		// share vertex buffer via ComPtr

	m_IB = CreateUploadBuffer( pDevice->getDevice(), pTriangles, m_Triangles * 3 * sizeof(word) );
	return m_IB != nullptr;
}

//----------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleListLD3D12 );

PrimitiveTriangleListLD3D12::PrimitiveTriangleListLD3D12() : m_VBSize(0)
{}

bool PrimitiveTriangleListLD3D12::execute()
{
	int triangleCount = m_VBSize / 3;
	if ( triangleCount > 0 && m_VB )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();

		D3D12_VERTEX_BUFFER_VIEW vbView = {};
		vbView.BufferLocation = m_VB->GetGPUVirtualAddress();
		vbView.SizeInBytes = m_VBSize * sizeof(VertexL);
		vbView.StrideInBytes = sizeof(VertexL);

		pDevice->bindPSO( PSOKey::IL_VERTEXL, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers( 0, 1, &vbView );
		pCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		pCmdList->DrawInstanced( m_VBSize, 1, 0, 0 );
		DisplayDevice::sm_nTrianglesRendered += triangleCount;
	}
	return true;
}

void PrimitiveTriangleListLD3D12::clear()
{}

void PrimitiveTriangleListLD3D12::release()
{
	if ( m_VB )
		DisplayDeviceD3D12::safeDeferReleaseResource( m_pDevice, m_VB.Detach() );
	m_VBSize = 0;
}

bool PrimitiveTriangleListLD3D12::initialize( int triangleCount, const TriangleL * pTriangles )
{
	release();
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
	m_VBSize = triangleCount * 3;
	m_VB = CreateUploadBuffer( pDevice->getDevice(), pTriangles, m_VBSize * sizeof(VertexL) );
	return m_VB != nullptr;
}

//----------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleListDLD3D12 );

PrimitiveTriangleListDLD3D12::PrimitiveTriangleListDLD3D12() : m_pTriangles(NULL), m_TriangleCount(0)
{}

bool PrimitiveTriangleListDLD3D12::execute()
{
	if ( m_TriangleCount > 0 && m_pTriangles )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		UINT dataSize = m_TriangleCount * sizeof(TriangleL);
		UploadRingBuffer::Allocation alloc = pDevice->allocateDynamic( dataSize, sizeof(VertexL) );
		memcpy( alloc.cpuAddress, m_pTriangles, dataSize );

		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbView = {};
		vbView.BufferLocation = alloc.gpuAddress;
		vbView.SizeInBytes = dataSize;
		vbView.StrideInBytes = sizeof(VertexL);

		pDevice->bindPSO( PSOKey::IL_VERTEXL, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers( 0, 1, &vbView );
		pCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		pCmdList->DrawInstanced( m_TriangleCount * 3, 1, 0, 0 );
		DisplayDevice::sm_nTrianglesRendered += m_TriangleCount;
	}
	return true;
}

void PrimitiveTriangleListDLD3D12::clear()
{}

void PrimitiveTriangleListDLD3D12::release()
{
	delete[] m_pTriangles;
	m_pTriangles = NULL;
	m_TriangleCount = 0;
}

bool PrimitiveTriangleListDLD3D12::initialize( int triangleCount )
{
	release();
	m_pTriangles = new TriangleL[ triangleCount ];
	m_TriangleCount = triangleCount;
	return true;
}

bool PrimitiveTriangleListDLD3D12::initialize( int triangleCount, const TriangleL * pTriangles )
{
	initialize( triangleCount );
	memcpy( m_pTriangles, pTriangles, sizeof(TriangleL) * triangleCount );
	return true;
}

TriangleL * PrimitiveTriangleListDLD3D12::lock()
{
	return m_pTriangles;
}

void PrimitiveTriangleListDLD3D12::unlock()
{}

//----------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleListTLD3D12 );

PrimitiveTriangleListTLD3D12::PrimitiveTriangleListTLD3D12() : m_VBSize(0)
{}

bool PrimitiveTriangleListTLD3D12::execute()
{
	int triangleCount = m_VBSize / 3;
	if ( triangleCount > 0 && m_VB )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();

		D3D12_VERTEX_BUFFER_VIEW vbView = {};
		vbView.BufferLocation = m_VB->GetGPUVirtualAddress();
		vbView.SizeInBytes = m_VBSize * sizeof(VertexTL);
		vbView.StrideInBytes = sizeof(VertexTL);

		pDevice->bindPSO( PSOKey::IL_VERTEXTL, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers( 0, 1, &vbView );
		pCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		pCmdList->DrawInstanced( m_VBSize, 1, 0, 0 );
		DisplayDevice::sm_nTrianglesRendered += triangleCount;
	}
	return true;
}

void PrimitiveTriangleListTLD3D12::clear()
{}

void PrimitiveTriangleListTLD3D12::release()
{
	if ( m_VB )
		DisplayDeviceD3D12::safeDeferReleaseResource( m_pDevice, m_VB.Detach() );
	m_VBSize = 0;
}

bool PrimitiveTriangleListTLD3D12::initialize( int triangleCount, const TriangleTL * pTriangles )
{
	release();
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
	m_VBSize = triangleCount * 3;
	m_VB = CreateUploadBuffer( pDevice->getDevice(), pTriangles, m_VBSize * sizeof(VertexTL) );
	return m_VB != nullptr;
}

//----------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveTriangleListDTLD3D12 );

PrimitiveTriangleListDTLD3D12::PrimitiveTriangleListDTLD3D12() : m_pTriangles(NULL), m_TriangleCount(0)
{}

bool PrimitiveTriangleListDTLD3D12::execute()
{
	if ( m_TriangleCount > 0 && m_pTriangles )
	{
		DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
		UINT dataSize = m_TriangleCount * sizeof(TriangleTL);
		UploadRingBuffer::Allocation alloc = pDevice->allocateDynamic( dataSize, sizeof(VertexTL) );
		memcpy( alloc.cpuAddress, m_pTriangles, dataSize );

		ID3D12GraphicsCommandList * pCmdList = pDevice->getCommandList();
		D3D12_VERTEX_BUFFER_VIEW vbView = {};
		vbView.BufferLocation = alloc.gpuAddress;
		vbView.SizeInBytes = dataSize;
		vbView.StrideInBytes = sizeof(VertexTL);

		static int s_nTLLog = 0;
		if ( s_nTLLog < 10 )
		{
			// Log first few vertex positions to verify where text is being drawn
			VertexTL * pV = (VertexTL *)m_pTriangles;
			TRACE( "TriangleListDTL: triangles=%d, v0=(%.1f,%.1f) v1=(%.1f,%.1f) v2=(%.1f,%.1f) color=0x%08X",
				m_TriangleCount,
				pV[0].position.x, pV[0].position.y,
				pV[1].position.x, pV[1].position.y,
				pV[2].position.x, pV[2].position.y,
				pV[0].diffuse.BGRA() );
			++s_nTLLog;
		}

		pDevice->bindPSO( PSOKey::IL_VERTEXTL, PSOKey::TOPO_TRIANGLE );
		pCmdList->IASetVertexBuffers( 0, 1, &vbView );
		pCmdList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		pCmdList->DrawInstanced( m_TriangleCount * 3, 1, 0, 0 );
		DisplayDevice::sm_nTrianglesRendered += m_TriangleCount;
	}
	return true;
}

void PrimitiveTriangleListDTLD3D12::clear()
{}

void PrimitiveTriangleListDTLD3D12::release()
{
	delete[] m_pTriangles;
	m_pTriangles = NULL;
	m_TriangleCount = 0;
}

bool PrimitiveTriangleListDTLD3D12::initialize( int triangleCount )
{
	release();
	m_pTriangles = new TriangleTL[ triangleCount ];
	m_TriangleCount = triangleCount;
	return true;
}

bool PrimitiveTriangleListDTLD3D12::initialize( int triangleCount, const TriangleTL * pTriangles )
{
	initialize( triangleCount );
	memcpy( m_pTriangles, pTriangles, sizeof(TriangleTL) * triangleCount );
	return true;
}

TriangleTL * PrimitiveTriangleListDTLD3D12::lock()
{
	return m_pTriangles;
}

void PrimitiveTriangleListDTLD3D12::unlock()
{}

//------------------------------------------------------------------------------------
// EOF
