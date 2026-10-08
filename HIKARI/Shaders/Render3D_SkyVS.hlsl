cbuffer SkyCB : register(b0)
{
    float4x4 gSkyInverseViewProj;
    float4 gSkyScaleYaw;
    float4 gSkyTintMode;
    float4 gSkyDisk;
    float4 gSkyDiskLuminance;
    float4 gSkyMoonPhase;           // xyz toward sun, w primary disk is the moon
    float4 gSkyStars;               // x star intensity, y limiting magnitude, z night background [nits], w time [s]
    float4 gSkyCelestialX;          // celestial -> world columns
    float4 gSkyCelestialY;
    float4 gSkyCelestialZ;
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
