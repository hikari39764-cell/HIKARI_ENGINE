#pragma once

#include <d3d12.h>

namespace HIKARI::MESHRENDERER::ROOT_PARAM {

    // Mesh graphics root parameter indices.
    // Camera -> b0
    constexpr UINT Camera = 0;
    // GPU-driven culling camera -> b9
    constexpr UINT CullingCamera = 1;
    // Object -> b1
    constexpr UINT Object = 2;
    // Light -> b2
    constexpr UINT Light = 3;
    // ShadowMap -> t2
    constexpr UINT ShadowMap = 4;
    // ShadowCB -> b4
    constexpr UINT ShadowCB = 5;
    // SkyEnvironment -> b5
    constexpr UINT SkyEnvironment = 6;
    // SkyCube -> t6
    constexpr UINT SkyCube = 7;
    // SceneDepth -> t7
    constexpr UINT SceneDepth = 8;
    // SceneColor -> t8
    constexpr UINT SceneColor = 9;
    // IBL Irradiance Cubemap -> t9
    constexpr UINT IblIrradiance = 10;
    // IBL Prefiltered Cubemap -> t10
    constexpr UINT IblPrefiltered = 11;
    // IBL BRDF LUT -> t11
    constexpr UINT IblBrdfLut = 12;
    // Reflection Probe Prefiltered Cubemap -> t12
    constexpr UINT ReflectionProbePrefiltered = 13;
    // SSAO texture -> t13
    constexpr UINT Ssao = 14;
    // LightProbe SH buffer -> t14
    constexpr UINT LightProbeSh = 15;
    // Mesh object data buffer -> t15
    constexpr UINT ObjectData = 16;
    // Mesh object data index -> b6
    constexpr UINT ObjectIndex = 17;
    // Mesh material data buffer -> t16
    constexpr UINT MaterialData = 18;
    // Mesh material data index -> b7
    constexpr UINT MaterialIndex = 19;
    // 材質テクスチャプール -> t20[]
    constexpr UINT TexturePool = 20;
    // Surface GPU scene buffer -> t17
    constexpr UINT SurfaceGpuScene = 21;
    // Surface GPU scene base index / mode -> b8
    constexpr UINT SurfaceGpuSceneControl = 22;
    // Cluster geometry resource pool -> t0[], space1
    constexpr UINT ClusterGeometryPool = 23;
    // Meshlet visible range buffer -> t18
    constexpr UINT MeshletVisibleRanges = 24;
    // Meshlet visible cluster list buffer -> t19
    constexpr UINT MeshletVisibleClusterList = 25;
    // Mesh-shader deformation palettes -> t0, space3. The skinned VS/PS root signature
    // overrides this same slot as the legacy b3 CBV during migration.
    constexpr UINT JointPalette = 26;

    constexpr UINT Count = JointPalette + 1;

} // namespace HIKARI::MESHRENDERER::ROOT_PARAM
