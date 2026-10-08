#include "Include/Render2D/HIKARI_Render2DBuiltinInterface.hlsli"

struct HikariRender2DMeshVertexInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct HikariRender2DMeshPixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct HikariRender2DLineVertexInput
{
    float2 position : POSITION;
    float4 color : COLOR0;
};

struct HikariRender2DLinePixelInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

float4 HikariRender2DPosition(float2 pixelPosition)
{
    const float2 ndc = float2(
        (pixelPosition.x / gHikariRender2DScreenSize.x) * 2.0f - 1.0f,
        1.0f - (pixelPosition.y / gHikariRender2DScreenSize.y) * 2.0f);
    return float4(ndc, 0.0f, 1.0f);
}

HikariRender2DMeshPixelInput MeshVS(
    HikariRender2DMeshVertexInput input)
{
    HikariRender2DMeshPixelInput output;
    output.position = HikariRender2DPosition(input.position);
    output.uv = input.uv;
    output.color = float4(input.color.rgb * gHikariRender2DColorScale, input.color.a);
    return output;
}

float4 MeshPS(HikariRender2DMeshPixelInput input) : SV_TARGET
{
    return gHikariRender2DTexture.Sample(
        gHikariRender2DSampler,
        input.uv) * input.color;
}

float4 MaskPS(HikariRender2DMeshPixelInput input) : SV_TARGET
{
    float4 color = gHikariRender2DTexture.Sample(
        gHikariRender2DSampler,
        input.uv) * input.color;
    const float distanceFromCenter = distance(
        input.position.xy,
        gHikariRender2DMaskCenter);
    color.a *= smoothstep(
        0.0f,
        gHikariRender2DMaskRadius,
        distanceFromCenter);
    return color;
}

HikariRender2DLinePixelInput LineVS(
    HikariRender2DLineVertexInput input)
{
    HikariRender2DLinePixelInput output;
    output.position = HikariRender2DPosition(input.position);
    output.color = float4(input.color.rgb * gHikariRender2DColorScale, input.color.a);
    return output;
}

float4 LinePS(HikariRender2DLinePixelInput input) : SV_TARGET
{
    return input.color;
}
