//-----------------------------------------------------------------------------
// File: PostProcess.hlsl
// Desc: All post-processing effects in one file for DX12.
//       Replaces PS_Blur.fx, PS_BrightPass.fx, PS_HorzBlur.fx,
//       PS_VertBlur.fx, PS_Combine.fx, PS_Scale.fx, PS_PassThrough.fx.
//       Each effect has its own PS entry point.
// (c)2024 Palestar
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Post-process constant buffer (b0 in post-process root signature)
//-----------------------------------------------------------------------------

cbuffer CBPostProcess : register(b0)
{
    float2  texelSize;
    float   fScale;
    float   fBrightThreshold;
};

//-----------------------------------------------------------------------------
// Textures and Samplers
//-----------------------------------------------------------------------------

Texture2D       tSource     : register(t0);
SamplerState    sLinear     : register(s0);

//-----------------------------------------------------------------------------
// Full-screen triangle vertex shader (no vertex buffer needed)
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
    output.vUV = float2((vertexID << 1) & 2, vertexID & 2);
    output.vPosition = float4(output.vUV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);

    return output;
}

//-----------------------------------------------------------------------------
// PS_BrightPass: Extract bright pixels for bloom (LDR-compatible)
//   Pixels above fBrightThreshold luminance contribute to bloom.
//   Uses a soft knee to avoid hard cutoff artifacts.
//-----------------------------------------------------------------------------

float4 PS_BrightPass(VS_PP_OUTPUT input) : SV_TARGET
{
    float3 color = tSource.Sample(sLinear, input.vUV).rgb;
    float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));

    // Soft knee: smoothly ramp from 0 at threshold to full at threshold+0.1
    float knee = 0.1f;
    float brightness = max(0.0f, luminance - fBrightThreshold);
    brightness = brightness / (brightness + knee);

    return float4(color * brightness * fScale, 1.0f);
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
// PS_HorzBlur: 13-tap horizontal Gaussian blur
//-----------------------------------------------------------------------------

float4 PS_HorzBlur(VS_PP_OUTPUT input) : SV_TARGET
{
    float4 Color = 0;

    [unroll]
    for (int i = 0; i < BLUR_SIZE; i++)
    {
        float2 offset = float2((float)(i - 6), 0.0f) * texelSize;
        Color += tSource.Sample(sLinear, input.vUV + offset) * BlurWeights[i];
    }

    return Color;
}

//-----------------------------------------------------------------------------
// PS_VertBlur: 13-tap vertical Gaussian blur
//-----------------------------------------------------------------------------

float4 PS_VertBlur(VS_PP_OUTPUT input) : SV_TARGET
{
    float4 Color = 0;

    [unroll]
    for (int i = 0; i < BLUR_SIZE; i++)
    {
        float2 offset = float2(0.0f, (float)(i - 6)) * texelSize;
        Color += tSource.Sample(sLinear, input.vUV + offset) * BlurWeights[i];
    }

    return Color;
}

//-----------------------------------------------------------------------------
// PS_Scale: Multiply by fScale (used for additive bloom composite)
//-----------------------------------------------------------------------------

float4 PS_Scale(VS_PP_OUTPUT input) : SV_TARGET
{
    return tSource.Sample(sLinear, input.vUV) * fScale;
}

//-----------------------------------------------------------------------------
// Default PS entry point (pass-through) — satisfies the shader loader
//-----------------------------------------------------------------------------

float4 ps_main(VS_PP_OUTPUT input) : SV_TARGET
{
    return tSource.Sample(sLinear, input.vUV);
}
