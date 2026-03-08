//-----------------------------------------------------------------------------
// File: ShadowMap.hlsl
// Desc: Shadow map generation shader for DX12.
//       Renders geometry from the light's perspective, writing NDC depth
//       to an R32_FLOAT color render target (same approach as DX9).
//       mView/mProj in CBPerFrame are set to the light's view/proj matrices.
// (c)2024 Palestar
//-----------------------------------------------------------------------------

#include "Common.hlsli"

//-----------------------------------------------------------------------------
// Vertex Shader: transform to light clip space
//-----------------------------------------------------------------------------

struct SM_VS_OUTPUT
{
    float4 vPosition : SV_POSITION;
    float  fDepth    : TEXCOORD0;       // NDC depth for color output
};

SM_VS_OUTPUT vs_main(VS_INPUT v)
{
    SM_VS_OUTPUT o;
    float4 worldPos = mul(v.vPosition, mWorld);
    float4 viewPos  = mul(worldPos, mView);
    o.vPosition     = mul(viewPos, mProj);
    o.fDepth        = o.vPosition.z / o.vPosition.w;   // NDC depth [0,1]
    return o;
}

//-----------------------------------------------------------------------------
// Pixel Shader: output NDC depth to R32_FLOAT color render target
//-----------------------------------------------------------------------------

float4 ps_main(SM_VS_OUTPUT input) : SV_TARGET
{
    return float4(input.fDepth, 0, 0, 1);
}
