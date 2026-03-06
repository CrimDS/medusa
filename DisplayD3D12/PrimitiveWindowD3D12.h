/*
	PrimitiveWindowD3D12.h
	(c)2024 Palestar
*/

#ifndef PRIMITIVE_WINDOW_D3D12_H
#define PRIMITIVE_WINDOW_D3D12_H

#include "Display/PrimitiveWindow.h"
#include "DisplayD3D12/DisplayDeviceD3D12.h"

//------------------------------------------------------------------------------------

class PrimitiveWindowD3D12 : public PrimitiveWindow
{
public:
	PrimitiveWindowD3D12();

	bool			execute();
	void			clear();
	void			release();
	bool			initialize( const RectInt & window, const RectFloat & uv, Color diffuse );

private:
	// Store as 6 vertices (2 triangles) since DX12 doesn't support triangle fans
	VertexTL		m_Verts[ 6 ];
};

//----------------------------------------------------------------------------

#endif

//------------------------------------------------------------------------------------
// EOF
