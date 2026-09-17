// Local-space primitives. Both mesh shaders and the vertex fallback call this owner.
bool ParticleIsSphere(uint shape) { return shape == 4u || shape == 7u; }

uint2 ParticlePrimitiveCounts(uint shape, uint segments)
{
    if (shape == 0u || shape == 9u) return uint2(4u, 2u);
    if (shape == 3u) return uint2(24u, 12u);
    if (ParticleIsSphere(shape)) return uint2(63u, 96u);
    if (shape == 8u) return uint2(8u, 4u);
    return uint2((segments + 1u) * 2u, shape == 6u ? segments : segments * 2u);
}

void ParticlePrimitiveVertex(uint shape, uint index, uint segments,
    out float3 position, out float3 normal, out float3 tangent, out float2 uv)
{
    if (shape == 3u || shape == 8u)
    {
        static const float3 normals[6] = {
            float3(1,0,0), float3(-1,0,0), float3(0,1,0),
            float3(0,-1,0), float3(0,0,1), float3(0,0,-1)
        };
        static const float2 corners[4] = {
            float2(-1,-1), float2(-1,1), float2(1,1), float2(1,-1)
        };
        const uint face = index / 4u;
        normal = shape == 3u ? normals[face] : (face == 0u ? float3(0,0,1) : float3(1,0,0));
        tangent = abs(normal.y) > 0.5f ? float3(1,0,0) : normalize(cross(float3(0,1,0), normal));
        const float3 up = cross(normal, tangent);
        const float2 corner = corners[index % 4u];
        position = (tangent * corner.x + up * corner.y + (shape == 3u ? normal : 0.0f.xxx)) * 0.5f;
        uv = float2(corner.x, -corner.y) * 0.5f + 0.5f;
    }
    else if (ParticleIsSphere(shape))
    {
        uv = float2(index % 9u, index / 9u) / float2(8,6);
        const float longitude = uv.x * 6.28318530718f;
        const float latitude = uv.y * (shape == 7u ? 1.57079632679f : 3.14159265359f);
        normal = float3(sin(latitude)*cos(longitude), cos(latitude), sin(latitude)*sin(longitude));
        position = normal * 0.5f;
        tangent = float3(-sin(longitude),0,cos(longitude));
    }
    else
    {
        const float u = float(index / 2u) / float(segments);
        const float height = float(index & 1u);
        const float angle = u * 6.28318530718f;
        const float radius = shape == 6u ? 0.5f * (1.0f - height) : 0.5f;
        position = float3(cos(angle) * radius, height - 0.5f, sin(angle) * radius);
        normal = normalize(float3(cos(angle), shape == 6u ? 0.5f : 0.0f, sin(angle)));
        tangent = float3(-sin(angle), 0, cos(angle));
        uv = float2(u, 1.0f - height);
    }
}

uint3 ParticlePrimitiveTriangle(uint shape, uint primitiveIndex)
{
    if (shape == 0u || shape == 3u || shape == 8u || shape == 9u)
    {
        const uint base = (primitiveIndex / 2u) * 4u;
        return (primitiveIndex & 1u) == 0u ?
            uint3(base, base+1u, base+2u) : uint3(base, base+2u, base+3u);
    }
    if (ParticleIsSphere(shape))
    {
        const uint cell = primitiveIndex / 2u;
        const uint base = (cell / 8u) * 9u + cell % 8u;
        return (primitiveIndex & 1u) == 0u ?
            uint3(base, base+9u, base+10u) : uint3(base, base+10u, base+1u);
    }
    // A cone emits only the non-degenerate triangle of each side cell.
    const uint base = (shape == 6u ? primitiveIndex : primitiveIndex / 2u) * 2u;
    if (shape == 6u) return uint3(base, base+1u, base+2u);
    return (primitiveIndex & 1u) == 0u ?
        uint3(base, base+1u, base+3u) : uint3(base, base+3u, base+2u);
}
