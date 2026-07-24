#ifndef HIKARI_GPU_DRIVEN_SKINNING_INCLUDED
#define HIKARI_GPU_DRIVEN_SKINNING_INCLUDED

// One upload-buffer arena is shared by all mesh-shader passes. Each GPU-scene
// instance owns a byte offset/count, so a single DispatchMesh can animate many
// independent skeletons without per-draw root-CBV changes.
ByteAddressBuffer gGpuDeformationPalettes : register(t0, space3);

float4x4 HikariLoadGpuDeformationMatrix(uint byteOffset)
{
    float4 c0 = asfloat(gGpuDeformationPalettes.Load4(byteOffset + 0u));
    float4 c1 = asfloat(gGpuDeformationPalettes.Load4(byteOffset + 16u));
    float4 c2 = asfloat(gGpuDeformationPalettes.Load4(byteOffset + 32u));
    float4 c3 = asfloat(gGpuDeformationPalettes.Load4(byteOffset + 48u));
    return float4x4(
        c0.x, c1.x, c2.x, c3.x,
        c0.y, c1.y, c2.y, c3.y,
        c0.z, c1.z, c2.z, c3.z,
        c0.w, c1.w, c2.w, c3.w);
}

void HikariApplyGpuDrivenSkinning(
    ByteAddressBuffer geometry,
    HikariSurfaceGpuSceneInstance instance,
    uint vertexIndex,
    inout float3 localPosition,
    inout float3 localNormal,
    inout float3 localTangent)
{
    if ((instance.flags & HIKARI_SURFACE_GPU_SCENE_FLAG_SKINNED) == 0u ||
        instance.jointPaletteMatrixCount == 0u)
    {
        return;
    }

    HikariClusterGeometryHeader geometryHeader =
        HikariLoadClusterGeometryHeader(geometry);
    if (!HikariIsValidClusterGeometryHeader(geometryHeader) ||
        vertexIndex >= geometryHeader.skinVertexCount)
    {
        return;
    }

    HikariClusterSkinVertex skin =
        HikariLoadClusterSkinVertex(geometry, geometryHeader, vertexIndex);
    float4 position = 0.0f;
    float3 normal = 0.0f;
    float3 tangent = 0.0f;
    float appliedWeight = 0.0f;

    [unroll]
    for (uint influence = 0u; influence < 4u; ++influence)
    {
        const uint jointIndex = skin.joints[influence];
        const float weight = skin.weights[influence];
        if (weight <= 0.0f || jointIndex >= instance.jointPaletteMatrixCount)
        {
            continue;
        }

        float4x4 jointMatrix = HikariLoadGpuDeformationMatrix(
            instance.jointPaletteOffsetBytes + jointIndex * 64u);
        position += mul(jointMatrix, float4(localPosition, 1.0f)) * weight;
        normal += mul((float3x3)jointMatrix, localNormal) * weight;
        tangent += mul((float3x3)jointMatrix, localTangent) * weight;
        appliedWeight += weight;
    }

    if (appliedWeight > 1.0e-6f)
    {
        localPosition = position.xyz / appliedWeight;
        localNormal = HikariNormalizeOrDefault(normal, localNormal);
        localTangent = HikariNormalizeOrDefault(tangent, localTangent);
    }
}

#endif
