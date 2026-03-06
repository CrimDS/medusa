//-----------------------------------------------------------------------------
// File: PassThrough.hlsl
// Desc: Standard vertex transform + texture passthrough for DX12.
//       Replaces VS_PassThrough.fx and PS_PassThrough.fx for standard verts.
// (c)2024 Palestar
//-----------------------------------------------------------------------------

#include "Common.hlsli"

//-----------------------------------------------------------------------------
// Output structure for passthrough (includes diffuse color)
//-----------------------------------------------------------------------------

struct VS_PT_OUTPUT
{
    float4 vPosition    : SV_POSITION;
    float4 vDiffuse     : COLOR0;
    float2 vUV          : TEXCOORD0;
};

//-----------------------------------------------------------------------------
// Vertex Shader: transform through world/view/proj
//-----------------------------------------------------------------------------

VS_PT_OUTPUT vs_main(VS_INPUT v)
{
    VS_PT_OUTPUT output;

    output.vPosition = mul(v.vPosition, mWorld);    // to world space
    output.vPosition = mul(output.vPosition, mView);    // to view space
    output.vPosition = mul(output.vPosition, mProj);    // to clip space
    output.vUV       = v.vUV;
    output.vDiffuse  = vMatDiffuse;

    return output;
}

//-----------------------------------------------------------------------------
// Pixel Shader: sample diffuse texture * vertex color
//-----------------------------------------------------------------------------

float4 ps_main(VS_PT_OUTPUT input) : SV_TARGET
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
