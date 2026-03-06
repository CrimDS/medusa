//-----------------------------------------------------------------------------
// File: Default.hlsl
// Desc: Medusa Uber Shader for DX12 - replaces Default.fx
//       Single-light Phong shader with shadow mapping, bump mapping,
//       and lightmap support. Geometry is rendered once per light.
// (c)2024 Palestar
//-----------------------------------------------------------------------------

#include "Common.hlsli"

#define SHADOW_EPSILON  5.0f

//-----------------------------------------------------------------------------
// Vertex Shader
//-----------------------------------------------------------------------------

VS_OUTPUT vs_main(VS_INPUT v)
{
    VS_OUTPUT rv;

    rv.vWorldPos    = mul(v.vPosition, mWorld);         // to world space
    rv.vPosition    = mul(rv.vWorldPos, mView);         // to view space
    rv.vPosition    = mul(rv.vPosition, mProj);         // to clip space
    rv.vUV          = v.vUV;
    rv.vNormal      = mul(v.vNormal, (float3x3)mWorld);

    // Compute tangent & binormal for bump mapping
    rv.vTangent  = float3(0, 0, 0);
    rv.vBinormal = float3(0, 0, 0);
    if (bEnableBumpMap)
    {
        float3 c1 = cross(v.vNormal, float3(0.0, 0.0, 1.0));
        float3 c2 = cross(v.vNormal, float3(0.0, 1.0, 0.0));
        if (length(c1) > length(c2))
            rv.vTangent = normalize(c1);
        else
            rv.vTangent = normalize(c2);
        rv.vBinormal = normalize(cross(v.vNormal, rv.vTangent));

        rv.vTangent  = mul(rv.vTangent, (float3x3)mWorld);
        rv.vBinormal = mul(rv.vBinormal, (float3x3)mWorld);
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
// Shadow map sampling - 3x3 PCF
//-----------------------------------------------------------------------------

float calculateLightAmount(VS_OUTPUT input)
{
    float fLightAmount = 4.0f;
    float2 vShadowUV = ((input.vLightPos.xy * 0.5f) / input.vLightPos.w) + float2(0.5f, 0.5f);
    vShadowUV.y = 1.0f - vShadowUV.y;

    float2 vTexel = 1.0f / szShadowMap;
    float fSampleSize = 1.0f;

    for (int x = -fSampleSize; x <= fSampleSize; x++)
    {
        for (int y = -fSampleSize; y <= fSampleSize; y++)
        {
            float fSMZ = tShadowMap.Sample(sPoint, vShadowUV + (float2(x, y) * vTexel)).r;
            if (fSMZ > 1.0f)
            {
                fSMZ += SHADOW_EPSILON;
                if (fSMZ < input.vLightPos.z)
                {
                    float fDistance = input.vLightPos.z - fSMZ;
                    float fDistanceScale = 1.0f - (fDistance / fShadowDistance);
                    if (fDistanceScale > 0.0f)
                        fLightAmount -= fDistanceScale;
                }
            }
        }
    }

    fLightAmount /= 4.0f;
    return saturate(fLightAmount);
}

//-----------------------------------------------------------------------------
// Phong lighting with Blinn half-angle specular
//-----------------------------------------------------------------------------

float4 applyDiffuseSpecular(float fLightAmount, VS_OUTPUT input)
{
    // Phong: I = Ia*ka*Oda + fatt*Ip[kd*Od(N.L) + ks(R.V)^n]
    float4 vPixel = 0.0f;

    float3 lightDir = vLightDirection.xyz;
    if (nLightType == LIGHT_POINT)
    {
        float3 vLightDelta = input.vWorldPos.xyz - vLightPosition.xyz;
        lightDir = normalize(vLightDelta);
        float fLightDistance = length(vLightDelta);
        fLightAmount *= 1.0f / (vAttenuation.x + (vAttenuation.y * fLightDistance));
    }

    float fDiffuseLight = fLightAmount;
    fDiffuseLight *= max(0.0f, dot(input.vNormal, -lightDir));

    if (bEnableDiffuse)
    {
        float4 vTexel = tDiffuse.Sample(sLinear, input.vUV);
        vPixel.xyz += (vTexel.xyz * vMatDiffuse.xyz * vLightDiffuse.xyz) * fDiffuseLight;
        vPixel.xyz += (vTexel.xyz * vMatEmissive.xyz);
        vPixel.w = vTexel.w * vMatDiffuse.w;
    }
    else
    {
        vPixel.xyz += (vMatDiffuse.xyz * vLightDiffuse.xyz) * fDiffuseLight;
        vPixel.xyz += vMatEmissive.xyz;
        vPixel.w = vMatDiffuse.w;
    }

    // Specular (Blinn half-angle)
    if (fMatSpecularPower > 0.0f)
    {
        float3 h = normalize(normalize(vCameraPos - input.vWorldPos.xyz) - lightDir);
        float fSpecularLight = fLightAmount;
        fSpecularLight *= pow(saturate(dot(h, input.vNormal)), fMatSpecularPower);

        if (fSpecularLight > 0.0f)
            vPixel.xyz += (vMatSpecular.xyz * vLightSpecular.xyz) * fSpecularLight;
    }

    return vPixel;
}

//-----------------------------------------------------------------------------
// Normal mapping from bump texture
//-----------------------------------------------------------------------------

float3 getBumpedNormal(VS_OUTPUT input)
{
    float3 vBump = fBumpDepth * (tBumpMap.Sample(sLinear, input.vUV).xyz - float3(0.5, 0.5, 0.5));
    float3 vBumpNormal = input.vNormal + (vBump.x * input.vTangent + vBump.y * input.vBinormal);
    vBumpNormal = normalize(vBumpNormal);
    return vBumpNormal;
}

//-----------------------------------------------------------------------------
// Pixel Shader
//-----------------------------------------------------------------------------

float4 ps_main(VS_OUTPUT input) : SV_TARGET
{
    // Normalize interpolated normal
    input.vNormal = normalize(input.vNormal);

    // Apply bump mapping if enabled
    if (bEnableBumpMap)
        input.vNormal = getBumpedNormal(input);

    // Shadow factor (1.0 = fully lit)
    float fLightAmount = 1.0f;
    if (bEnableShadowMap)
        fLightAmount = calculateLightAmount(input);

    // Diffuse + specular lighting
    float4 vPixel = applyDiffuseSpecular(fLightAmount, input);

    // Add ambient contribution
    if (bEnableAmbient)
    {
        if (bEnableDiffuse)
        {
            float4 vTexel = tDiffuse.Sample(sLinear, input.vUV);
            vPixel.xyz += vTexel.xyz * vMatAmbient.xyz * vGlobalAmbient.xyz;
        }
        else
        {
            vPixel.xyz += vMatAmbient.xyz * vGlobalAmbient.xyz;
        }
    }

    // Lightmap - additive, matching DX9 Default.fx behavior
    if (bEnableLightMap)
    {
        float4 vLM = tLightMap.Sample(sLinear, input.vUV);
        vPixel.xyz += vLM.xyz;
    }

    return saturate(vPixel);
}
