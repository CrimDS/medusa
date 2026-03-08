//-----------------------------------------------------------------------------
// File: FXAA.hlsl
// Desc: Fast Approximate Anti-Aliasing (FXAA 3.11 quality preset)
//       Fullscreen post-process pass — samples the scene render target and
//       outputs anti-aliased pixels to the back buffer.
// (c)2024 Palestar
//-----------------------------------------------------------------------------

// Scene texture and sampler
Texture2D    tScene   : register(t0);
SamplerState sLinear  : register(s0);

cbuffer FXAA_CB : register(b0)
{
    float2 rcpFrame;    // 1.0 / renderTargetSize
    float  fSubpix;     // subpixel quality (0.75 default)
    float  fEdgeThreshold;      // edge detection threshold (0.166 default)
    float  fEdgeThresholdMin;   // minimum edge threshold (0.0833 default)
    float3 pad;
};

//-----------------------------------------------------------------------------
// Fullscreen triangle from vertex ID (no vertex buffer needed)
//-----------------------------------------------------------------------------

struct VS_OUT
{
    float4 vPosition : SV_POSITION;
    float2 vUV       : TEXCOORD0;
};

VS_OUT vs_main(uint id : SV_VertexID)
{
    VS_OUT o;
    // Generate fullscreen triangle: vertices at (-1,-1), (3,-1), (-1,3)
    o.vUV = float2((id << 1) & 2, id & 2);
    o.vPosition = float4(o.vUV * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

//-----------------------------------------------------------------------------
// Luminance helper
//-----------------------------------------------------------------------------

float FxaaLuma(float3 rgb)
{
    return rgb.y * (0.587f / 0.299f) + rgb.x;
}

//-----------------------------------------------------------------------------
// FXAA 3.11 — Quality preset
//-----------------------------------------------------------------------------

float4 ps_main(VS_OUT input) : SV_TARGET
{
    float2 posM = input.vUV;

    // Sample center and 4 neighbors
    float3 rgbM  = tScene.Sample(sLinear, posM).rgb;
    float3 rgbN  = tScene.Sample(sLinear, posM + float2( 0, -1) * rcpFrame).rgb;
    float3 rgbS  = tScene.Sample(sLinear, posM + float2( 0,  1) * rcpFrame).rgb;
    float3 rgbW  = tScene.Sample(sLinear, posM + float2(-1,  0) * rcpFrame).rgb;
    float3 rgbE  = tScene.Sample(sLinear, posM + float2( 1,  0) * rcpFrame).rgb;

    float lumaM = FxaaLuma(rgbM);
    float lumaN = FxaaLuma(rgbN);
    float lumaS = FxaaLuma(rgbS);
    float lumaW = FxaaLuma(rgbW);
    float lumaE = FxaaLuma(rgbE);

    float rangeMax = max(max(lumaN, lumaS), max(lumaW, lumaE));
    float rangeMin = min(min(lumaN, lumaS), min(lumaW, lumaE));
    rangeMax = max(rangeMax, lumaM);
    rangeMin = min(rangeMin, lumaM);
    float range = rangeMax - rangeMin;

    // Skip FXAA if contrast is below threshold
    if (range < max(fEdgeThresholdMin, rangeMax * fEdgeThreshold))
        return float4(rgbM, 1.0f);

    // Sample diagonal neighbors
    float3 rgbNW = tScene.Sample(sLinear, posM + float2(-1, -1) * rcpFrame).rgb;
    float3 rgbNE = tScene.Sample(sLinear, posM + float2( 1, -1) * rcpFrame).rgb;
    float3 rgbSW = tScene.Sample(sLinear, posM + float2(-1,  1) * rcpFrame).rgb;
    float3 rgbSE = tScene.Sample(sLinear, posM + float2( 1,  1) * rcpFrame).rgb;

    float lumaNW = FxaaLuma(rgbNW);
    float lumaNE = FxaaLuma(rgbNE);
    float lumaSW = FxaaLuma(rgbSW);
    float lumaSE = FxaaLuma(rgbSE);

    // Compute subpixel blend factor
    float lumaL = (lumaN + lumaS + lumaW + lumaE) * 0.25f;
    float rangeL = abs(lumaL - lumaM);
    float blendL = max(0.0f, (rangeL / range) - 0.25f) * (1.0f / 0.75f);
    blendL = min(blendL, 0.75f) * fSubpix;

    // Determine edge direction (horizontal vs vertical)
    float edgeH = abs(lumaNW + lumaNE - 2.0f * lumaN)
                + abs(lumaW  + lumaE  - 2.0f * lumaM) * 2.0f
                + abs(lumaSW + lumaSE - 2.0f * lumaS);
    float edgeV = abs(lumaNW + lumaSW - 2.0f * lumaW)
                + abs(lumaN  + lumaS  - 2.0f * lumaM) * 2.0f
                + abs(lumaNE + lumaSE - 2.0f * lumaE);
    bool horzSpan = (edgeH >= edgeV);

    // Choose step direction along the edge
    float lengthSign = horzSpan ? rcpFrame.y : rcpFrame.x;
    float lumaP, lumaN2;
    if (horzSpan)
    {
        lumaP = lumaS;
        lumaN2 = lumaN;
    }
    else
    {
        lumaP = lumaE;
        lumaN2 = lumaW;
    }
    float gradP = abs(lumaP - lumaM);
    float gradN = abs(lumaN2 - lumaM);

    if (gradN > gradP)
        lengthSign = -lengthSign;

    // Walk along the edge in both directions
    float2 posB = posM;
    float2 offNP = horzSpan ? float2(rcpFrame.x, 0) : float2(0, rcpFrame.y);

    if (horzSpan)
        posB.y += lengthSign * 0.5f;
    else
        posB.x += lengthSign * 0.5f;

    float2 posN = posB - offNP;
    float2 posP = posB + offNP;

    float lumaEndN = FxaaLuma(tScene.Sample(sLinear, posN).rgb);
    float lumaEndP = FxaaLuma(tScene.Sample(sLinear, posP).rgb);

    float lumaMM = lumaM - ((lumaP + lumaN2) * 0.5f);
    bool lumaMLTZero = (lumaMM < 0.0f);

    lumaEndN -= (lumaP + lumaN2) * 0.5f;
    lumaEndP -= (lumaP + lumaN2) * 0.5f;

    bool doneN = (abs(lumaEndN) >= gradP * 0.5f);
    bool doneP = (abs(lumaEndP) >= gradP * 0.5f);

    // Continue walking along edge
    [unroll]
    for (int i = 0; i < 6; i++)
    {
        if (!doneN) posN -= offNP;
        if (!doneP) posP += offNP;

        if (!doneN) lumaEndN = FxaaLuma(tScene.Sample(sLinear, posN).rgb) - (lumaP + lumaN2) * 0.5f;
        if (!doneP) lumaEndP = FxaaLuma(tScene.Sample(sLinear, posP).rgb) - (lumaP + lumaN2) * 0.5f;

        doneN = doneN || (abs(lumaEndN) >= gradP * 0.5f);
        doneP = doneP || (abs(lumaEndP) >= gradP * 0.5f);

        if (doneN && doneP) break;
    }

    // Compute edge blend factor
    float dstN, dstP;
    if (horzSpan)
    {
        dstN = posM.x - posN.x;
        dstP = posP.x - posM.x;
    }
    else
    {
        dstN = posM.y - posN.y;
        dstP = posP.y - posM.y;
    }

    float spanLength = dstN + dstP;
    bool dirN = (dstN < dstP);
    float dst = min(dstN, dstP);
    float pixelOffset = (-dst / spanLength) + 0.5f;

    bool goodSpan = dirN ? ((lumaEndN < 0.0f) != lumaMLTZero) : ((lumaEndP < 0.0f) != lumaMLTZero);
    float pixelOffsetGood = goodSpan ? pixelOffset : 0.0f;
    float pixelOffsetSubpix = max(pixelOffsetGood, blendL);

    // Final sample with subpixel offset
    float2 finalPos = posM;
    if (horzSpan)
        finalPos.y += pixelOffsetSubpix * lengthSign;
    else
        finalPos.x += pixelOffsetSubpix * lengthSign;

    float3 rgbF = tScene.Sample(sLinear, finalPos).rgb;
    return float4(rgbF, 1.0f);
}
