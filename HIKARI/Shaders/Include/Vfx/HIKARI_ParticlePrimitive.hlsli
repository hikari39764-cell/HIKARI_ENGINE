// Local-space primitives. Both mesh shaders and the vertex fallback call this owner.
bool ParticleIsSphere(uint shape) { return shape == 4u || shape == 7u; }
// 球/半球の分割数。既定の 32 セグメントで 17×9 の格子になり、以前の固定
// 9×7（63 頂点）より滑らか。上限はメッシュシェーダの 256 図元制限から決まる。
uint ParticleSphereSegments(uint segments)
{
    return clamp(segments, 8u, 16u);
}
// トーラ/カプセルの分割数。メッシュシェーダの 256 図元上限から決まる。
uint ParticleTorusMajorSegments(uint segments)
{
    return clamp(segments / 2u, 8u, 16u);
}
uint ParticleCapsuleSegments(uint segments)
{
    return clamp(segments / 2u, 8u, 12u);
}
// 格子状の図元（球/トーラ/カプセル）が共有する四角形分割。
uint3 ParticleGridQuadTriangle(uint s, uint primitiveIndex)
{
    const uint columns = s + 1u;
    const uint cell = primitiveIndex / 2u;
    const uint base = (cell / s) * columns + cell % s;
    return (primitiveIndex & 1u) == 0u
        ? uint3(base, base + columns, base + columns + 1u)
        : uint3(base, base + columns + 1u, base + 1u);
}

uint2 ParticlePrimitiveCounts(uint shape, uint segments)
{
    if (shape == 0u || shape == 9u) return uint2(4u, 2u);
    if (shape == 3u) return uint2(24u, 12u);
    if (ParticleIsSphere(shape))
    {
        const uint s = ParticleSphereSegments(segments);
        const uint columns = s + 1u;
        const uint rows = s / 2u + 1u;
        return uint2(columns * rows, (columns - 1u) * (rows - 1u) * 2u);
    }
    if (shape == 10u)
    {
        const uint s = ParticleTorusMajorSegments(segments);
        const uint t = max(s / 2u, 3u);
        return uint2((s + 1u) * (t + 1u), s * t * 2u);
    }
    if (shape == 11u)
    {
        const uint s = ParticleCapsuleSegments(segments);
        const uint rows = 10u;
        return uint2((s + 1u) * (rows + 1u), s * rows * 2u);
    }
    if (shape == 8u) return uint2(8u, 4u);
    if (shape == 5u || shape == 6u)
    {
        const uint s = clamp(segments, 8u, 32u);
        const uint rows = 128u / s;
        return uint2((s + 1u) * (rows + 1u), s * (rows * 2u - (shape == 6u ? 1u : 0u)));
    }
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
        const uint s = ParticleSphereSegments(segments);
        const uint columns = s + 1u;
        const uint rows = s / 2u + 1u;
        uv = float2(float(index % columns), float(index / columns)) /
            float2(float(s), float(rows - 1u));
        const float longitude = uv.x * 6.28318530718f;
        const float latitude = uv.y * (shape == 7u ? 1.57079632679f : 3.14159265359f);
        normal = float3(sin(latitude)*cos(longitude), cos(latitude), sin(latitude)*sin(longitude));
        position = normal * 0.5f;
        tangent = float3(-sin(longitude),0,cos(longitude));
    }
    else if (shape == 10u)
    {
        // トーラ: u は主円周、v は管の断面周。
        const uint s = ParticleTorusMajorSegments(segments);
        const uint t = max(s / 2u, 3u);
        const uint columns = s + 1u;
        uv = float2(float(index % columns), float(index / columns)) /
            float2(float(s), float(t));
        const float majorAngle = uv.x * 6.28318530718f;
        const float minorAngle = uv.y * 6.28318530718f;
        const float majorRadius = max(gShapeParam3, 1.0e-4f);
        const float minorRadius = max(gShapeParam4, 1.0e-4f);
        const float ringRadius = majorRadius + minorRadius * cos(minorAngle);
        position = float3(
            cos(majorAngle) * ringRadius,
            minorRadius * sin(minorAngle),
            sin(majorAngle) * ringRadius);
        normal = normalize(float3(
            cos(minorAngle) * cos(majorAngle),
            sin(minorAngle),
            cos(minorAngle) * sin(majorAngle)));
        tangent = float3(-sin(majorAngle), 0.0f, cos(majorAngle));
    }
    else if (shape == 11u)
    {
        // カプセル: 半径一定の円柱の両端を半球で閉じる。全高は 1。
        const uint s = ParticleCapsuleSegments(segments);
        const uint rows = 10u;
        const uint columns = s + 1u;
        uv = float2(float(index % columns), float(index / columns)) /
            float2(float(s), float(rows));
        const float longitude = uv.x * 6.28318530718f;
        const float radius = clamp(gShapeParam5, 0.01f, 0.5f);
        const float capBase = 0.5f - radius;
        const float y = uv.y - 0.5f;
        float profileRadius = radius;
        float profileY = y;
        if (y > capBase)
        {
            const float angle =
                (y - capBase) / max(radius, 1.0e-4f) * 1.57079632679f;
            profileRadius = radius * cos(angle);
            profileY = capBase + radius * sin(angle);
        }
        else if (y < -capBase)
        {
            const float angle =
                (-y - capBase) / max(radius, 1.0e-4f) * 1.57079632679f;
            profileRadius = radius * cos(angle);
            profileY = -capBase - radius * sin(angle);
        }
        position = float3(
            cos(longitude) * profileRadius,
            profileY,
            sin(longitude) * profileRadius);
        // 円柱部は半径方向、キャップは球面法線。中心線からの差で表せる。
        normal = normalize(float3(
            cos(longitude) * profileRadius,
            profileY - clamp(y, -capBase, capBase),
            sin(longitude) * profileRadius));
        tangent = float3(-sin(longitude), 0.0f, cos(longitude));
    }
    else
    {
        // 軸方向も分割し、ベンド/テーパーを両端だけの変形にしない。
        const uint s = clamp(segments, 8u, 32u);
        const uint rows = 128u / s;
        const float u = float(index % (s + 1u)) / float(s);
        const float height = float(index / (s + 1u)) / float(rows);
        const float angle = u * 6.28318530718f;
        // 円柱は上下端で半径を変えられる。円錐は底半径から頂点へ絞る。
        const float bottomRadius = shape == 6u
            ? asfloat(gShapeParam2)
            : asfloat(gShapeParam1);
        const float topRadius = shape == 6u ? 0.0f : asfloat(gShapeParam0);
        const float radius = lerp(bottomRadius, topRadius, height);
        position = float3(cos(angle) * radius, height - 0.5f, sin(angle) * radius);
        // 側面の法線は半径の傾きだけ傾く。等半径なら従来どおり水平。
        normal = normalize(float3(
            cos(angle), bottomRadius - topRadius, sin(angle)));
        tangent = float3(-sin(angle), 0, cos(angle));
        uv = float2(u, 1.0f - height);
    }
}

// パラメータUVの U/V が実寸でどれだけの長さに対応するか。
// WorldLength 契約はこの値で U を物理長へ換算する。
float2 ParticlePrimitiveUvExtent(uint shape, float3 scale)
{
    if (shape == 3u || shape == 8u)
        return float2(abs(scale.x), abs(scale.y));
    if (ParticleIsSphere(shape))
        return float2(
            3.14159265359f * 0.5f * (abs(scale.x) + abs(scale.z)),
            (shape == 7u ? 0.78539816339f : 1.57079632679f) * abs(scale.y));
    if (shape == 10u)
        return float2(
            6.28318530718f * max(gShapeParam3, 1.0e-4f) *
                0.5f * (abs(scale.x) + abs(scale.z)),
            6.28318530718f * max(gShapeParam4, 1.0e-4f) * abs(scale.y));
    if (shape == 11u)
        return float2(
            6.28318530718f * clamp(gShapeParam5, 0.01f, 0.5f) *
                0.5f * (abs(scale.x) + abs(scale.z)),
            abs(scale.y));
    // 円柱/円錐: u は周方向（周長 = 2π × 平均半径）、v は軸方向。
    const float averageRadius = shape == 6u
        ? asfloat(gShapeParam2) * 0.5f
        : 0.5f * (asfloat(gShapeParam0) + asfloat(gShapeParam1));
    return float2(
        3.14159265359f * averageRadius * (abs(scale.x) + abs(scale.z)),
        abs(scale.y));
}

uint3 ParticlePrimitiveTriangle(uint shape, uint primitiveIndex, uint segments)
{
    if (shape == 0u || shape == 3u || shape == 8u || shape == 9u)
    {
        const uint base = (primitiveIndex / 2u) * 4u;
        return (primitiveIndex & 1u) == 0u ?
            uint3(base, base+1u, base+2u) : uint3(base, base+2u, base+3u);
    }
    if (ParticleIsSphere(shape))
    {
        const uint s = ParticleSphereSegments(segments);
        const uint columns = s + 1u;
        const uint cell = primitiveIndex / 2u;
        const uint base = (cell / s) * columns + cell % s;
        return (primitiveIndex & 1u) == 0u ?
            uint3(base, base+columns, base+columns+1u) :
            uint3(base, base+columns+1u, base+1u);
    }
    if (shape == 10u)
        return ParticleGridQuadTriangle(
            ParticleTorusMajorSegments(segments), primitiveIndex);
    if (shape == 11u)
        return ParticleGridQuadTriangle(
            ParticleCapsuleSegments(segments), primitiveIndex);
    if (shape == 5u || shape == 6u)
    {
        const uint s = clamp(segments, 8u, 32u);
        const uint rows = 128u / s;
        const uint bodyTriangles = s * (rows - 1u) * 2u;
        if (shape == 6u && primitiveIndex >= bodyTriangles)
        {
            const uint base = (rows - 1u) * (s + 1u) + primitiveIndex - bodyTriangles;
            return uint3(base, base + s + 1u, base + 1u);
        }
        return ParticleGridQuadTriangle(s, primitiveIndex);
    }
    const uint base = (primitiveIndex / 2u) * 2u;
    return (primitiveIndex & 1u) == 0u ?
        uint3(base, base+1u, base+3u) : uint3(base, base+3u, base+2u);
}
