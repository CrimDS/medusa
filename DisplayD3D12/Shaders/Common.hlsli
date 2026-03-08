//-----------------------------------------------------------------------------
// File: Common.hlsli
// Desc: Shared structures and constant buffer declarations for DX12 shaders.
//       Replaces the per-shader globals from the DX9 .fx files.
// (c)2024 Palestar
//-----------------------------------------------------------------------------

#ifndef COMMON_HLSLI
#define COMMON_HLSLI

//-----------------------------------------------------------------------------
// Light types - must match D3DLIGHTTYPE enum
//-----------------------------------------------------------------------------
#define LIGHT_POINT         1
#define LIGHT_SPOT          2
#define LIGHT_DIRECTIONAL   3

//-----------------------------------------------------------------------------
// Constant Buffers
//-----------------------------------------------------------------------------

cbuffer CBPerFrame : register(b0)
{
    float4x4    mView;
    float4x4    mProj;
    float4x4    mProjOrtho;     // ortho matrix for screen-space (TL) vertices
    float3      vCameraPos;
    float       fTime;          // elapsed seconds since engine start
    float4      vGlobalAmbient;
    float2      szShadowMap;
    float       fShadowDistance;
    float       fShadowDepthRange;  // far - near of shadow projection (world units)
    float4      vShadowFocus;       // xyz = world-space shadow focus position
};

cbuffer CBPerObject : register(b1)
{
    float4x4    mWorld;
};

cbuffer CBPerMaterial : register(b2)
{
    float4      vMatDiffuse;
    float4      vMatSpecular;
    float4      vMatAmbient;
    float4      vMatEmissive;
    float       fMatSpecularPower;
    int         bEnableDiffuse;
    int         bEnableLightMap;
    int         bEnableBumpMap;
    float       fBumpDepth;
    int         bEnableShadowMap;
    int         bEnableAmbient;
    float       pad2;
};

cbuffer CBPerLight : register(b3)
{
    int         nLightType;
    float3      lightPad;
    float4      vLightDiffuse;
    float4      vLightSpecular;
    float4      vLightAmbient;
    float4      vLightPosition;
    float4      vLightDirection;
    float4      vAttenuation;
    float4      vSpot;
    float4x4    mCascadeViewProj[4];    // combined view*proj per shadow cascade
    float4      vCascadeSplits;         // cascade split distances (world units)
};

//-----------------------------------------------------------------------------
// Textures and Samplers
//-----------------------------------------------------------------------------

Texture2D           tDiffuse    : register(t0);
Texture2D           tLightMap   : register(t1);
Texture2D           tBumpMap    : register(t2);
Texture2D<float>    tShadowMap  : register(t7);

SamplerState                sLinear     : register(s0);
SamplerState                sPoint      : register(s1);
SamplerComparisonState      sShadowCmp  : register(s2);

//-----------------------------------------------------------------------------
// Vertex Shader Input / Output structures
//-----------------------------------------------------------------------------

// Standard vertex (position + normal + UV)
struct VS_INPUT
{
    float4 vPosition    : POSITION;
    float3 vNormal      : NORMAL;
    float2 vUV          : TEXCOORD0;
};

struct VS_OUTPUT
{
    float4 vPosition    : SV_POSITION;
    float2 vUV          : TEXCOORD0;
    float3 vNormal      : TEXCOORD1;
    float3 vTangent     : TEXCOORD2;
    float3 vBinormal    : TEXCOORD3;
    float4 vWorldPos    : TEXCOORD4;
    float4 vLightPos    : TEXCOORD5;
};

// Lit vertex (position + normal + diffuse color + UV)
struct VSL_INPUT
{
    float4 vPosition    : POSITION;
    float3 vNormal      : NORMAL;
    float4 vDiffuse     : COLOR0;
    float2 vUV          : TEXCOORD0;
};

struct VSL_OUTPUT
{
    float4 vPosition    : SV_POSITION;
    float4 vDiffuse     : COLOR0;
    float2 vUV          : TEXCOORD0;
};

// Transformed-lit vertex (pre-transformed position + diffuse color + UV)
struct VSTL_INPUT
{
    float4 vPosition    : POSITION;
    float4 vDiffuse     : COLOR0;
    float2 vUV          : TEXCOORD0;
};

struct VSTL_OUTPUT
{
    float4 vPosition    : SV_POSITION;
    float4 vDiffuse     : COLOR0;
    float2 vUV          : TEXCOORD0;
};

#endif // COMMON_HLSLI
