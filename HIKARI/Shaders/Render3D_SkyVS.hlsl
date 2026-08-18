cbuffer SkyCB : register(b0)
{
    float4x4 gSkyInverseViewProj;
    float4 gSkyZenithExposure;
    float4 gSkyHorizonPower;
    float4 gSkyGroundYaw;
    float4 gSkyTintMode;
    float4 gSkySunDirectionIntensity;
    float4 gSkySunSizeParams;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

VSOutput main(uint vertexId : SV_VertexID)
{
    VSOutput output;
    float2 ndc;
    if (vertexId == 0u)
    {
        ndc = float2(-1.0f, -1.0f);
    }
    else if (vertexId == 1u)
    {
        ndc = float2(-1.0f, 3.0f);
    }
    else
    {
        ndc = float2(3.0f, -1.0f);
    }

    output.position = float4(ndc, 1.0f, 1.0f);
    output.ndc = ndc;
    return output;
}
