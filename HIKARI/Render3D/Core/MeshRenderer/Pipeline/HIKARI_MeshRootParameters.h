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
// Primary LightProbe SH9 textures -> t0-t8, space2
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
// AS visibility table -> t19 + u0,space8 (AS only; unchanged DWORD budget)
constexpr UINT MeshletOcclusionDepth = 25;
// Mesh-shader deformation palettes -> t0, space3. The skinned VS/PS root signature
// overrides this same slot as the legacy b3 CBV during migration.
constexpr UINT JointPalette = 26;
// Secondary Reflection Probe Prefiltered Cubemap -> t14
constexpr UINT ReflectionProbePrefilteredSecondary = 27;
// Secondary LightProbe SH9 textures -> t0-t8, space4
constexpr UINT LightProbeShSecondary = 28;
// Per-view local light records -> t0, space5
constexpr UINT LocalLights = 29;
// Per-view clustered light headers -> t1, space5
constexpr UINT LocalLightClusterHeaders = 30;
// Per-view compact clustered light indices -> t2, space5
constexpr UINT LocalLightClusterIndices = 31;
// Fixed material-program float4 parameter blocks -> t0, space6
constexpr UINT MaterialProgramParameters = 32;
// Fixed material-program descriptor-index tables -> t1, space6
constexpr UINT MaterialProgramResources = 33;
// Water surface feature records -> t0, space7
constexpr UINT SurfaceFeatureData = 34;
// Sand surface feature records -> t4, space7
constexpr UINT TerrainSurfaceData = 35;
// Sand surface feature heights -> t1, space7
constexpr UINT SurfaceFeatureHeights = 36;
// Current-frame hierarchical depth pyramid -> t2, space7
constexpr UINT ScreenSpaceDepthPyramid = 37;
// Current-frame packed receiver surface -> t3, space7
constexpr UINT ScreenSpacePackedSurface = 38;
// Screen-space trace validity / mip count -> b10
constexpr UINT ScreenSpaceTraceControl = 39;
// Global three-cascade ocean displacement/slope table -> t5-t10, space7
constexpr UINT OceanSpectrumData = 40;
// Ocean spectrum validity / resolution -> b11
constexpr UINT OceanSpectrumControl = 41;

constexpr UINT Count = OceanSpectrumControl + 1;

// Root descriptor は 2 DWORD、descriptor table は 1 DWORD。
// Static/Skinned は JointPalette slot の型だけを置換し、同じ予算を使う。
constexpr UINT RootDescriptorCount = 16u;
constexpr UINT DescriptorTableCount = 21u;
constexpr UINT RootConstantDwordCount = 10u;
constexpr UINT DwordCost = RootDescriptorCount * 2u + DescriptorTableCount + RootConstantDwordCount;
static_assert(Count == 42u, "Update the mesh root-signature budget.");
static_assert(DwordCost == 63u);
static_assert(DwordCost <= 64u);

} // namespace HIKARI::MESHRENDERER::ROOT_PARAM
