//-----------------------------------------------------------------------------
// File: PassThroughL.hlsl
// Desc: Lit vertex passthrough for DX12.
//       Handles pre-lit vertices (position + normal + diffuse color + UV).
//       Transforms position through world/view/proj, passes diffuse and UV.
// (c)2024 Palestar
//-----------------------------------------------------------------------------

#include "Common.hlsli"

//-----------------------------------------------------------------------------
// Vertex Shader: transform position, pass diffuse color and UV through
//-----------------------------------------------------------------------------

VSL_OUTPUT vs_main(VSL_INPUT v)
{
    VSL_OUTPUT output;

    output.vPosition = mul(v.vPosition, mWorld);        // to world space
    output.vPosition = mul(output.vPosition, mView);    // to view space
    output.vPosition = mul(output.vPosition, mProj);    // to clip space
    output.vDiffuse  = v.vDiffuse;
    output.vUV       = v.vUV;

    return output;
}

//-----------------------------------------------------------------------------
// Pixel Shader: sample diffuse texture * vertex color
//-----------------------------------------------------------------------------

float4 ps_main(VSL_OUTPUT input) : SV_TARGET
{
    float4 vPixel;
    if (bEnableDiffuse)
        vPixel = input.vDiffuse * tDiffuse.Sample(sLinear, input.vUV);
    else
        vPixel = input.vDiffuse;

    // Add ambient contribution (matches DX9 fixed-function ambient)
    if (bEnableAmbient)
    {
        if (bEnableDiffuse)
            vPixel.xyz += tDiffuse.Sample(sLinear, input.vUV).xyz * vMatAmbient.xyz * vGlobalAmbient.xyz;
        else
            vPixel.xyz += vMatAmbient.xyz * vGlobalAmbient.xyz;
    }

    return vPixel;
}
