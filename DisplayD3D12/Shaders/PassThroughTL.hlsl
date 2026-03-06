//-----------------------------------------------------------------------------
// File: PassThroughTL.hlsl
// Desc: Pre-transformed (screen-space) vertex passthrough for DX12.
//       Handles vertices that are already in screen coordinates (like the
//       old D3DFVF_XYZRHW vertices from DX9).
// (c)2024 Palestar
//-----------------------------------------------------------------------------

#include "Common.hlsli"

//-----------------------------------------------------------------------------
// Vertex Shader: convert screen-space coordinates to clip space
//
// The incoming vPosition is in screen pixels (x, y, z, rhw) just like
// DX9 transformed vertices. We need to convert to NDC for DX12.
// We use the view and projection matrices to extract the viewport size;
// mProj._11 and mProj._22 contain the projection scaling factors, and
// mView is identity for 2D rendering. We reconstruct viewport dimensions
// from the projection matrix, but for pre-transformed vertices we simply
// use the standard screen-to-NDC conversion.
//
// The viewport dimensions are passed via the projection matrix diagonal:
//   width  = 2.0 / mProj._11  (when mProj is an ortho matrix)
//   height = 2.0 / mProj._22
// For pre-transformed verts, the C++ side sets up mProj as an ortho
// matrix matching the viewport, so we can use it directly.
//-----------------------------------------------------------------------------

VSTL_OUTPUT vs_main(VSTL_INPUT v)
{
    VSTL_OUTPUT output;

    // For pre-transformed vertices, the position is already in screen space.
    // Convert from screen coordinates to clip space using the orthographic
    // projection matrix that the C++ code provides matching the viewport.
    // Simply multiply through the ortho projection.
    float4 pos = float4(v.vPosition.xyz, 1.0f);
    output.vPosition = mul(pos, mProjOrtho);

    output.vDiffuse = v.vDiffuse;
    output.vUV      = v.vUV;

    return output;
}

//-----------------------------------------------------------------------------
// Pixel Shader: sample diffuse texture * vertex color, or just vertex color
//-----------------------------------------------------------------------------

float4 ps_main(VSTL_OUTPUT input) : SV_TARGET
{
    if (bEnableDiffuse)
    {
        float4 texColor = tDiffuse.Sample(sLinear, input.vUV);
        return input.vDiffuse * texColor;
    }
    else
    {
        return input.vDiffuse;
    }
}
