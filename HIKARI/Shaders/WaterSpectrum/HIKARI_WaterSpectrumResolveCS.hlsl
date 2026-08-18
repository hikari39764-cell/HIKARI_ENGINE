#include "WaterSpectrum/HIKARI_WaterSpectrumCommon.hlsli"

cbuffer WaterSpectrumResolveParams : register(b0)
{
    uint gResolution;
    float gInverseNormalization;
    float gGridStep;
    float gUnused0;
    float gUnused1;
    float gUnused2;
    uint gUnused3;
    uint gUnused4;
};

Texture2D<float4> gField0 : register(t0);
Texture2D<float4> gField1 : register(t1);
RWTexture2D<float4> gDisplacement : register(u0);
RWTexture2D<float4> gSlope : register(u1);

float4 HikariLoadField0(int2 coord)
{
    return gField0[HikariWrapCoord(coord, gResolution)];
}

float4 HikariLoadField1(int2 coord)
{
    return gField1[HikariWrapCoord(coord, gResolution)];
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= gResolution || dispatchThreadId.y >= gResolution)
    {
        return;
    }

    const int2 coord = (int2)dispatchThreadId.xy;
    const float inverseNormalization = max(
        HikariWaterSpectrumFiniteOr(gInverseNormalization, 0.0),
        0.0);
    const float gridStep = max(
        HikariWaterSpectrumFiniteOr(gGridStep, 1.0),
        HIKARI_WATER_EPSILON);
    const float4 field0 = HikariWaterSpectrumFiniteOr(
        HikariLoadField0(coord) * inverseNormalization,
        0.0.xxxx);
    const float4 field1 = HikariWaterSpectrumFiniteOr(
        HikariLoadField1(coord) * inverseNormalization,
        0.0.xxxx);
    const float height = field0.z;
    const float displacementX = field0.x;
    const float displacementZ = field1.x;
    const float slopeX = field1.z;

    const float heightLeft = HikariWaterSpectrumFiniteOr(HikariLoadField0(coord + int2(-1, 0)).z * inverseNormalization, height);
    const float heightRight = HikariWaterSpectrumFiniteOr(HikariLoadField0(coord + int2(1, 0)).z * inverseNormalization, height);
    const float heightDown = HikariWaterSpectrumFiniteOr(HikariLoadField0(coord + int2(0, -1)).z * inverseNormalization, height);
    const float heightUp = HikariWaterSpectrumFiniteOr(HikariLoadField0(coord + int2(0, 1)).z * inverseNormalization, height);
    const float displacementXLeft = HikariWaterSpectrumFiniteOr(HikariLoadField0(coord + int2(-1, 0)).x * inverseNormalization, displacementX);
    const float displacementXRight = HikariWaterSpectrumFiniteOr(HikariLoadField0(coord + int2(1, 0)).x * inverseNormalization, displacementX);
    const float displacementXDown = HikariWaterSpectrumFiniteOr(HikariLoadField0(coord + int2(0, -1)).x * inverseNormalization, displacementX);
    const float displacementXUp = HikariWaterSpectrumFiniteOr(HikariLoadField0(coord + int2(0, 1)).x * inverseNormalization, displacementX);
    const float displacementZLeft = HikariWaterSpectrumFiniteOr(HikariLoadField1(coord + int2(-1, 0)).x * inverseNormalization, displacementZ);
    const float displacementZRight = HikariWaterSpectrumFiniteOr(HikariLoadField1(coord + int2(1, 0)).x * inverseNormalization, displacementZ);
    const float displacementZDown = HikariWaterSpectrumFiniteOr(HikariLoadField1(coord + int2(0, -1)).x * inverseNormalization, displacementZ);
    const float displacementZUp = HikariWaterSpectrumFiniteOr(HikariLoadField1(coord + int2(0, 1)).x * inverseNormalization, displacementZ);

    const float slopeZ = (heightUp - heightDown) / (2.0 * gridStep);
    const float dXdx = (displacementXRight - displacementXLeft) /
        (2.0 * gridStep);
    const float dZdz = (displacementZUp - displacementZDown) /
        (2.0 * gridStep);
    const float dXdz = (displacementXUp - displacementXDown) /
        (2.0 * gridStep);
    const float dZdx = (displacementZRight - displacementZLeft) /
        (2.0 * gridStep);
    const float jacobian = (1.0 + dXdx) * (1.0 + dZdz) - dXdz * dZdx;

    gDisplacement[dispatchThreadId.xy] =
        float4(clamp(
            HikariWaterSpectrumFiniteOr(
                float3(displacementX, height, displacementZ),
                0.0.xxx),
            -32.0.xxx,
            32.0.xxx), 1.0);
    gSlope[dispatchThreadId.xy] = float4(
        clamp(
            HikariWaterSpectrumFiniteOr(float2(slopeX, slopeZ), 0.0.xx),
            -8.0.xx,
            8.0.xx),
        clamp(HikariWaterSpectrumFiniteOr(jacobian, 1.0), -4.0, 4.0),
        1.0);
}
