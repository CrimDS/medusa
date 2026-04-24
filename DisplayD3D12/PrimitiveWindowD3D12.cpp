/*
	PrimitiveWindowD3D12.cpp
	(c)2024 Palestar
*/

#include "DisplayD3D12/PrimitiveWindowD3D12.h"
#include "DisplayD3D12/PrimitiveFactory.h"

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveWindowD3D12 );

PrimitiveWindowD3D12::PrimitiveWindowD3D12()
{
	memset( m_Verts, 0, sizeof(m_Verts) );
}

//----------------------------------------------------------------------------

bool PrimitiveWindowD3D12::execute()
{
	DisplayDeviceD3D12 * pDev = (DisplayDeviceD3D12 *)m_pDevice;
	UINT dataSize = 6 * sizeof(VertexTL);
	UploadRingBuffer::Allocation alloc = pDev->allocateDynamic( dataSize, sizeof(VertexTL) );
	if ( !alloc.cpuAddress )
		return false;
	memcpy( alloc.cpuAddress, m_Verts, dataSize );

	ID3D12GraphicsCommandList * cl = pDev->getCommandList();
	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	vbv.BufferLocation = alloc.gpuAddress;
	vbv.SizeInBytes = dataSize;
	vbv.StrideInBytes = sizeof(VertexTL);
	pDev->bindPSO( PSOKey::IL_VERTEXTL, PSOKey::TOPO_TRIANGLE );
	cl->IASetVertexBuffers( 0, 1, &vbv );
	cl->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	cl->DrawInstanced( 6, 1, 0, 0 );

	DisplayDevice::sm_nTrianglesRendered += 2;
	return true;
}

void PrimitiveWindowD3D12::clear()
{}

void PrimitiveWindowD3D12::release()
{}

//----------------------------------------------------------------------------

#undef RGB

bool PrimitiveWindowD3D12::initialize( const RectInt & window, const RectFloat & uv, Color diffuse )
{
	// Build 4 corner vertices (same as DX9 fan layout)
	VertexTL corners[4];
	corners[0].position.x = (float)window.left;	corners[0].position.y = (float)window.top;
	corners[1].position.x = (float)window.right;	corners[1].position.y = (float)window.top;
	corners[2].position.x = (float)window.right;	corners[2].position.y = (float)window.bottom;
	corners[3].position.x = (float)window.left;	corners[3].position.y = (float)window.bottom;

	corners[0].position.z = corners[1].position.z = corners[2].position.z = corners[3].position.z = 0.0f;
	corners[0].w = corners[1].w = corners[2].w = corners[3].w = 1.0f;
	corners[0].diffuse = corners[1].diffuse = corners[2].diffuse = corners[3].diffuse = diffuse;

	corners[0].u = uv.left;	corners[0].v = uv.top;
	corners[1].u = uv.right;	corners[1].v = uv.top;
	corners[2].u = uv.right;	corners[2].v = uv.bottom;
	corners[3].u = uv.left;	corners[3].v = uv.bottom;

	// Convert fan (0,1,2,3) to two triangles: (0,1,2) and (0,2,3)
	m_Verts[0] = corners[0];
	m_Verts[1] = corners[1];
	m_Verts[2] = corners[2];
	m_Verts[3] = corners[0];
	m_Verts[4] = corners[2];
	m_Verts[5] = corners[3];

	return true;
}

//------------------------------------------------------------------------------------
// EOF
