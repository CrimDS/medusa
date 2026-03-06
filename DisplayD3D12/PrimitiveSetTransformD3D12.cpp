/*
	PrimitiveSetTransformD3D12.cpp
	(c)2024 Palestar
*/

#include "DisplayD3D12/PrimitiveSetTransformD3D12.h"
#include "DisplayD3D12/PrimitiveFactory.h"

//------------------------------------------------------------------------------------

IMPLEMENT_PRIMITIVE_FACTORY_D3D12( PrimitiveSetTransformD3D12 );

PrimitiveSetTransformD3D12::PrimitiveSetTransformD3D12()
{
	m_Matrix = XMMatrixIdentity();
}

bool PrimitiveSetTransformD3D12::execute()
{
	DisplayDeviceD3D12 * pDevice = (DisplayDeviceD3D12 *)m_pDevice;
	if ( !pDevice )
		return false;

	// Set the world matrix on the device - this will be uploaded as a constant buffer
	pDevice->setWorldMatrix( m_Matrix );
	pDevice->bindPerObjectCB();

	return true;
}

void PrimitiveSetTransformD3D12::clear()
{}

void PrimitiveSetTransformD3D12::release()
{}

//----------------------------------------------------------------------------

const Matrix33 & PrimitiveSetTransformD3D12::transform() const
{
	return(m_Transform);
}

const Vector3 &	PrimitiveSetTransformD3D12::transformOffset() const
{
	return(m_PostTransformOffset);
}

void PrimitiveSetTransformD3D12::setTransform( const Matrix33 & transform, const Vector3 & postTransformOffset )
{
	m_Transform = transform;
	m_PostTransformOffset = postTransformOffset;

	// Build XMMATRIX from Matrix33 + offset (same layout as DX9)
	m_Matrix = XMMATRIX(
		m_Transform.i.x, m_Transform.i.y, m_Transform.i.z, 0.0f,
		m_Transform.j.x, m_Transform.j.y, m_Transform.j.z, 0.0f,
		m_Transform.k.x, m_Transform.k.y, m_Transform.k.z, 0.0f,
		m_PostTransformOffset.x, m_PostTransformOffset.y, m_PostTransformOffset.z, 1.0f
	);
}

//------------------------------------------------------------------------------------
// EOF
