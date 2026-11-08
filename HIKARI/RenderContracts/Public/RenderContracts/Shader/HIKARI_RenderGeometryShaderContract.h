#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "RenderContracts/Shader/HIKARI_ShaderContract.h"
#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::ASSETS::SHADER {

inline constexpr std::string_view kRenderGeometryEngineInterfaceId = "HIKARI.RenderGeometry";
inline constexpr uint32_t kRenderGeometryEngineInterfaceVersion = 2u;
inline constexpr std::string_view kRenderGeometryEngineInterfaceHash =
    "8e88b019f3846a824173fdb81feefac56a8175880c670fe4a53f817f7fd40a98";
inline constexpr std::string_view kRenderGeometryInterfaceDependency =
    "EngineSdk/Include/Render3D/HIKARI_RenderGeometryInterface.hlsli";

enum class RenderGeometryRootLayout : uint8_t {
    MeshGraphics = 0u,
    TraditionalCommand,
    ClusterCulling,
    ParticleSimulation,
    ParticleCompaction,
    ParticleDraw,
    WorldSpriteDraw,
    WorldTextDraw,
    ShadowGraphics,
    ClusterRayTracing,
    ModuleSurfaceDraw,
};

// モジュールの面（HIKARI/Project/RenderSurface.h）。MeshGraphics と同じ番号で三つの型だけ違う。
inline constexpr std::string_view kModuleSurfaceDrawRootLayoutId = "HIKARI.RenderGeometry.ModuleSurfaceDraw.Root";
inline constexpr uint32_t kModuleSurfaceDrawRootLayoutVersion = 1u;
inline constexpr std::string_view kModuleSurfaceDrawRootLayoutHash =
    "48c72838cb3b50e8727ad0c61579c6731b5d95906d278e4d1cb0f89a3a03b7db";
inline constexpr std::string_view kModuleSurfaceDrawRootLayoutCanonical =
    "HIKARI.RenderGeometry.ModuleSurfaceDraw.Root.v1|MeshGraphicsSlots|"
    "JointPalette(SRV(t0,space10))|MeshletVisibleRanges(CBV(b3))|"
    "ClusterGeometryPool(Table(t1,space10,count4))|Pipeline(Graphics)|Flags(AllowInputAssembler)";

inline constexpr std::string_view kClusterRayTracingRootLayoutId = "HIKARI.RenderGeometry.ClusterRayTracing.Root";
inline constexpr uint32_t kClusterRayTracingRootLayoutVersion = 1u;
inline constexpr std::string_view kClusterRayTracingRootLayoutHash =
    "6d1ca3b9897f9c559932321f5383aaa2df1932d7ade4269a9ffd2e539ba65750";
inline constexpr std::string_view kClusterRayTracingRootLayoutCanonical =
    "HIKARI.RenderGeometry.ClusterRayTracing.Root.v1|"
    "0:SRV(t0,space0,All)|1:SRV(t1,space0,All)|"
    "2:UAV(u0,space0,All)|3:Constants(b0,space0,count4,All)|"
    "Pipeline(Compute)|Flags(None)";

inline constexpr std::string_view kMeshGraphicsRootLayoutId = "HIKARI.RenderGeometry.MeshGraphics.Root";
inline constexpr uint32_t kMeshGraphicsRootLayoutVersion = 8u;
inline constexpr std::string_view kMeshGraphicsRootLayoutHash =
    "3af4fa606c409010d72d7ff96b1ce51043f95b16fb3cd4a27c4a0b71682367d3";
inline constexpr std::string_view kMeshGraphicsRootLayoutCanonical =
    "HIKARI.RenderGeometry.MeshGraphics.Root.v8|MeshletDispatchControl(Base,Pass,Bucket,RangeCount)|Grid(MaxX65535)|"
    "RootParameters(43)|DwordCost(64)|ASVisibility(Table(t19,u0space8,b12),QueryBytes(80,viewProj+control))|VisibleRangeBytes(80)|PayloadBytes(832)|"
    "RayTracedShadow(Table(t11,space7))|"
    "JointPaletteVariant(StaticSRV(t0,space3),SkinnedCBV(b3,space0))|"
    "SurfaceControlAlias(b8,TraditionalSurfaceGpuScene(Base,Enabled,ShellCount,ShellLodBits),MeshletDrawControl)|"
    "ClusterGeometryTable(SRV(t0,space1,count1024))|"
    "StaticSamplers(s0,s1,s2,s3,s4)|"
    "Pipeline(Graphics,Amplification,Mesh)|Flags(AllowInputAssembler)";

inline constexpr std::string_view kTraditionalCommandRootLayoutId = "HIKARI.RenderGeometry.TraditionalCommand.Root";
inline constexpr uint32_t kTraditionalCommandRootLayoutVersion = 2u;
inline constexpr std::string_view kTraditionalCommandRootLayoutHash =
    "9be672b776faa4a182cd3b769dc5f84fb64590207f02f810f90d37b6b33714bb";
inline constexpr std::string_view kTraditionalCommandRootLayoutCanonical =
    "HIKARI.RenderGeometry.TraditionalCommand.Root.v2|SeedBytes(64)|CullConstantsBytes(160)|"
    "0:CBV(b0,space0,All)|1:SRV(t0,space0,All)|"
    "2:UAV(u0,space0,All)|3:SRV(t1,space0,All)|"
    "4:UAV(u1,space0,All)|5:UAV(u2,space0,All)|"
    "Pipeline(Compute)|Flags(None)";

inline constexpr std::string_view kClusterCullingRootLayoutId = "HIKARI.RenderGeometry.ClusterCulling.Root";
inline constexpr uint32_t kClusterCullingRootLayoutVersion = 4u;
inline constexpr std::string_view kClusterCullingRootLayoutHash =
    "1fe0139728af1f8b1ec1264854b389d779e1d6a3eae7c1af8eef412b04b60d60";
inline constexpr std::string_view kClusterCullingRootLayoutCanonical =
    "HIKARI.RenderGeometry.ClusterCulling.Root.v4|MeshletDispatchControl(Base,Pass,Bucket,RangeCount)|Grid(MaxX65535)|"
    "0:CBV(b0,space0,All)|1:SRV(t17,space0,All)|"
    "2:UAV(u0,space0,All)|3:UAV(u1,space0,All)|"
    "4:Table(SRV(t0,space1,count1024),All)|"
    "5:UAV(u3,space0,All)|6:UAV(u4,space0,All)|"
    "7:UAV(u5,space0,All)|8:Table(SRV(t18,space0,count1),All)|"
    "9:UAV(u6,space0,All)|"
    "10:SRV(t0,space8,All)|MeshletDispatchSlots(PassBucket)|Pipeline(Compute)|Flags(None)";

inline constexpr std::string_view kParticleSimulationRootLayoutId = "HIKARI.RenderGeometry.ParticleSimulation.Root";
inline constexpr uint32_t kParticleSimulationRootLayoutVersion = 5u;
inline constexpr std::string_view kParticleSimulationRootLayoutHash =
    "a18f3782d8771da18d57fa192f2f20e706681c714641496c92545084a08b56cd";
inline constexpr std::string_view kParticleSimulationRootLayoutCanonical =
    "HIKARI.RenderGeometry.ParticleSimulation.Root.v5|"
    "0:Table(UAV(u0,space0,count1),All)|1:SRV(t0,space0,All)|"
    "2:Constants(b0,space0,count8,All)|3:Table(SRV(t1,space0,count1),All)|4:Table(SRV(t2,space0,count1),All)|"
    "5:Table(SRV(t3,space0,count1),All)|6:Table(SRV(t4,space0,count1),All)|7:CBV(b1,space0,All)|"
    "8:Table(SRV(t5,space0,count1),All)|9:CBV(b2,space0,All)|10:Table(UAV(u1,space0,count1),All)|"
    "11:UAV(u2,space0,All)|12:UAV(u3,space0,All)|13:UAV(u4,space0,All)|14:UAV(u5,space0,All)|15:UAV(u6,space0,All)|"
    "StaticSampler(s0,LinearClamp,All)|Pipeline(Compute)|Flags(None)";

inline constexpr std::string_view kParticleCompactionRootLayoutId = "HIKARI.RenderGeometry.ParticleCompaction.Root";
inline constexpr uint32_t kParticleCompactionRootLayoutVersion = 2u;
inline constexpr std::string_view kParticleCompactionRootLayoutHash =
    "b721e743343700916a7db6b60e59422b67d0e6374510bde1deb51e3105a1acfb";
inline constexpr std::string_view kParticleCompactionRootLayoutCanonical =
    "HIKARI.RenderGeometry.ParticleCompaction.Root.v2|"
    "0:Table(UAV(u0,space0,count1),All)|1:UAV(u1,space0,All)|"
    "2:UAV(u2,space0,All)|3:UAV(u3,space0,All)|"
    "4:Constants(b1,space0,count5,All)|Pipeline(Compute)|Flags(None)";

inline constexpr std::string_view kParticleDrawRootLayoutId = "HIKARI.RenderGeometry.ParticleDraw.Root";
inline constexpr uint32_t kParticleDrawRootLayoutVersion = 6u;
inline constexpr std::string_view kParticleDrawRootLayoutHash =
    "8f7c7143b75f14d13098cf0be479a864a9eecea82d9a60a9b6e1c307ff70992f";
inline constexpr std::string_view kParticleDrawRootLayoutCanonical =
    "HIKARI.RenderGeometry.ParticleDraw.Root.v6|"
    "0:CBV(b0,space0,All)|"
    "1:SRV(t1,space0,All)|"
    "2:SRV(t2,space0,All)|"
    "3:SRV(t16,space0,Pixel)|"
    "4:SRV(t0,space6,Pixel)|"
    "5:SRV(t1,space6,Pixel)|"
    "6:Table(SRV(t20,space0,count3968),Pixel)|"
    "7:Constants(b1,space0,count16,All)|"
    "8:Constants(b7,space0,count1,Pixel)|"
    "9:Table(SRV(t3,space0,count1),Pixel)|"
    "10:CBV(b2,space0,Pixel)|"
    "11:CBV(b4,space0,Pixel)|"
    "12:CBV(b5,space0,Pixel)|"
    "13:SRV(t0,space5,Pixel)|"
    "14:SRV(t1,space5,Pixel)|"
    "15:SRV(t2,space5,Pixel)|"
    "16:SRV(t4,space0,All)|"
    "17:Table(SRV(t0,space9,count1),Pixel)|"
    "18:Table(SRV(t9,space0,count1),Pixel)|"
    "19:Table(SRV(t10,space0,count1),Pixel)|"
    "20:SRV(t5,space0,All)|"
    "21:Table(SRV(t11,space0,count1),Pixel)|"
    "22:Constants(b8,space0,count4,Pixel)|"
    "StaticSampler(s0,Anisotropic8Clamp,Pixel)|"
    "StaticSampler(s1,PointClamp,Pixel)|"
    "StaticSampler(s2,ComparisonLinearClampLessEqual,Pixel)|"
    "Pipeline(Amplification,Mesh,Vertex,Pixel)|"
    "Flags(None)";

inline constexpr std::string_view kWorldSpriteDrawRootLayoutId = "HIKARI.RenderGeometry.WorldSpriteDraw.Root";
inline constexpr uint32_t kWorldSpriteDrawRootLayoutVersion = 5u;
inline constexpr std::string_view kWorldSpriteDrawRootLayoutHash =
    "28b3180ebac7fb772905401be74bd0fed4dc99633fe26ce1b89d8f2e18801f8b";
inline constexpr std::string_view kWorldSpriteDrawRootLayoutCanonical =
    "HIKARI.RenderGeometry.WorldSpriteDraw.Root.v5|0:CBV(b0,space0,All)|1:SRV(t1,space0,All)|2:Table(SRV(t20,space0,count3968),Pixel)|3:Constants(b1,space0,count5,All)|4:CBV(b2,Pixel)|5:CBV(b4,Pixel)|6:CBV(b5,Pixel)|7:SRV(t0,space5,Pixel)|8:SRV(t1,space5,Pixel)|9:SRV(t2,space5,Pixel)|10:Table(SRV(t0,space9),Pixel)|11:Table(SRV(t9),Pixel)|12:Table(SRV(t10),Pixel)|13:Constants(b3,space0,count16,All)|14:SRV(t2,space0,Pixel)|WorldSpriteInstance(Stride144,NormalTextureDescriptorIndex,SourceCanvasRect)|SpriteParameters(StructuredBufferFloat4,16Slots,InstanceReserved0BlockIndex)|PositionW(CastShadowBit0,ReceiveShadowBit1,RotatedAtlasBit2,FlipXBit3)|StaticSampler(s0,LinearClamp,Pixel)|StaticSampler(s2,ShadowCompare,Pixel)|Pipeline(MeshOrVertex,Pixel)|Flags(None)";

inline constexpr std::string_view kWorldTextDrawRootLayoutId = "HIKARI.RenderGeometry.WorldTextDraw.Root";
inline constexpr uint32_t kWorldTextDrawRootLayoutVersion = 1u;
inline constexpr std::string_view kWorldTextDrawRootLayoutHash =
    "92eedb3f876b85dc0340b6c23ddfeef1db489d233fc3d9a0f4523edbc3e2b222";
inline constexpr std::string_view kWorldTextDrawRootLayoutCanonical = "HIKARI.RenderGeometry.WorldTextDraw.Root.v1|"
                                                                      "0:CBV(b0,space0,All)|1:SRV(t1,space0,All)|"
                                                                      "2:Table(SRV(t20,space0,count3968),Pixel)|"
                                                                      "3:Constants(b1,space0,count4,Mesh)|"
                                                                      "StaticSampler(s0,LinearClamp,Pixel)|"
                                                                      "Pipeline(Mesh,Pixel)|Flags(None)";

inline constexpr std::string_view kShadowGraphicsRootLayoutId = "HIKARI.RenderGeometry.ShadowGraphics.Root";
inline constexpr uint32_t kShadowGraphicsRootLayoutVersion = 2u;
inline constexpr std::string_view kShadowGraphicsRootLayoutHash =
    "b3a7556a0b35c164f85f551522484be832bc745e970af90d66db03f881f7a88c";
inline constexpr std::string_view kShadowGraphicsRootLayoutCanonical = "HIKARI.RenderGeometry.ShadowGraphics.Root.v2|"
                                                                       "0:CBV(b0,space0,All)|"
                                                                       "1:CBV(b1,space0,All)|"
                                                                       "2:Table(SRV(t0,space0,count1),Pixel)|"
                                                                       "3:Table(SRV(t16,space0,count1),All)|"
                                                                       "4:Table(SRV(t17,space0,count1),All)|"
                                                                       "5:Constants(b8,space0,count4,All)|"
                                                                       "6:Table(SRV(t20,space0,count3968),Pixel)|"
                                                                       "7:Constants(b7,space0,count1,All)|"
                                                                       "8:Table(SRV(t15,space0,count1),All)|"
                                                                       "9:Table(SRV(t0,space1,count1024),All)|"
                                                                       "10:SRV(t18,space0,All)|"
                                                                       "11:CBV(b9,space0,All)|"
                                                                       "12:SRV(t0,space3,All)|"
                                                                       "13:SRV(t4,space7,All)|"
                                                                       "14:SRV(t1,space7,All)|"
                                                                       "15:SRV(t0,space6,All)|"
                                                                       "16:SRV(t1,space6,All)|"
                                                                       "17:CBV(b10,space0,All)|"
                                                                       "SkinnedAppend(18:CBV(b3,space0,All))|"
                                                                       "RootParameters(Static18,Skinned19)|"
                                                                       "DwordCost(Static31,Skinned33)|"
                                                                       "StaticSampler(s0,LinearWrap,Pixel)|"
                                                                       "Pipeline(Graphics,Amplification,Mesh)|"
                                                                       "Flags(AllowInputAssembler)";

inline std::string_view RenderGeometryRootLayoutId(RenderGeometryRootLayout layout) noexcept {
    switch (layout) {
    case RenderGeometryRootLayout::MeshGraphics: return kMeshGraphicsRootLayoutId;
    case RenderGeometryRootLayout::TraditionalCommand: return kTraditionalCommandRootLayoutId;
    case RenderGeometryRootLayout::ClusterCulling: return kClusterCullingRootLayoutId;
    case RenderGeometryRootLayout::ParticleSimulation: return kParticleSimulationRootLayoutId;
    case RenderGeometryRootLayout::ParticleCompaction: return kParticleCompactionRootLayoutId;
    case RenderGeometryRootLayout::ParticleDraw: return kParticleDrawRootLayoutId;
    case RenderGeometryRootLayout::WorldSpriteDraw: return kWorldSpriteDrawRootLayoutId;
    case RenderGeometryRootLayout::WorldTextDraw: return kWorldTextDrawRootLayoutId;
    case RenderGeometryRootLayout::ShadowGraphics: return kShadowGraphicsRootLayoutId;
    case RenderGeometryRootLayout::ClusterRayTracing: return kClusterRayTracingRootLayoutId;
    case RenderGeometryRootLayout::ModuleSurfaceDraw: return kModuleSurfaceDrawRootLayoutId;
    }
    return {};
}

inline bool TryParseRenderGeometryRootLayout(std::string_view id, RenderGeometryRootLayout& outLayout) noexcept {
    for (uint8_t raw = 0u; raw <= static_cast<uint8_t>(RenderGeometryRootLayout::ModuleSurfaceDraw); ++raw) {
        const auto layout = static_cast<RenderGeometryRootLayout>(raw);
        if (RenderGeometryRootLayoutId(layout) == id) {
            outLayout = layout;
            return true;
        }
    }
    return false;
}

inline bool TryResolveRenderGeometryProgramRootLayout(std::string_view programId,
                                                      RenderGeometryRootLayout& outLayout) noexcept {
    constexpr std::string_view prefix = "HIKARI.EngineShader.Render3D_";
    if (programId.rfind(prefix, 0u) != 0u) {
        return false;
    }
    if (programId == "HIKARI.EngineShader.Render3D_ShadowStaticVS" ||
        programId == "HIKARI.EngineShader.Render3D_ShadowSkinnedVS" ||
        programId == "HIKARI.EngineShader.Render3D_ShadowAlphaPS" ||
        programId == "HIKARI.EngineShader.Render3D_MeshletShadowAS" ||
        programId == "HIKARI.EngineShader.Render3D_MeshletShadowMS" ||
        programId == "HIKARI.EngineShader.Render3D_MeshletShadowPS") {
        outLayout = RenderGeometryRootLayout::ShadowGraphics;
        return true;
    }
    if (programId == "HIKARI.EngineShader."
                     "Render3D_GpuTraditionalCommandCompactCS") {
        outLayout = RenderGeometryRootLayout::TraditionalCommand;
        return true;
    }
    if (programId == "HIKARI.EngineShader.Render3D_TerrainKitSurface") {
        outLayout = RenderGeometryRootLayout::ModuleSurfaceDraw;
        return true;
    }
    if (programId == "HIKARI.EngineShader.Render3D_ClusterRayTracingCS") {
        outLayout = RenderGeometryRootLayout::ClusterRayTracing;
        return true;
    }
    if (programId.rfind("HIKARI.EngineShader.Render3D_Cluster", 0u) == 0u) {
        outLayout = RenderGeometryRootLayout::ClusterCulling;
        return true;
    }
    if (programId == "HIKARI.EngineShader.Render3D_ParticleSimulation") {
        outLayout = RenderGeometryRootLayout::ParticleSimulation;
        return true;
    }
    if (programId == "HIKARI.EngineShader.Render3D_ParticleGpuDrivenCompute") {
        outLayout = RenderGeometryRootLayout::ParticleCompaction;
        return true;
    }
    if (programId == "HIKARI.EngineShader.Render3D_ParticleGpuDrivenDraw" ||
        programId == "HIKARI.EngineShader.Render3D_ParticleGpuDrivenRaster") {
        outLayout = RenderGeometryRootLayout::ParticleDraw;
        return true;
    }
    if (programId == "HIKARI.EngineShader.Render3D_WorldSpriteDraw" ||
        programId == "HIKARI.EngineShader.Render3D_WorldSpriteRaster") {
        outLayout = RenderGeometryRootLayout::WorldSpriteDraw;
        return true;
    }
    if (programId == "HIKARI.EngineShader.Render3D_WorldTextDraw") {
        outLayout = RenderGeometryRootLayout::WorldTextDraw;
        return true;
    }
    if (programId.rfind("HIKARI.EngineShader.Render3D_Particle", 0u) == 0u) {
        return false;
    }
    outLayout = RenderGeometryRootLayout::MeshGraphics;
    return true;
}

inline std::string_view RenderGeometryRootLayoutHash(RenderGeometryRootLayout layout) noexcept {
    switch (layout) {
    case RenderGeometryRootLayout::MeshGraphics: return kMeshGraphicsRootLayoutHash;
    case RenderGeometryRootLayout::TraditionalCommand: return kTraditionalCommandRootLayoutHash;
    case RenderGeometryRootLayout::ClusterCulling: return kClusterCullingRootLayoutHash;
    case RenderGeometryRootLayout::ParticleSimulation: return kParticleSimulationRootLayoutHash;
    case RenderGeometryRootLayout::ParticleCompaction: return kParticleCompactionRootLayoutHash;
    case RenderGeometryRootLayout::ParticleDraw: return kParticleDrawRootLayoutHash;
    case RenderGeometryRootLayout::WorldSpriteDraw: return kWorldSpriteDrawRootLayoutHash;
    case RenderGeometryRootLayout::WorldTextDraw: return kWorldTextDrawRootLayoutHash;
    case RenderGeometryRootLayout::ShadowGraphics: return kShadowGraphicsRootLayoutHash;
    case RenderGeometryRootLayout::ClusterRayTracing: return kClusterRayTracingRootLayoutHash;
    case RenderGeometryRootLayout::ModuleSurfaceDraw: return kModuleSurfaceDrawRootLayoutHash;
    }
    return {};
}

inline std::string_view RenderGeometryRootLayoutCanonical(RenderGeometryRootLayout layout) noexcept {
    switch (layout) {
    case RenderGeometryRootLayout::MeshGraphics: return kMeshGraphicsRootLayoutCanonical;
    case RenderGeometryRootLayout::TraditionalCommand: return kTraditionalCommandRootLayoutCanonical;
    case RenderGeometryRootLayout::ClusterCulling: return kClusterCullingRootLayoutCanonical;
    case RenderGeometryRootLayout::ParticleSimulation: return kParticleSimulationRootLayoutCanonical;
    case RenderGeometryRootLayout::ParticleCompaction: return kParticleCompactionRootLayoutCanonical;
    case RenderGeometryRootLayout::ParticleDraw: return kParticleDrawRootLayoutCanonical;
    case RenderGeometryRootLayout::WorldSpriteDraw: return kWorldSpriteDrawRootLayoutCanonical;
    case RenderGeometryRootLayout::WorldTextDraw: return kWorldTextDrawRootLayoutCanonical;
    case RenderGeometryRootLayout::ShadowGraphics: return kShadowGraphicsRootLayoutCanonical;
    case RenderGeometryRootLayout::ClusterRayTracing: return kClusterRayTracingRootLayoutCanonical;
    case RenderGeometryRootLayout::ModuleSurfaceDraw: return kModuleSurfaceDrawRootLayoutCanonical;
    }
    return {};
}

inline constexpr uint32_t RenderGeometryRootLayoutVersion(RenderGeometryRootLayout layout) noexcept {
    switch (layout) {
    case RenderGeometryRootLayout::MeshGraphics: return kMeshGraphicsRootLayoutVersion;
    case RenderGeometryRootLayout::TraditionalCommand: return kTraditionalCommandRootLayoutVersion;
    case RenderGeometryRootLayout::ClusterCulling: return kClusterCullingRootLayoutVersion;
    case RenderGeometryRootLayout::ParticleSimulation: return kParticleSimulationRootLayoutVersion;
    case RenderGeometryRootLayout::ParticleCompaction: return kParticleCompactionRootLayoutVersion;
    case RenderGeometryRootLayout::ParticleDraw: return kParticleDrawRootLayoutVersion;
    case RenderGeometryRootLayout::WorldSpriteDraw: return kWorldSpriteDrawRootLayoutVersion;
    case RenderGeometryRootLayout::WorldTextDraw: return kWorldTextDrawRootLayoutVersion;
    case RenderGeometryRootLayout::ShadowGraphics: return kShadowGraphicsRootLayoutVersion;
    case RenderGeometryRootLayout::ClusterRayTracing: return kClusterRayTracingRootLayoutVersion;
    case RenderGeometryRootLayout::ModuleSurfaceDraw: return kModuleSurfaceDrawRootLayoutVersion;
    }
    return 0u;
}

inline ShaderAssetContractExpectation MakeRenderGeometryShaderContract(
    RenderGeometryRootLayout layout, ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    if (RenderGeometryRootLayoutHash(layout).empty()) {
        return {};
    }
    return MakeShaderContract({kRenderGeometryEngineInterfaceId, kRenderGeometryEngineInterfaceVersion,
                               kRenderGeometryEngineInterfaceHash, RenderGeometryRootLayoutId(layout),
                               RenderGeometryRootLayoutVersion(layout), RenderGeometryRootLayoutHash(layout),
                               RenderGeometryRootLayoutCanonical(layout)},
                              availableCaps);
}

inline bool IsRenderGeometryStageProfileValid(const ShaderCookedEntry& entry) noexcept {
    switch (entry.entry.stage) {
    case ShaderStage::Vertex: return entry.entry.profile == "vs_6_0";
    case ShaderStage::Pixel: return entry.entry.profile == "ps_6_0";
    case ShaderStage::Compute: return entry.entry.profile == "cs_6_0";
    case ShaderStage::Amplification: return entry.entry.profile == "as_6_5";
    case ShaderStage::Mesh: return entry.entry.profile == "ms_6_5";
    default: return false;
    }
}

inline bool IsRenderGeometryEntryIdentityValid(std::string_view programId, RenderGeometryRootLayout layout,
                                               const ShaderCookedEntry& entry) noexcept {
    const std::string_view id = entry.entry.id;
    switch (layout) {
    case RenderGeometryRootLayout::MeshGraphics:
    case RenderGeometryRootLayout::ShadowGraphics:
    case RenderGeometryRootLayout::ClusterRayTracing: return id == "Main" && entry.entry.entry == "main";
    case RenderGeometryRootLayout::TraditionalCommand:
        return id == "CompactGpuTraditionalCommandStreamCS" && entry.entry.entry == id;
    case RenderGeometryRootLayout::ClusterCulling:
        if (entry.entry.entry != id) {
            return false;
        }
        if (programId.ends_with("ClusterDispatchFinalizeCS")) {
            return id == "FinalizePageTaskDispatchCS";
        }
        if (programId.ends_with("ClusterFineVisibilityCS")) {
            return id == "CullPageTasksCS";
        }
        if (programId.ends_with("ClusterMeshletDispatchFinalizeCS")) {
            return id == "FinalizeMeshletDispatchCS";
        }
        if (programId.ends_with("ClusterTransparentSortCS")) {
            return id == "SortTransparentRangesCS";
        }
        if (programId.ends_with("ClusterWorklistCS")) {
            return id == "ExpandPageTasksCS";
        }
        return false;
    case RenderGeometryRootLayout::ParticleSimulation:
        return (id == "ResetCS" || id == "SpawnCS" || id == "UpdateCS" || id == "CollisionDepthCS" ||
                id == "EventClearCS" || id == "EventSpawnCS") &&
               entry.entry.entry == id;
    case RenderGeometryRootLayout::ParticleCompaction:
        return (id == "BuildDrawArgsCS" || id == "CompactDrawCS" || id == "ResetDrawCS") && entry.entry.entry == id;
    case RenderGeometryRootLayout::ParticleDraw:
        return (id == "ParticlePS" || id == "ParticleSpriteMS" || id == "ParticleShapeMS" || id == "ParticleAS" ||
                id == "ParticleVS") &&
               entry.entry.entry == id;
    case RenderGeometryRootLayout::WorldSpriteDraw:
        return (id == "WorldSpriteMS" || id == "WorldSpritePS" || id == "WorldSpriteVS" || id == "WorldSpriteDepthPS" ||
                id == "WorldSpriteDepthOfFieldPS" || id == "WorldSpriteSurfacePS" ||
                id == "WorldSpriteSurfaceFeaturePS") &&
               entry.entry.entry == id;
    case RenderGeometryRootLayout::WorldTextDraw:
        return (id == "WorldTextMS" || id == "WorldTextPS") && entry.entry.entry == id;
    case RenderGeometryRootLayout::ModuleSurfaceDraw:
        return (id == "SurfaceVS" || id == "SurfaceGeometryPS" || id == "SurfaceGeometryFeaturePS" ||
                id == "SurfaceForwardPS") &&
               entry.entry.entry == id;
    }
    return false;
}

inline bool MatchesRenderGeometryBinding(const ShaderResourceBinding& binding, ShaderResourceBindingKind kind,
                                         uint32_t registerSpace, uint32_t registerIndex, uint32_t count = 1u) noexcept {
    return binding.kind == kind && binding.registerSpace == registerSpace && binding.registerIndex == registerIndex &&
           binding.count == count;
}

inline bool IsRenderGeometryBindingAllowed(RenderGeometryRootLayout layout,
                                           const ShaderResourceBinding& binding) noexcept {
    using Kind = ShaderResourceBindingKind;
    switch (layout) {
    case RenderGeometryRootLayout::ModuleSurfaceDraw:
        return (binding.kind == Kind::ShaderResource && binding.registerSpace == 10u && binding.registerIndex <= 4u &&
                binding.count == 1u) ||
               IsRenderGeometryBindingAllowed(RenderGeometryRootLayout::MeshGraphics, binding);
    case RenderGeometryRootLayout::ClusterRayTracing:
        return MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 0u, 0u) ||
               MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 0u, 1u) ||
               MatchesRenderGeometryBinding(binding, Kind::UnorderedAccess, 0u, 0u) ||
               MatchesRenderGeometryBinding(binding, Kind::ConstantBuffer, 0u, 0u);
    case RenderGeometryRootLayout::ShadowGraphics:
        if (binding.kind == Kind::ConstantBuffer)
            return binding.registerSpace == 0u && binding.count == 1u &&
                   (binding.registerIndex <= 1u || binding.registerIndex == 3u ||
                    (binding.registerIndex >= 7u && binding.registerIndex <= 10u));
        if (binding.kind == Kind::Sampler)
            return MatchesRenderGeometryBinding(binding, Kind::Sampler, 0u, 0u);
        if (binding.kind != Kind::ShaderResource)
            return false;
        return (binding.registerSpace == 0u && binding.count == 1u &&
                (binding.registerIndex == 0u || (binding.registerIndex >= 15u && binding.registerIndex <= 19u))) ||
               MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 0u, 20u, 3968u) ||
               MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 1u, 0u, 1024u) ||
               MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 3u, 0u) ||
               (binding.registerSpace == 6u && binding.registerIndex <= 1u && binding.count == 1u) ||
               (binding.registerSpace == 7u && binding.count == 1u &&
                (binding.registerIndex == 1u || binding.registerIndex == 4u));
    case RenderGeometryRootLayout::MeshGraphics:
        if (binding.kind == Kind::UnorderedAccess) {
            return MatchesRenderGeometryBinding(binding, Kind::UnorderedAccess, 8u, 0u) &&
                   binding.visibilityMask == (1u << static_cast<uint32_t>(ShaderStage::Amplification));
        }
        if (binding.kind == Kind::ConstantBuffer) {
            return binding.registerSpace == 0u && binding.registerIndex <= 12u && binding.count == 1u;
        }
        if (binding.kind == Kind::Sampler) {
            return binding.registerSpace == 0u && binding.registerIndex <= 4u && binding.count == 1u;
        }
        if (binding.kind != Kind::ShaderResource) {
            return false;
        }
        if (binding.registerSpace == 0u) {
            return binding.count == 1u &&
                       (binding.registerIndex == 2u || (binding.registerIndex >= 6u && binding.registerIndex <= 19u)) ||
                   binding.registerIndex == 20u && binding.count == 3968u;
        }
        return MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 1u, 0u, 1024u) ||
               MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 2u, 0u, 9u) ||
               MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 3u, 0u) ||
               MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 4u, 0u, 9u) ||
               (binding.registerSpace == 5u && binding.registerIndex <= 2u && binding.count == 1u) ||
               (binding.registerSpace == 6u && binding.registerIndex <= 1u && binding.count == 1u) ||
               (binding.registerSpace == 7u &&
                (((binding.registerIndex <= 4u || binding.registerIndex == 11u) && binding.count == 1u) ||
                 ((binding.registerIndex == 5u || binding.registerIndex == 8u) && binding.count == 3u))) ||
               MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 8u, 0u);
    case RenderGeometryRootLayout::TraditionalCommand:
        return MatchesRenderGeometryBinding(binding, Kind::ConstantBuffer, 0u, 0u) ||
               (binding.kind == Kind::ShaderResource && binding.registerSpace == 0u && binding.registerIndex <= 1u &&
                binding.count == 1u) ||
               (binding.kind == Kind::UnorderedAccess && binding.registerSpace == 0u && binding.registerIndex <= 2u &&
                binding.count == 1u);
    case RenderGeometryRootLayout::ClusterCulling:
        return MatchesRenderGeometryBinding(binding, Kind::ConstantBuffer, 0u, 0u) ||
               (binding.kind == Kind::ShaderResource && binding.registerSpace == 0u && binding.registerIndex >= 17u &&
                binding.registerIndex <= 18u && binding.count == 1u) ||
               MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 1u, 0u, 1024u) ||
               MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 8u, 0u) ||
               (binding.kind == Kind::UnorderedAccess && binding.registerSpace == 0u &&
                (binding.registerIndex <= 1u || (binding.registerIndex >= 3u && binding.registerIndex <= 7u)) &&
                binding.count == 1u);
    case RenderGeometryRootLayout::ParticleSimulation:
        return (binding.kind == Kind::ConstantBuffer && binding.registerSpace == 0u && binding.registerIndex <= 2u &&
                binding.count == 1u) ||
               (binding.kind == Kind::ShaderResource && binding.registerSpace == 0u && binding.registerIndex <= 5u &&
                binding.count == 1u) ||
               MatchesRenderGeometryBinding(binding, Kind::Sampler, 0u, 0u) ||
               (binding.kind == Kind::UnorderedAccess && binding.registerSpace == 0u && binding.registerIndex <= 6u &&
                binding.count == 1u);
    case RenderGeometryRootLayout::ParticleCompaction:
        return MatchesRenderGeometryBinding(binding, Kind::ConstantBuffer, 0u, 1u) ||
               (binding.kind == Kind::UnorderedAccess && binding.registerSpace == 0u && binding.registerIndex <= 3u &&
                binding.count == 1u);
    case RenderGeometryRootLayout::ParticleDraw:
        return (binding.kind == Kind::ConstantBuffer && binding.registerSpace == 0u &&
                (binding.registerIndex <= 2u || binding.registerIndex == 4u || binding.registerIndex == 5u ||
                 binding.registerIndex == 7u || binding.registerIndex == 8u) &&
                binding.count == 1u) ||
               (binding.kind == Kind::ShaderResource && binding.registerSpace == 0u &&
                (((binding.registerIndex >= 1u && binding.registerIndex <= 5u) || binding.registerIndex == 9u ||
                  binding.registerIndex == 10u || binding.registerIndex == 11u || binding.registerIndex == 16u) &&
                     binding.count == 1u ||
                 (binding.registerIndex == 20u && binding.count == 3968u))) ||
               (binding.kind == Kind::ShaderResource && binding.registerSpace == 5u && binding.registerIndex <= 2u &&
                binding.count == 1u) ||
               (binding.kind == Kind::ShaderResource && binding.registerSpace == 6u && binding.registerIndex <= 1u &&
                binding.count == 1u) ||
               MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 9u, 0u) ||
               (binding.kind == Kind::Sampler && binding.registerSpace == 0u && binding.registerIndex <= 2u &&
                binding.count == 1u);
    case RenderGeometryRootLayout::WorldSpriteDraw:
        return (binding.kind == Kind::ConstantBuffer && binding.registerSpace == 0u && binding.registerIndex <= 5u &&
                binding.count == 1u) ||
               (binding.kind == Kind::ShaderResource && binding.registerSpace == 0u &&
                ((binding.registerIndex == 1u || binding.registerIndex == 2u || binding.registerIndex == 9u ||
                  binding.registerIndex == 10u) &&
                     binding.count == 1u ||
                 binding.registerIndex == 20u && binding.count == 3968u)) ||
               (binding.kind == Kind::ShaderResource && binding.registerSpace == 5u && binding.registerIndex <= 2u &&
                binding.count == 1u) ||
               MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 9u, 0u) ||
               (binding.kind == Kind::Sampler && binding.registerSpace == 0u &&
                (binding.registerIndex == 0u || binding.registerIndex == 2u) && binding.count == 1u);
    case RenderGeometryRootLayout::WorldTextDraw:
        return MatchesRenderGeometryBinding(binding, Kind::ConstantBuffer, 0u, 0u) ||
               MatchesRenderGeometryBinding(binding, Kind::ConstantBuffer, 0u, 1u) ||
               MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 0u, 1u) ||
               MatchesRenderGeometryBinding(binding, Kind::ShaderResource, 0u, 20u, 3968u) ||
               MatchesRenderGeometryBinding(binding, Kind::Sampler, 0u, 0u);
    }
    return false;
}

inline bool ValidateRenderGeometryShaderArtifact(const ShaderCookedArtifact& artifact, std::string& outDiagnostic) {
    RenderGeometryRootLayout rootLayout{};
    RenderGeometryRootLayout expectedRootLayout{};
    if (artifact.entries.empty() || !TryParseRenderGeometryRootLayout(artifact.rootLayoutId, rootLayout) ||
        !TryResolveRenderGeometryProgramRootLayout(artifact.programId, expectedRootLayout) ||
        rootLayout != expectedRootLayout) {
        outDiagnostic = "RenderGeometry artifact identity is invalid";
        return false;
    }
    const size_t expectedEntryCount =
        rootLayout == RenderGeometryRootLayout::ParticleDraw
            ? (artifact.programId == "HIKARI.EngineShader.Render3D_ParticleGpuDrivenRaster" ? 2u : 4u)
        : rootLayout == RenderGeometryRootLayout::ParticleSimulation
            ? 6u // Reset、Update、Spawn、CollisionDepth、Event*
        : rootLayout == RenderGeometryRootLayout::ParticleCompaction ? 3u
        : rootLayout == RenderGeometryRootLayout::WorldSpriteDraw    ? 6u
        : rootLayout == RenderGeometryRootLayout::WorldTextDraw      ? 2u
        : rootLayout == RenderGeometryRootLayout::ModuleSurfaceDraw  ? 4u
                                                                     : 1u;
    if (artifact.entries.size() != expectedEntryCount) {
        outDiagnostic = "RenderGeometry artifact entry count is invalid";
        return false;
    }
    const ShaderAssetContractExpectation expected = MakeRenderGeometryShaderContract(
        rootLayout, {artifact.requiredCaps.minimumShaderModel, artifact.requiredCaps.minimumRootSignatureVersion,
                     artifact.requiredCaps.requiredFeatureMask});
    bool requiresMeshShader = false;
    for (const ShaderCookedEntry& entry : artifact.entries) {
        if (!IsRenderGeometryStageProfileValid(entry) ||
            !IsRenderGeometryEntryIdentityValid(artifact.programId, rootLayout, entry)) {
            outDiagnostic = "RenderGeometry artifact entry identity is invalid";
            return false;
        }
        const uint32_t expectedVisibility = 1u << static_cast<uint32_t>(entry.entry.stage);
        for (const ShaderResourceBinding& binding : entry.bindings) {
            if (binding.visibilityMask != expectedVisibility || !IsRenderGeometryBindingAllowed(rootLayout, binding)) {
                outDiagnostic = "RenderGeometry reflection escapes its root layout: "
                                "kind=" +
                                std::to_string(static_cast<uint32_t>(binding.kind)) +
                                " register=" + std::to_string(binding.registerIndex) +
                                " space=" + std::to_string(binding.registerSpace) +
                                " count=" + std::to_string(binding.count) +
                                " visibility=" + std::to_string(binding.visibilityMask);
                return false;
            }
        }
        requiresMeshShader = requiresMeshShader || entry.entry.stage == ShaderStage::Amplification ||
                             entry.entry.stage == ShaderStage::Mesh;
    }
    const uint32_t expectedShaderModel = requiresMeshShader ? 65u : 60u;
    const uint64_t expectedFeatureMask = requiresMeshShader ? ShaderDeviceFeature::MeshShader : 0u;
    if (expected.engineInterfaceHash.IsZero() || expected.rootLayoutHash.IsZero() ||
        artifact.engineInterfaceId != expected.engineInterfaceId ||
        artifact.engineInterfaceVersion != expected.engineInterfaceVersion ||
        artifact.engineInterfaceHash != expected.engineInterfaceHash ||
        artifact.rootLayoutId != expected.rootLayoutId || artifact.rootLayoutVersion != expected.rootLayoutVersion ||
        artifact.rootLayoutHash != expected.rootLayoutHash ||
        artifact.requiredCaps.minimumShaderModel != expectedShaderModel ||
        artifact.requiredCaps.minimumRootSignatureVersion != 10u ||
        artifact.requiredCaps.requiredFeatureMask != expectedFeatureMask) {
        outDiagnostic = "RenderGeometry artifact contract is invalid";
        return false;
    }
    bool ownsInterface = false;
    for (const ShaderSourceDependency& dependency : artifact.dependencies) {
        ownsInterface = ownsInterface || (dependency.logicalPath == kRenderGeometryInterfaceDependency &&
                                          dependency.contentHash == expected.engineInterfaceHash);
    }
    if (!ownsInterface) {
        outDiagnostic = "RenderGeometry interface dependency is missing";
        return false;
    }
    outDiagnostic.clear();
    return true;
}

} // namespace HIKARI::ASSETS::SHADER
