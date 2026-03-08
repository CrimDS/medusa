//-----------------------------------------------------------------------------
// File: Default.hlsl
// Desc: Medusa Uber Shader for DX12.
//       Single-light Phong shader with shadow mapping, bump mapping,
//       hemisphere ambient, Fresnel rim lighting, and lightmap support.
//       Geometry is rendered once per light.
// (c)2024 Palestar
//-----------------------------------------------------------------------------

#include "Common.hlsli"

//-----------------------------------------------------------------------------
// Shadow tuning
//-----------------------------------------------------------------------------

#define SHADOW_BIAS_WORLD   5.0f

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

    // Project to light space for shadow mapping
    rv.vLightPos = float4(0, 0, 0, 1);
    if (bEnableShadowMap)
    {
        rv.vLightPos = mul(rv.vWorldPos, mLightView);
        rv.vLightPos = mul(rv.vLightPos, mLightProj);
    }

    return rv;
}

//-----------------------------------------------------------------------------
// Shadow map sampling — distance-based attenuation (matches DX9 approach)
//-----------------------------------------------------------------------------

float calculateLightAmount(VS_OUTPUT input)
{
    float2 vShadowUV = (input.vLightPos.xy / input.vLightPos.w) * float2(0.5f, -0.5f) + 0.5f;

    if (any(vShadowUV < 0.0f) || any(vShadowUV > 1.0f))
        return 1.0f;

    float fRefDepth = input.vLightPos.z / input.vLightPos.w;
    float fBiasNDC = (fShadowDepthRange > 0.0f) ? (SHADOW_BIAS_WORLD / fShadowDepthRange) : 0.001f;
    float fInvShadowDist = 1.0f / max(fShadowDistance, 0.001f);

    float2 vTexel = 1.0f / szShadowMap;
    float fLightAmount = 4.0f;

    [unroll]
    for (int x = -1; x <= 1; x++)
    {
        [unroll]
        for (int y = -1; y <= 1; y++)
        {
            float fStoredDepth = tShadowMap.SampleLevel(sLinear, vShadowUV + float2(x, y) * vTexel, 0).r;

            if (fStoredDepth < 0.999f)
            {
                float fBiasedDepth = fStoredDepth + fBiasNDC;
                float fDepthDiff = fRefDepth - fBiasedDepth;
                if (fDepthDiff > 0.0f)
                {
                    float fDistanceScale = 1.0f - (fDepthDiff * fShadowDepthRange * fInvShadowDist);
                    fLightAmount -= max(fDistanceScale, 0.0f);
                }
            }
        }
    }

    return saturate(fLightAmount * 0.25f);
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
