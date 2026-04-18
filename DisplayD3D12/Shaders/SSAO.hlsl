//-----------------------------------------------------------------------------
// File: SSAO.hlsl
// Desc: Screen-Space Ambient Occlusion post-process.
//       Reconstructs view-space positions from depth, samples hemisphere
//       around each pixel, and computes occlusion factor.
//       Uses interleaved gradient noise for per-pixel rotation.
// (c)2024 Palestar
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Constant buffer
//-----------------------------------------------------------------------------

cbuffer CBSSAO : register(b0)
{
    float4x4    mProj;          // camera projection matrix
    float4x4    mInvProj;       // inverse projection matrix
    float2      texelSize;      // 1.0 / screenSize
    float       fRadius;        // AO sample radius in view-space
    float       fBias;          // depth bias to reduce self-occlusion
    float       fIntensity;     // AO intensity multiplier
    float       fScale;         // used by blur/composite pass
    float2      padSSAO;
};

//-----------------------------------------------------------------------------
// Textures and Samplers
//-----------------------------------------------------------------------------

Texture2D       tSource     : register(t0);     // depth buffer (AO pass) or AO texture (blur pass)
Texture2D       tDepth      : register(t1);     // depth buffer (blur pass only)
SamplerState    sPoint      : register(s0);

//-----------------------------------------------------------------------------
// Full-screen triangle vertex shader
//-----------------------------------------------------------------------------

struct VS_PP_OUTPUT
{
    float4 vPosition    : SV_POSITION;
    float2 vUV          : TEXCOORD0;
};

VS_PP_OUTPUT vs_main(uint vertexID : SV_VertexID)
{
    VS_PP_OUTPUT output;
    output.vUV = float2((vertexID << 1) & 2, vertexID & 2);
    output.vPosition = float4(output.vUV * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------

// Reconstruct view-space position from UV and depth
float3 viewPosFromDepth(float2 uv, float depth)
{
    // Convert UV to clip space
    float4 clipPos = float4(uv * 2.0f - 1.0f, depth, 1.0f);
    clipPos.y = -clipPos.y;
    float4 viewPos = mul(clipPos, mInvProj);
    return viewPos.xyz / viewPos.w;
}

// Interleaved gradient noise (no banding, no texture needed)
float interleavedGradientNoise(float2 screenPos)
{
    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(screenPos, magic.xy)));
}

//-----------------------------------------------------------------------------
// 16-sample hemisphere kernel (pre-computed, biased toward center)
//-----------------------------------------------------------------------------

static const float3 ssaoKernel[16] = {
    float3( 0.5381f,  0.1856f, 0.4319f),
    float3( 0.1379f,  0.2486f, 0.4430f),
    float3( 0.3371f,  0.5679f, 0.0057f),
    float3(-0.6999f, -0.0451f, 0.0019f),
    float3( 0.0689f, -0.1598f, 0.8547f),
    float3( 0.0560f,  0.0069f, 0.1843f),
    float3(-0.0146f,  0.1402f, 0.0762f),
    float3( 0.0100f, -0.1924f, 0.0344f),
    float3(-0.3577f, -0.5301f, 0.4358f),
    float3(-0.3169f,  0.1063f, 0.0158f),
    float3( 0.0103f, -0.5869f, 0.0046f),
    float3(-0.0897f, -0.4940f, 0.3287f),
    float3( 0.7119f, -0.0154f, 0.0918f),
    float3(-0.0533f,  0.0596f, 0.5411f),
    float3( 0.0352f, -0.0631f, 0.5460f),
    float3(-0.4776f,  0.2847f, 0.0271f)
};

//-----------------------------------------------------------------------------
// PS_SSAO: Compute ambient occlusion
// Input: depth buffer in t0
// Output: R8 AO factor (1=fully lit, 0=fully occluded)
//-----------------------------------------------------------------------------

float4 PS_SSAO(VS_PP_OUTPUT input) : SV_TARGET
{
    float depth = tSource.SampleLevel(sPoint, input.vUV, 0).r;

    // Skip sky (depth at max)
    if (depth >= 0.9999f)
        return float4(1, 1, 1, 1);

    float3 viewPos = viewPosFromDepth(input.vUV, depth);

    // Reconstruct view-space normal from depth derivatives
    float3 viewPosR = viewPosFromDepth(input.vUV + float2(texelSize.x, 0), tSource.SampleLevel(sPoint, input.vUV + float2(texelSize.x, 0), 0).r);
    float3 viewPosU = viewPosFromDepth(input.vUV + float2(0, texelSize.y), tSource.SampleLevel(sPoint, input.vUV + float2(0, texelSize.y), 0).r);
    float3 normal = normalize(cross(viewPosR - viewPos, viewPosU - viewPos));

    // Per-pixel random rotation via IGN
    float noise = interleavedGradientNoise(input.vPosition.xy);
    float angle = noise * 6.2831853f;
    float sa = sin(angle);
    float ca = cos(angle);

    float occlusion = 0.0f;

    [unroll]
    for (int i = 0; i < 16; i++)
    {
        // Rotate sample around view-space Z axis
        float3 sampleDir = ssaoKernel[i];
        float3 rotated = float3(
            sampleDir.x * ca - sampleDir.y * sa,
            sampleDir.x * sa + sampleDir.y * ca,
            sampleDir.z
        );

        // Flip if pointing away from normal (hemisphere orientation)
        if (dot(rotated, normal) < 0.0f)
            rotated = -rotated;

        // Sample position in view space
        float3 samplePos = viewPos + rotated * fRadius;

        // Project sample to screen space
        float4 projected = mul(float4(samplePos, 1.0f), mProj);
        projected.xy /= projected.w;
        float2 sampleUV = projected.xy * float2(0.5f, -0.5f) + 0.5f;

        // Sample depth at projected position
        float sampleDepth = tSource.SampleLevel(sPoint, sampleUV, 0).r;
        float3 sampleViewPos = viewPosFromDepth(sampleUV, sampleDepth);

        // Range check and occlusion test
        float rangeCheck = smoothstep(0.0f, 1.0f, fRadius / max(abs(viewPos.z - sampleViewPos.z), 0.001f));
        occlusion += (sampleViewPos.z <= samplePos.z - fBias ? 1.0f : 0.0f) * rangeCheck;
    }

    float ao = 1.0f - (occlusion / 16.0f) * fIntensity;
    return float4(ao, ao, ao, 1.0f);
}

//-----------------------------------------------------------------------------
// PS_SSAOBlur: Edge-preserving bilateral blur (4x4 kernel)
// Preserves edges by comparing depth differences
//-----------------------------------------------------------------------------

float4 PS_SSAOBlur(VS_PP_OUTPUT input) : SV_TARGET
{
    float centerDepth = tDepth.SampleLevel(sPoint, input.vUV, 0).r;
    float result = 0.0f;
    float totalWeight = 0.0f;

    [unroll]
    for (int x = -2; x <= 2; x++)
    {
        [unroll]
        for (int y = -2; y <= 2; y++)
        {
            float2 offset = float2(x, y) * texelSize;
            float2 sampleUV = input.vUV + offset;
            float sampleAO = tSource.SampleLevel(sPoint, sampleUV, 0).r;
            float sampleDepth = tDepth.SampleLevel(sPoint, sampleUV, 0).r;

            // Bilateral weight: reduce weight if depth differs significantly
            float depthDiff = abs(centerDepth - sampleDepth);
            float weight = exp(-depthDiff * 1000.0f);

            result += sampleAO * weight;
            totalWeight += weight;
        }
    }

    float ao = result / max(totalWeight, 0.001f);
    return float4(ao, ao, ao, 1.0f);
}

//-----------------------------------------------------------------------------
// PS_SSAOApply: Multiply scene color by AO factor
//-----------------------------------------------------------------------------

float4 PS_SSAOApply(VS_PP_OUTPUT input) : SV_TARGET
{
    float ao = tSource.SampleLevel(sPoint, input.vUV, 0).r;
    return float4(ao * fScale, ao * fScale, ao * fScale, 1.0f);
}

//-----------------------------------------------------------------------------
// Default ps_main (satisfies shader loader)
//-----------------------------------------------------------------------------

float4 ps_main(VS_PP_OUTPUT input) : SV_TARGET
{
    return tSource.Sample(sPoint, input.vUV);
}
