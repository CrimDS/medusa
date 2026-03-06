//-----------------------------------------------------------------------------
// File: PostProcess.hlsl
// Desc: All post-processing effects in one file for DX12.
//       Replaces PS_Blur.fx, PS_BrightPass.fx, PS_HorzBlur.fx,
//       PS_VertBlur.fx, PS_Combine.fx, PS_Scale.fx, PS_PassThrough.fx.
//       Each effect has its own PS entry point.
// (c)2024 Palestar
//-----------------------------------------------------------------------------

#include "Common.hlsli"

//-----------------------------------------------------------------------------
// Post-process constant buffer
//-----------------------------------------------------------------------------

cbuffer CBPostProcess : register(b4)
{
    float   fScale;
    float   fMiddleGray;
    float   fWhiteCutoff;
    float   fLuminance;
    float2  texelSize;
    float   fHorzScale;
    float   fVertScale;
};

//-----------------------------------------------------------------------------
// Additional texture for combine/blur operations
//-----------------------------------------------------------------------------

Texture2D   tPostProcess : register(t1);

//-----------------------------------------------------------------------------
// Full-screen quad/triangle vertex shader
//-----------------------------------------------------------------------------

struct VS_PP_OUTPUT
{
    float4 vPosition    : SV_POSITION;
    float2 vUV          : TEXCOORD0;
};

VS_PP_OUTPUT vs_main(uint vertexID : SV_VertexID)
{
    VS_PP_OUTPUT output;

    // Generate a full-screen triangle from vertex ID (0, 1, 2)
    // This produces a triangle that covers the entire screen:
    //   vertex 0: (-1, -1) UV (0, 1)
    //   vertex 1: (-1,  3) UV (0,-1)
    //   vertex 2: ( 3, -1) UV (2, 1)
    output.vUV = float2((vertexID << 1) & 2, vertexID & 2);
    output.vPosition = float4(output.vUV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);

    return output;
}

//-----------------------------------------------------------------------------
// PS_Blur: Combine scene + blurred texture (replaces PS_Blur.fx)
//-----------------------------------------------------------------------------

float4 PS_Blur(VS_PP_OUTPUT input) : SV_TARGET
{
    float3 ColorOrig = tDiffuse.Sample(sLinear, input.vUV).rgb;
    ColorOrig += tPostProcess.Sample(sLinear, input.vUV).rgb * fScale;
    return float4(ColorOrig, 1.0f);
}

//-----------------------------------------------------------------------------
// PS_BrightPass: Tone mapping bright pass filter (replaces PS_BrightPass.fx)
//-----------------------------------------------------------------------------

float4 PS_BrightPass(VS_PP_OUTPUT input) : SV_TARGET
{
    float3 ColorOut = tDiffuse.Sample(sLinear, input.vUV).rgb;

    ColorOut *= fMiddleGray / (fLuminance + 0.001f);
    ColorOut *= (1.0f + (ColorOut / (fWhiteCutoff * fWhiteCutoff)));
    ColorOut -= 5.0f;

    ColorOut = max(ColorOut, 0.0f);

    ColorOut /= (10.0f + ColorOut);

    return float4(ColorOut, 1.0f);
}

//-----------------------------------------------------------------------------
// 13-tap Gaussian blur weights (shared by horz and vert)
//-----------------------------------------------------------------------------

static const int BLUR_SIZE = 13;

static const float BlurWeights[BLUR_SIZE] =
{
    0.002216,
    0.008764,
    0.026995,
    0.064759,
    0.120985,
    0.176033,
    0.199471,
    0.176033,
    0.120985,
    0.064759,
    0.026995,
    0.008764,
    0.002216,
};

//-----------------------------------------------------------------------------
// PS_HorzBlur: 13-tap horizontal Gaussian blur (replaces PS_HorzBlur.fx)
//-----------------------------------------------------------------------------

float4 PS_HorzBlur(VS_PP_OUTPUT input) : SV_TARGET
{
    float4 Color = 0;

    [unroll]
    for (int i = 0; i < BLUR_SIZE; i++)
    {
        float2 offset = float2((float)(i - 6), 0.0f) * texelSize;
        Color += tDiffuse.Sample(sLinear, input.vUV + offset) * BlurWeights[i] * fHorzScale;
    }

    return Color;
}

//-----------------------------------------------------------------------------
// PS_VertBlur: 13-tap vertical Gaussian blur (replaces PS_VertBlur.fx)
//-----------------------------------------------------------------------------

float4 PS_VertBlur(VS_PP_OUTPUT input) : SV_TARGET
{
    float4 Color = 0;

    [unroll]
    for (int i = 0; i < BLUR_SIZE; i++)
    {
        float2 offset = float2(0.0f, (float)(i - 6)) * texelSize;
        Color += tDiffuse.Sample(sLinear, input.vUV + offset) * BlurWeights[i] * fVertScale;
    }

    return Color;
}

//-----------------------------------------------------------------------------
// PS_Combine: Combine diffuse + bloom (replaces PS_Combine.fx)
//   Same algorithm as PS_Blur
//-----------------------------------------------------------------------------

float4 PS_Combine(VS_PP_OUTPUT input) : SV_TARGET
{
    float3 ColorOrig = tDiffuse.Sample(sLinear, input.vUV).rgb;
    ColorOrig += tPostProcess.Sample(sLinear, input.vUV).rgb * fScale;
    return float4(ColorOrig, 1.0f);
}

//-----------------------------------------------------------------------------
// PS_Scale: Multiply by fScale (replaces PS_Scale.fx)
//-----------------------------------------------------------------------------

float4 PS_Scale(VS_PP_OUTPUT input) : SV_TARGET
{
    return tDiffuse.Sample(sLinear, input.vUV) * fScale;
}

//-----------------------------------------------------------------------------
// PS_PassThrough: Simple texture sample (replaces PS_PassThrough.fx for post)
//-----------------------------------------------------------------------------

float4 PS_PassThrough(VS_PP_OUTPUT input) : SV_TARGET
{
    return tDiffuse.Sample(sLinear, input.vUV);
}

// Default PS entry point (pass-through) — satisfies the shader loader
float4 ps_main(VS_PP_OUTPUT input) : SV_TARGET
{
    return PS_PassThrough(input);
}
