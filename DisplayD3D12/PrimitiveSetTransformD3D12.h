/*
	PrimitiveSetTransformD3D12.h
	(c)2024 Palestar
*/

#ifndef PRIMITIVE_SET_TRANSFORM_D3D12_H
#define PRIMITIVE_SET_TRANSFORM_D3D12_H

#include "Display/PrimitiveSetTransform.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

//----------------------------------------------------------------------------

class PrimitiveSetTransformD3D12 : public PrimitiveSetTransform
{
public:
	PrimitiveSetTransformD3D12();

	// DevicePrimitive interface
	bool					execute();
	void					clear();
	void					release();

	// PrimitiveSetTransform interface
	const Matrix33 &		transform() const;
	const Vector3 &			transformOffset() const;
	void					setTransform( const Matrix33 & transform, const Vector3 & transformOffset );

	// Data
	Matrix33				m_Transform;
	Vector3					m_PostTransformOffset;
	XMMATRIX				m_Matrix;
};

//----------------------------------------------------------------------------

#endif

//------------------------------------------------------------------------------------
// EOF
