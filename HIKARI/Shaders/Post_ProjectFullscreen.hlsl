#include "Project/Fullscreen.hlsli"

HikariProjectFullscreenVertex FullscreenVS(uint vertexId : SV_VertexID) {
    HikariProjectFullscreenVertex output;
    float2 position;
    if (vertexId == 0u) {
        position = float2(-1.0f, -1.0f);
    } else if (vertexId == 1u) {
        position = float2(-1.0f, 3.0f);
    } else {
        position = float2(3.0f, -1.0f);
    }
    output.position = float4(position, 0.0f, 1.0f);
    output.uv = float2(
        (position.x + 1.0f) * 0.5f,
        1.0f - (position.y + 1.0f) * 0.5f);
    return output;
}

float4 CopyPS(HikariProjectFullscreenVertex input) : SV_TARGET {
    float4 color = HikariProjectInput.Sample(HikariProjectLinearClamp, input.uv);
    if (gReadabilityAttenuateCopy != 0u) {
        uint width, height;
        HikariProjectInput.GetDimensions(width, height);
        color *= 1.0f - .9f * HikariScreenReadabilityAfterWarp(input.uv, float2(width, height));
    }
    return color;
}
