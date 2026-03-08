//-----------------------------------------------------------------------------
// File: Default.hlsl
// Desc: Medusa Uber Shader for DX12.
//       Single-light Phong shader with cascaded shadow mapping, bump mapping,
//       hemisphere ambient, Fresnel rim lighting, and lightmap support.
//       Geometry is rendered once per light.
// (c)2024 Palestar
//-----------------------------------------------------------------------------

#include "Common.hlsli"

//-----------------------------------------------------------------------------
// Shadow tuning
//-----------------------------------------------------------------------------

#define SHADOW_BIAS_WORLD       3.0f
#define NUM_CASCADES            4
#define CASCADE_BLEND_RANGE     0.15f   // fraction of cascade radius used for blending

//-----------------------------------------------------------------------------
// Vertex Shader
//-----------------------------------------------------------------------------

VS_OUTPUT vs_main(VS_INPUT v)
{
    VS_OUTPUT rv;

    rv.vWorldPos    = mul(v.vPosition, mWorld);
    rv.vPosition    = mul(rv.vWorldPos, mView);
    rv.vPosition    = mul(rv.vPosition, mProj);
    rv.vUV          = v.vUV;
    rv.vNormal      = mul(v.vNormal, (float3x3)mWorld);

    // Tangent & binormal only needed for bump mapping
    rv.vTangent  = float3(0, 0, 0);
    rv.vBinormal = float3(0, 0, 0);
    if (bEnableBumpMap)
    {
        float3 c1 = cross(v.vNormal, float3(0.0, 0.0, 1.0));
        float3 c2 = cross(v.vNormal, float3(0.0, 1.0, 0.0));
        float3 tangent = dot(c1, c1) > dot(c2, c2) ? c1 : c2;
        rv.vTangent  = mul(normalize(tangent), (float3x3)mWorld);
        rv.vBinormal = mul(normalize(cross(v.vNormal, tangent)), (float3x3)mWorld);
    }

    // vLightPos unused with CSM — projection done per-pixel in PS
    rv.vLightPos = float4(0, 0, 0, 1);

    return rv;
}

//-----------------------------------------------------------------------------
// Cascaded shadow map sampling
// Atlas layout: 2x2 grid, each quadrant = szShadowMap/2
//   Cascade 0: top-left     Cascade 1: top-right
//   Cascade 2: bottom-left  Cascade 3: bottom-right
//-----------------------------------------------------------------------------

// 16-tap Poisson disk for smooth soft shadows
static const float2 poissonDisk[16] = {
    float2(-0.9420f, -0.3990f), float2( 0.9456f, -0.7685f),
    float2(-0.0942f, -0.9294f), float2( 0.3448f,  0.2935f),
    float2(-0.9159f,  0.4579f), float2(-0.8154f, -0.8796f),
    float2(-0.3826f,  0.2768f), float2( 0.9748f,  0.7562f),
    float2( 0.4432f, -0.4032f), float2(-0.5067f, -0.0876f),
    float2( 0.0592f,  0.8639f), float2(-0.2370f,  0.8855f),
    float2( 0.3942f,  0.7170f), float2( 0.7413f, -0.1150f),
    float2(-0.6667f,  0.6773f), float2( 0.1584f, -0.6624f)
};

// Interleaved gradient noise for per-pixel rotation (no banding)
float interleavedGradientNoise(float2 screenPos)
{
    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(screenPos, magic.xy)));
}

float sampleShadowCascade(int cascade, float3 worldPos, float2 screenPos)
{
    static const float2 cascadeOffsets[NUM_CASCADES] = {
        float2(0.0f, 0.0f), float2(0.5f, 0.0f),
        float2(0.0f, 0.5f), float2(0.5f, 0.5f)
    };

    // Project to cascade's light space
    float4 lightPos = mul(float4(worldPos, 1.0f), mCascadeViewProj[cascade]);

    float2 vLocalUV = (lightPos.xy / lightPos.w) * float2(0.5f, -0.5f) + 0.5f;
    if (any(vLocalUV < 0.0f) || any(vLocalUV > 1.0f))
        return 1.0f;

    // Map to atlas quadrant
    float2 vAtlasUV = vLocalUV * 0.5f + cascadeOffsets[cascade];

    float fRefDepth = lightPos.z / lightPos.w;
    float fBiasNDC = (fShadowDepthRange > 0.0f) ? (SHADOW_BIAS_WORLD / fShadowDepthRange) : 0.001f;
    float fInvShadowDist = 1.0f / max(fShadowDistance, 0.001f);

    float2 vTexel = 1.0f / szShadowMap;

    // PCF bounds clamped to cascade quadrant
    float2 quadMin = cascadeOffsets[cascade] + vTexel;
    float2 quadMax = cascadeOffsets[cascade] + 0.5f - vTexel;

    // Per-pixel rotation angle from interleaved gradient noise
    float angle = interleavedGradientNoise(screenPos) * 6.2831853f;
    float sa = sin(angle);
    float ca = cos(angle);

    // Poisson disk PCF — 16 taps with per-pixel rotation
    float fShadow = 0.0f;
    float filterRadius = 2.5f;     // in texels

    [unroll]
    for (int i = 0; i < 16; i++)
    {
        // Rotate the Poisson sample
        float2 offset = float2(
            poissonDisk[i].x * ca - poissonDisk[i].y * sa,
            poissonDisk[i].x * sa + poissonDisk[i].y * ca
        );
        float2 sampleUV = clamp(vAtlasUV + offset * filterRadius * vTexel, quadMin, quadMax);
        float fStoredDepth = tShadowMap.SampleLevel(sLinear, sampleUV, 0).r;

        if (fStoredDepth < 0.999f)
        {
            float fBiasedDepth = fStoredDepth + fBiasNDC;
            float fDepthDiff = fRefDepth - fBiasedDepth;
            if (fDepthDiff > 0.0f)
            {
                float fDistanceScale = 1.0f - (fDepthDiff * fShadowDepthRange * fInvShadowDist);
                fShadow += max(fDistanceScale, 0.0f);
            }
        }
    }

    return saturate(1.0f - fShadow * 0.0625f);  // 1/16
}

float calculateLightAmount(VS_OUTPUT input)
{
    float dist = distance(input.vWorldPos.xyz, vShadowFocus.xyz);
    float2 screenPos = input.vPosition.xy;

    // Select primary cascade
    int cascade = 3;
    if (dist < vCascadeSplits.x) cascade = 0;
    else if (dist < vCascadeSplits.y) cascade = 1;
    else if (dist < vCascadeSplits.z) cascade = 2;

    float shadowA = sampleShadowCascade(cascade, input.vWorldPos.xyz, screenPos);

    // Blend between cascades at boundaries for smooth transitions
    if (cascade < 3)
    {
        float splitDist = vCascadeSplits[cascade];
        float blendStart = splitDist * (1.0f - CASCADE_BLEND_RANGE);
        if (dist > blendStart)
        {
            float blendFactor = saturate((dist - blendStart) / (splitDist - blendStart));
            float shadowB = sampleShadowCascade(cascade + 1, input.vWorldPos.xyz, screenPos);
            shadowA = lerp(shadowA, shadowB, blendFactor);
        }
    }

    return shadowA;
}

//-----------------------------------------------------------------------------
// Phong lighting with energy-conserving Blinn specular + Fresnel rim
//-----------------------------------------------------------------------------

float4 applyDiffuseSpecular(float fLightAmount, VS_OUTPUT input)
{
    float4 vPixel = 0.0f;

    float3 lightDir = vLightDirection.xyz;
    if (nLightType == LIGHT_POINT)
    {
        float3 vLightDelta = input.vWorldPos.xyz - vLightPosition.xyz;
        lightDir = normalize(vLightDelta);
        float fLightDistance = length(vLightDelta);
        fLightAmount *= 1.0f / (vAttenuation.x + (vAttenuation.y * fLightDistance));
    }

    // Compute NdotL once, derive half-lambert from it
    float fRawNdotL = dot(input.vNormal, -lightDir);
    float NdotL = max(0.0f, fRawNdotL);
    float fHalfLambert = fRawNdotL * 0.5f + 0.5f;
    fHalfLambert *= fHalfLambert;
    float fWrappedDiffuse = fLightAmount * lerp(NdotL, fHalfLambert, 0.3f);

    // Compute view direction once for specular + rim
    float3 viewDir = normalize(vCameraPos - input.vWorldPos.xyz);

    if (bEnableDiffuse)
    {
        float4 vTexel = tDiffuse.Sample(sLinear, input.vUV);
        vPixel.xyz += (vTexel.xyz * vMatDiffuse.xyz * vLightDiffuse.xyz) * fWrappedDiffuse;
        vPixel.xyz += (vTexel.xyz * vMatEmissive.xyz);
        vPixel.w = vTexel.w * vMatDiffuse.w;
    }
    else
    {
        vPixel.xyz += (vMatDiffuse.xyz * vLightDiffuse.xyz) * fWrappedDiffuse;
        vPixel.xyz += vMatEmissive.xyz;
        vPixel.w = vMatDiffuse.w;
    }

    // Energy-conserving Blinn-Phong specular + Fresnel rim (combined)
    if (NdotL > 0.0f)
    {
        if (fMatSpecularPower > 0.0f)
        {
            float3 h = normalize(viewDir - lightDir);
            float NdotH = saturate(dot(h, input.vNormal));
            float normFactor = (fMatSpecularPower + 8.0f) * 0.039788736f;  // 1/25.13274
            float fSpecularLight = fLightAmount * normFactor * pow(NdotH, fMatSpecularPower);

            float VdotH = saturate(dot(viewDir, h));
            float fFresnel = 0.04f + 0.96f * pow(1.0f - VdotH, 5.0f);

            vPixel.xyz += (vMatSpecular.xyz * vLightSpecular.xyz) * fSpecularLight * fFresnel;
        }

        // Fresnel rim lighting
        float rim = 1.0f - saturate(dot(viewDir, input.vNormal));
        rim = rim * rim * rim * 0.25f;    // pow(rim, 3) * 0.25
        vPixel.xyz += vLightDiffuse.xyz * rim * fLightAmount * NdotL;
    }

    return vPixel;
}

//-----------------------------------------------------------------------------
// Normal mapping from bump texture
//-----------------------------------------------------------------------------

float3 getBumpedNormal(VS_OUTPUT input)
{
    float3 vBump = fBumpDepth * (tBumpMap.Sample(sLinear, input.vUV).xyz - 0.5f);
    return normalize(input.vNormal + (vBump.x * input.vTangent + vBump.y * input.vBinormal));
}

//-----------------------------------------------------------------------------
// Pixel Shader
//-----------------------------------------------------------------------------

float4 ps_main(VS_OUTPUT input) : SV_TARGET
{
    input.vNormal = normalize(input.vNormal);

    if (bEnableBumpMap)
        input.vNormal = getBumpedNormal(input);

    float fLightAmount = 1.0f;
    if (bEnableShadowMap)
        fLightAmount = calculateLightAmount(input);

    float4 vPixel = applyDiffuseSpecular(fLightAmount, input);

    // Hemisphere ambient
    if (bEnableAmbient)
    {
        float3 ambientColor = vGlobalAmbient.xyz;
        float fUp = input.vNormal.y * 0.5f + 0.5f;
        float3 hemiAmbient = lerp(ambientColor * 0.6f, ambientColor, fUp);

        if (bEnableDiffuse)
        {
            float4 vTexel = tDiffuse.Sample(sLinear, input.vUV);
            vPixel.xyz += vTexel.xyz * vMatAmbient.xyz * hemiAmbient;
        }
        else
        {
            vPixel.xyz += vMatAmbient.xyz * hemiAmbient;
        }
    }

    // Lightmap — additive
    if (bEnableLightMap)
        vPixel.xyz += tLightMap.Sample(sLinear, input.vUV).xyz;

    return saturate(vPixel);
}
