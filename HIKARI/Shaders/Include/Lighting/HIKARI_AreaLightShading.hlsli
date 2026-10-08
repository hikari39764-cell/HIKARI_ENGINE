#ifndef HIKARI_AREA_LIGHT_SHADING_INCLUDED
#define HIKARI_AREA_LIGHT_SHADING_INCLUDED

// 面光源（管と長方形）の代表点。局所光の標本（方向・距離・減衰）を点光源と同じ形で返し、受け手の BRDF は
// 変えない。光度 [cd] は発光体全体の値（点光源と同じ換算）で、近くでは発光体の広がりで逆二乗を和らげる。
//   管: 線分に沿って逆二乗を積分した照度（無限に短ければ点光源に一致）。方向は線分上の最も近い点、
//       鏡面は反射方向に最も近い線分上の点（Karis 2013）。
//   長方形: 片面の Lambert 発光体。方向は長方形上の最も近い点、減衰は面積で頭打ちにした逆二乗に、
//       発光面の向き（中心からの方向との cos）を掛ける。鏡面は反射方向と面の交点を長方形へ収めた点。

// 線分 [start, start + axis * length] の、点 p に最も近い点のパラメーター（0..length）。
float HikariClosestOnSegment(float3 start, float3 axis, float segmentLength, float3 position)
{
    return clamp(dot(position - start, axis), 0.0f, segmentLength);
}

// 線分の照度係数（光度 1 cd の線光源、受け手の cos は含めない）。∫dl/(h²+l²) / L。
float HikariTubeIrradianceFactor(float3 start, float3 axis, float segmentLength, float3 position, float radius)
{
    const float3 fromStart = position - start;
    const float along = dot(fromStart, axis);
    const float height = max(length(fromStart - axis * along), max(radius, 1.0e-3f));
    const float t0 = (0.0f - along) / height;
    const float t1 = (segmentLength - along) / height;
    return (atan(t1) - atan(t0)) / (max(segmentLength, 1.0e-4f) * height);
}

// 反射方向 reflection の光線に最も近い線分上の点（Karis 2013 の鏡面の代表点）。
float3 HikariTubeSpecularPoint(float3 start, float3 axis, float segmentLength, float3 position, float3 reflection)
{
    const float3 toStart = start - position;
    const float axisDotReflection = dot(axis, reflection);
    const float denominator = max(1.0f - axisDotReflection * axisDotReflection, 1.0e-4f);
    const float t = (dot(reflection, toStart) * axisDotReflection - dot(axis, toStart)) / denominator;
    return start + axis * clamp(t, 0.0f, segmentLength);
}

// 長方形（中心 center、法線 normal、接線 tangent、半幅 halfWidth/halfHeight）上の p に最も近い点。
float3 HikariClosestOnRect(float3 center, float3 normal, float3 tangent, float halfWidth, float halfHeight,
    float3 position)
{
    const float3 bitangent = cross(normal, tangent);
    const float3 offset = position - center;
    return center + tangent * clamp(dot(offset, tangent), -halfWidth, halfWidth) +
        bitangent * clamp(dot(offset, bitangent), -halfHeight, halfHeight);
}

// 反射方向の光線と発光面の交点を長方形へ収めた点。面の裏や平行なら最も近い点。
float3 HikariRectSpecularPoint(float3 center, float3 normal, float3 tangent, float halfWidth, float halfHeight,
    float3 position, float3 reflection)
{
    const float denominator = dot(reflection, normal);
    if (denominator > -1.0e-4f)
        return HikariClosestOnRect(center, normal, tangent, halfWidth, halfHeight, position);
    const float t = dot(center - position, normal) / denominator;
    const float3 hit = position + reflection * max(t, 0.0f);
    return HikariClosestOnRect(center, normal, tangent, halfWidth, halfHeight, hit);
}

#endif
