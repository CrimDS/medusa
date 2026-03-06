//-----------------------------------------------------------------------------
// File: ShadowMap.hlsl
// Desc: Shadow map generation shader for DX12 - replaces ShadowMap.fx
//       Renders depth from the light's perspective.
// (c)2024 Palestar
//-----------------------------------------------------------------------------

#include "Common.hlsli"

//-----------------------------------------------------------------------------
// Shadow map vertex output
//-----------------------------------------------------------------------------

struct VS_SHADOW_OUTPUT
{
    float4 vPosition    : SV_POSITION;
    float2 vDepth       : TEXCOORD0;
};

//-----------------------------------------------------------------------------
// Vertex Shader: transform to light space and output depth
//-----------------------------------------------------------------------------

VS_SHADOW_OUTPUT vs_main(VS_INPUT v)
{
    VS_SHADOW_OUTPUT output;

    output.vPosition = mul(v.vPosition, mWorld);        // model to world
    output.vPosition = mul(output.vPosition, mLightView);  // world to light view
    output.vPosition = mul(output.vPosition, mLightProj);  // light view to light proj

    // Store z and w for depth computation
    output.vDepth.xy = output.vPosition.zw;

    return output;
}

//-----------------------------------------------------------------------------
// Pixel Shader: output depth value
//-----------------------------------------------------------------------------

float4 ps_main(VS_SHADOW_OUTPUT input) : SV_TARGET
{
    // Output raw z depth (matching the non-W-buffer path from the original)
    return input.vDepth.x;
}
