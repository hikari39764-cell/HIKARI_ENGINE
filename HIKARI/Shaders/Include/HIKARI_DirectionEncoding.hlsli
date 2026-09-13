#ifndef HIKARI_DIRECTION_ENCODING_INCLUDED
#define HIKARI_DIRECTION_ENCODING_INCLUDED

float2 HikariEncodeOctahedralDirection(float3 normal)
{
    normal /= max(abs(normal.x) + abs(normal.y) + abs(normal.z), 1e-6f);
    float2 encoded = normal.xy;
    if (normal.z < 0.0f)
    {
        encoded =
            (1.0f - abs(encoded.yx)) *
            float2(encoded.x >= 0.0f ? 1.0f : -1.0f,
                   encoded.y >= 0.0f ? 1.0f : -1.0f);
    }
    return encoded * 0.5f + 0.5f;
}

float3 HikariDecodeOctahedralDirection(float2 encoded)
{
    const float2 value = encoded * 2.0f - 1.0f;
    float3 normal =
        float3(value.x, value.y, 1.0f - abs(value.x) - abs(value.y));
    if (normal.z < 0.0f)
    {
        normal.xy =
            (1.0f - abs(normal.yx)) *
            float2(normal.x >= 0.0f ? 1.0f : -1.0f,
                   normal.y >= 0.0f ? 1.0f : -1.0f);
    }
    return normalize(normal);
}

#endif
