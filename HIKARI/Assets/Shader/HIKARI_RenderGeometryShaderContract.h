#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "Assets/Shader/HIKARI_ShaderAssetResolver.h"
#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::ASSETS::SHADER {

    inline constexpr std::string_view kRenderGeometryEngineInterfaceId =
        "HIKARI.RenderGeometry";
    inline constexpr uint32_t kRenderGeometryEngineInterfaceVersion = 1u;
    inline constexpr std::string_view kRenderGeometryEngineInterfaceHash =
        "630237ae301298e15328be80f98b8d771aba449eecaf0a577a58eaa0cbe16f6e";
    inline constexpr std::string_view kRenderGeometryInterfaceDependency =
        "EngineSdk/Include/Render3D/HIKARI_RenderGeometryInterface.hlsli";

    enum class RenderGeometryRootLayout : uint8_t {
        MeshGraphics = 0u,
        TraditionalCommand,
        ClusterCulling,
        ParticleSimulation,
        ParticleCompaction,
        ParticleDraw,
    };

    inline constexpr std::string_view kMeshGraphicsRootLayoutId =
        "HIKARI.RenderGeometry.MeshGraphics.Root";
    inline constexpr std::string_view kMeshGraphicsRootLayoutHash =
        "464cf94187a9e276f496ff60a579d5d046e9a753f3bd693993fd935f2695b8dd";
    inline constexpr std::string_view kMeshGraphicsRootLayoutCanonical =
        "HIKARI.RenderGeometry.MeshGraphics.Root.v1|"
        "RootParameters(42)|DwordCost(64)|"
        "JointPaletteVariant(StaticSRV(t0,space3),SkinnedCBV(b3,space0))|"
        "SurfaceControlAlias(b8,TraditionalSurfaceGpuScene,MeshletDrawControl)|"
        "StaticSamplers(s0,s1,s2,s3,s4)|"
        "Pipeline(Graphics,Amplification,Mesh)|Flags(AllowInputAssembler)";

    inline constexpr std::string_view kTraditionalCommandRootLayoutId =
        "HIKARI.RenderGeometry.TraditionalCommand.Root";
    inline constexpr std::string_view kTraditionalCommandRootLayoutHash =
        "cf60faa111cdfb4808e88526ae3b68bea6c05bb1078231951d7b4d518e34eab3";
    inline constexpr std::string_view kTraditionalCommandRootLayoutCanonical =
        "HIKARI.RenderGeometry.TraditionalCommand.Root.v1|"
        "0:CBV(b0,space0,All)|1:SRV(t0,space0,All)|"
        "2:UAV(u0,space0,All)|3:SRV(t1,space0,All)|"
        "4:UAV(u1,space0,All)|5:UAV(u2,space0,All)|"
        "Pipeline(Compute)|Flags(None)";

    inline constexpr std::string_view kClusterCullingRootLayoutId =
        "HIKARI.RenderGeometry.ClusterCulling.Root";
    inline constexpr std::string_view kClusterCullingRootLayoutHash =
        "7fd9e59581449e67338e968652587b7eea22778290fb0b08ed7da2d3a1e12ee7";
    inline constexpr std::string_view kClusterCullingRootLayoutCanonical =
        "HIKARI.RenderGeometry.ClusterCulling.Root.v1|"
        "0:CBV(b0,space0,All)|1:SRV(t17,space0,All)|"
        "2:UAV(u0,space0,All)|3:UAV(u1,space0,All)|"
        "4:Table(SRV(t0,space1,count111),All)|"
        "5:UAV(u3,space0,All)|6:UAV(u4,space0,All)|"
        "7:UAV(u5,space0,All)|8:Table(SRV(t18,space0,count1),All)|"
        "9:UAV(u6,space0,All)|10:UAV(u7,space0,All)|"
        "11:SRV(t0,space8,All)|Pipeline(Compute)|Flags(None)";

    inline constexpr std::string_view kParticleSimulationRootLayoutId =
        "HIKARI.RenderGeometry.ParticleSimulation.Root";
    inline constexpr std::string_view kParticleSimulationRootLayoutHash =
        "13007e949f7390c3d7e4b2266249d437402af2ef980dfa560923f16d9fafe838";
    inline constexpr std::string_view kParticleSimulationRootLayoutCanonical =
        "HIKARI.RenderGeometry.ParticleSimulation.Root.v1|"
        "0:Table(UAV(u0,space0,count1),All)|1:SRV(t0,space0,All)|"
        "2:Constants(b0,space0,count8,All)|Pipeline(Compute)|Flags(None)";

    inline constexpr std::string_view kParticleCompactionRootLayoutId =
        "HIKARI.RenderGeometry.ParticleCompaction.Root";
    inline constexpr std::string_view kParticleCompactionRootLayoutHash =
        "0ed335ed555028fae367718e16f0035cb4b2b0688718978fb7a09de98dd34d4d";
    inline constexpr std::string_view kParticleCompactionRootLayoutCanonical =
        "HIKARI.RenderGeometry.ParticleCompaction.Root.v1|"
        "0:Table(UAV(u0,space0,count1),All)|1:UAV(u1,space0,All)|"
        "2:UAV(u2,space0,All)|3:UAV(u3,space0,All)|"
        "4:Constants(b1,space0,count4,All)|Pipeline(Compute)|Flags(None)";

    inline constexpr std::string_view kParticleDrawRootLayoutId =
        "HIKARI.RenderGeometry.ParticleDraw.Root";
    inline constexpr std::string_view kParticleDrawRootLayoutHash =
        "0918d4159c16db6c3d8225e1582f895969658d1b6a463cd81efe4b31f18aaab2";
    inline constexpr std::string_view kParticleDrawRootLayoutCanonical =
        "HIKARI.RenderGeometry.ParticleDraw.Root.v1|"
        "0:CBV(b0,space0,All)|1:SRV(t1,space0,All)|"
        "2:SRV(t2,space0,All)|3:SRV(t16,space0,Pixel)|"
        "4:SRV(t0,space6,Pixel)|5:SRV(t1,space6,Pixel)|"
        "6:Table(SRV(t20,space0,count3968),Pixel)|"
        "7:Constants(b1,space0,count32,All)|"
        "8:Constants(b7,space0,count4,Pixel)|"
        "9:Table(SRV(t3,space0,count1),Pixel)|"
        "StaticSampler(s0,LinearClamp,Pixel)|"
        "StaticSampler(s1,PointClamp,Pixel)|"
        "Pipeline(Mesh,Pixel)|Flags(None)";

    inline std::string_view RenderGeometryRootLayoutId(
        RenderGeometryRootLayout layout) noexcept {
        switch (layout) {
        case RenderGeometryRootLayout::MeshGraphics:
            return kMeshGraphicsRootLayoutId;
        case RenderGeometryRootLayout::TraditionalCommand:
            return kTraditionalCommandRootLayoutId;
        case RenderGeometryRootLayout::ClusterCulling:
            return kClusterCullingRootLayoutId;
        case RenderGeometryRootLayout::ParticleSimulation:
            return kParticleSimulationRootLayoutId;
        case RenderGeometryRootLayout::ParticleCompaction:
            return kParticleCompactionRootLayoutId;
        case RenderGeometryRootLayout::ParticleDraw:
            return kParticleDrawRootLayoutId;
        }
        return {};
    }

    inline bool TryParseRenderGeometryRootLayout(
        std::string_view id,
        RenderGeometryRootLayout& outLayout) noexcept {
        for (uint8_t raw = 0u;
             raw <= static_cast<uint8_t>(
                 RenderGeometryRootLayout::ParticleDraw);
             ++raw) {
            const auto layout =
                static_cast<RenderGeometryRootLayout>(raw);
            if (RenderGeometryRootLayoutId(layout) == id) {
                outLayout = layout;
                return true;
            }
        }
        return false;
    }

    inline bool TryResolveRenderGeometryProgramRootLayout(
        std::string_view programId,
        RenderGeometryRootLayout& outLayout) noexcept {
        constexpr std::string_view prefix =
            "HIKARI.EngineShader.Render3D_";
        if (programId.rfind(prefix, 0u) != 0u) {
            return false;
        }
        if (programId ==
                "HIKARI.EngineShader."
                "Render3D_GpuTraditionalCommandCompactCS") {
            outLayout = RenderGeometryRootLayout::TraditionalCommand;
            return true;
        }
        if (programId.rfind(
                "HIKARI.EngineShader.Render3D_Cluster",
                0u) == 0u) {
            outLayout = RenderGeometryRootLayout::ClusterCulling;
            return true;
        }
        if (programId ==
                "HIKARI.EngineShader.Render3D_ParticleSimulation") {
            outLayout = RenderGeometryRootLayout::ParticleSimulation;
            return true;
        }
        if (programId ==
                "HIKARI.EngineShader.Render3D_ParticleGpuDrivenCompute") {
            outLayout = RenderGeometryRootLayout::ParticleCompaction;
            return true;
        }
        if (programId ==
                "HIKARI.EngineShader.Render3D_ParticleGpuDrivenDraw") {
            outLayout = RenderGeometryRootLayout::ParticleDraw;
            return true;
        }
        if (programId.rfind(
                "HIKARI.EngineShader.Render3D_Particle",
                0u) == 0u) {
            return false;
        }
        outLayout = RenderGeometryRootLayout::MeshGraphics;
        return true;
    }

    inline std::string_view RenderGeometryRootLayoutHash(
        RenderGeometryRootLayout layout) noexcept {
        switch (layout) {
        case RenderGeometryRootLayout::MeshGraphics:
            return kMeshGraphicsRootLayoutHash;
        case RenderGeometryRootLayout::TraditionalCommand:
            return kTraditionalCommandRootLayoutHash;
        case RenderGeometryRootLayout::ClusterCulling:
            return kClusterCullingRootLayoutHash;
        case RenderGeometryRootLayout::ParticleSimulation:
            return kParticleSimulationRootLayoutHash;
        case RenderGeometryRootLayout::ParticleCompaction:
            return kParticleCompactionRootLayoutHash;
        case RenderGeometryRootLayout::ParticleDraw:
            return kParticleDrawRootLayoutHash;
        }
        return {};
    }

    inline std::string_view RenderGeometryRootLayoutCanonical(
        RenderGeometryRootLayout layout) noexcept {
        switch (layout) {
        case RenderGeometryRootLayout::MeshGraphics:
            return kMeshGraphicsRootLayoutCanonical;
        case RenderGeometryRootLayout::TraditionalCommand:
            return kTraditionalCommandRootLayoutCanonical;
        case RenderGeometryRootLayout::ClusterCulling:
            return kClusterCullingRootLayoutCanonical;
        case RenderGeometryRootLayout::ParticleSimulation:
            return kParticleSimulationRootLayoutCanonical;
        case RenderGeometryRootLayout::ParticleCompaction:
            return kParticleCompactionRootLayoutCanonical;
        case RenderGeometryRootLayout::ParticleDraw:
            return kParticleDrawRootLayoutCanonical;
        }
        return {};
    }

    inline ShaderAssetContractExpectation MakeRenderGeometryShaderContract(
        RenderGeometryRootLayout layout,
        ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
        ShaderAssetContractExpectation contract{};
        contract.engineInterfaceId = kRenderGeometryEngineInterfaceId;
        contract.engineInterfaceVersion =
            kRenderGeometryEngineInterfaceVersion;
        contract.rootLayoutId = RenderGeometryRootLayoutId(layout);
        contract.rootLayoutVersion = 1u;
        contract.availableDeviceCaps = availableCaps;
        HASH::Sha256Digest interfaceHash{};
        HASH::Sha256Digest rootLayoutHash{};
        HASH::Sha256Digest computedRootLayoutHash{};
        if (!HASH::ParseSha256(
                kRenderGeometryEngineInterfaceHash,
                interfaceHash) ||
            !HASH::ParseSha256(
                RenderGeometryRootLayoutHash(layout),
                rootLayoutHash) ||
            !HASH::ComputeSha256(
                RenderGeometryRootLayoutCanonical(layout),
                computedRootLayoutHash) ||
            computedRootLayoutHash != rootLayoutHash) {
            return {};
        }
        contract.engineInterfaceHash = interfaceHash;
        contract.rootLayoutHash = rootLayoutHash;
        return contract;
    }

    inline bool IsRenderGeometryStageProfileValid(
        const ShaderCookedEntry& entry) noexcept {
        switch (entry.entry.stage) {
        case ShaderStage::Vertex:
            return entry.entry.profile == "vs_6_0";
        case ShaderStage::Pixel:
            return entry.entry.profile == "ps_6_0";
        case ShaderStage::Compute:
            return entry.entry.profile == "cs_6_0";
        case ShaderStage::Amplification:
            return entry.entry.profile == "as_6_5";
        case ShaderStage::Mesh:
            return entry.entry.profile == "ms_6_5";
        default:
            return false;
        }
    }

    inline bool IsRenderGeometryEntryIdentityValid(
        std::string_view programId,
        RenderGeometryRootLayout layout,
        const ShaderCookedEntry& entry) noexcept {
        const std::string_view id = entry.entry.id;
        switch (layout) {
        case RenderGeometryRootLayout::MeshGraphics:
            return id == "Main" && entry.entry.entry == "main";
        case RenderGeometryRootLayout::TraditionalCommand:
            return id == "CompactGpuTraditionalCommandStreamCS" &&
                entry.entry.entry == id;
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
            if (programId.ends_with(
                    "ClusterMeshletDispatchFinalizeCS")) {
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
            return (id == "ResetCS" || id == "SpawnCS" ||
                    id == "UpdateCS") &&
                entry.entry.entry == id;
        case RenderGeometryRootLayout::ParticleCompaction:
            return (id == "BuildDrawArgsCS" || id == "CompactDrawCS" ||
                    id == "ResetDrawCS") &&
                entry.entry.entry == id;
        case RenderGeometryRootLayout::ParticleDraw:
            return (id == "ParticlePS" || id == "ParticleSpriteMS" ||
                    id == "ParticleStripMS") &&
                entry.entry.entry == id;
        }
        return false;
    }

    inline bool MatchesRenderGeometryBinding(
        const ShaderResourceBinding& binding,
        ShaderResourceBindingKind kind,
        uint32_t registerSpace,
        uint32_t registerIndex,
        uint32_t count = 1u) noexcept {
        return binding.kind == kind &&
            binding.registerSpace == registerSpace &&
            binding.registerIndex == registerIndex &&
            binding.count == count;
    }

    inline bool IsRenderGeometryBindingAllowed(
        RenderGeometryRootLayout layout,
        const ShaderResourceBinding& binding) noexcept {
        using Kind = ShaderResourceBindingKind;
        switch (layout) {
        case RenderGeometryRootLayout::MeshGraphics:
            if (binding.kind == Kind::ConstantBuffer) {
                return binding.registerSpace == 0u &&
                    binding.registerIndex <= 11u &&
                    binding.count == 1u;
            }
            if (binding.kind == Kind::Sampler) {
                return binding.registerSpace == 0u &&
                    binding.registerIndex <= 4u &&
                    binding.count == 1u;
            }
            if (binding.kind != Kind::ShaderResource) {
                return false;
            }
            if (binding.registerSpace == 0u) {
                return binding.count == 1u &&
                    (binding.registerIndex == 2u ||
                     (binding.registerIndex >= 6u &&
                      binding.registerIndex <= 19u)) ||
                    binding.registerIndex == 20u &&
                    binding.count == 3968u;
            }
            return
                MatchesRenderGeometryBinding(
                    binding, Kind::ShaderResource, 1u, 0u, 111u) ||
                MatchesRenderGeometryBinding(
                    binding, Kind::ShaderResource, 2u, 0u, 9u) ||
                MatchesRenderGeometryBinding(
                    binding, Kind::ShaderResource, 3u, 0u) ||
                MatchesRenderGeometryBinding(
                    binding, Kind::ShaderResource, 4u, 0u, 9u) ||
                (binding.registerSpace == 5u &&
                 binding.registerIndex <= 2u && binding.count == 1u) ||
                (binding.registerSpace == 6u &&
                 binding.registerIndex <= 1u && binding.count == 1u) ||
                (binding.registerSpace == 7u &&
                 ((binding.registerIndex <= 4u &&
                   binding.count == 1u) ||
                  ((binding.registerIndex == 5u ||
                    binding.registerIndex == 8u) &&
                   binding.count == 3u))) ||
                MatchesRenderGeometryBinding(
                    binding, Kind::ShaderResource, 8u, 0u);
        case RenderGeometryRootLayout::TraditionalCommand:
            return
                MatchesRenderGeometryBinding(
                    binding, Kind::ConstantBuffer, 0u, 0u) ||
                (binding.kind == Kind::ShaderResource &&
                 binding.registerSpace == 0u &&
                 binding.registerIndex <= 1u && binding.count == 1u) ||
                (binding.kind == Kind::UnorderedAccess &&
                 binding.registerSpace == 0u &&
                 binding.registerIndex <= 2u && binding.count == 1u);
        case RenderGeometryRootLayout::ClusterCulling:
            return
                MatchesRenderGeometryBinding(
                    binding, Kind::ConstantBuffer, 0u, 0u) ||
                (binding.kind == Kind::ShaderResource &&
                 binding.registerSpace == 0u &&
                 binding.registerIndex >= 17u &&
                 binding.registerIndex <= 18u &&
                 binding.count == 1u) ||
                MatchesRenderGeometryBinding(
                    binding, Kind::ShaderResource, 1u, 0u, 111u) ||
                MatchesRenderGeometryBinding(
                    binding, Kind::ShaderResource, 8u, 0u) ||
                (binding.kind == Kind::UnorderedAccess &&
                 binding.registerSpace == 0u &&
                 (binding.registerIndex <= 1u ||
                  (binding.registerIndex >= 3u &&
                   binding.registerIndex <= 7u)) &&
                 binding.count == 1u);
        case RenderGeometryRootLayout::ParticleSimulation:
            return
                MatchesRenderGeometryBinding(
                    binding, Kind::ConstantBuffer, 0u, 0u) ||
                MatchesRenderGeometryBinding(
                    binding, Kind::ShaderResource, 0u, 0u) ||
                MatchesRenderGeometryBinding(
                    binding, Kind::UnorderedAccess, 0u, 0u);
        case RenderGeometryRootLayout::ParticleCompaction:
            return
                MatchesRenderGeometryBinding(
                    binding, Kind::ConstantBuffer, 0u, 1u) ||
                (binding.kind == Kind::UnorderedAccess &&
                 binding.registerSpace == 0u &&
                 binding.registerIndex <= 3u && binding.count == 1u);
        case RenderGeometryRootLayout::ParticleDraw:
            return
                (binding.kind == Kind::ConstantBuffer &&
                 binding.registerSpace == 0u &&
                 (binding.registerIndex <= 1u ||
                  binding.registerIndex == 7u) &&
                 binding.count == 1u) ||
                (binding.kind == Kind::ShaderResource &&
                 binding.registerSpace == 0u &&
                 ((binding.registerIndex >= 1u &&
                   binding.registerIndex <= 3u &&
                   binding.count == 1u) ||
                  (binding.registerIndex == 16u &&
                   binding.count == 1u) ||
                  (binding.registerIndex == 20u &&
                   binding.count == 3968u))) ||
                (binding.kind == Kind::ShaderResource &&
                 binding.registerSpace == 6u &&
                 binding.registerIndex <= 1u && binding.count == 1u) ||
                (binding.kind == Kind::Sampler &&
                 binding.registerSpace == 0u &&
                 binding.registerIndex <= 1u && binding.count == 1u);
        }
        return false;
    }

    inline bool ValidateRenderGeometryShaderArtifact(
        const ShaderCookedArtifact& artifact,
        std::string& outDiagnostic) {
        RenderGeometryRootLayout rootLayout{};
        RenderGeometryRootLayout expectedRootLayout{};
        if (artifact.entries.empty() ||
            !TryParseRenderGeometryRootLayout(
                artifact.rootLayoutId,
                rootLayout) ||
            !TryResolveRenderGeometryProgramRootLayout(
                artifact.programId,
                expectedRootLayout) ||
            rootLayout != expectedRootLayout) {
            outDiagnostic = "RenderGeometry artifact identity is invalid";
            return false;
        }
        const size_t expectedEntryCount =
            rootLayout ==
                    RenderGeometryRootLayout::ParticleSimulation ||
                rootLayout ==
                    RenderGeometryRootLayout::ParticleCompaction ||
                rootLayout ==
                    RenderGeometryRootLayout::ParticleDraw
                ? 3u
                : 1u;
        if (artifact.entries.size() != expectedEntryCount) {
            outDiagnostic =
                "RenderGeometry artifact entry count is invalid";
            return false;
        }
        const ShaderAssetContractExpectation expected =
            MakeRenderGeometryShaderContract(
                rootLayout,
                { artifact.requiredCaps.minimumShaderModel,
                  artifact.requiredCaps.minimumRootSignatureVersion,
                  artifact.requiredCaps.requiredFeatureMask });
        bool requiresMeshShader = false;
        for (const ShaderCookedEntry& entry : artifact.entries) {
            if (!IsRenderGeometryStageProfileValid(entry) ||
                !IsRenderGeometryEntryIdentityValid(
                    artifact.programId,
                    rootLayout,
                    entry)) {
                outDiagnostic =
                    "RenderGeometry artifact entry identity is invalid";
                return false;
            }
            const uint32_t expectedVisibility =
                1u << static_cast<uint32_t>(entry.entry.stage);
            for (const ShaderResourceBinding& binding : entry.bindings) {
                if (binding.visibilityMask != expectedVisibility ||
                    !IsRenderGeometryBindingAllowed(
                        rootLayout,
                        binding)) {
                    outDiagnostic =
                        "RenderGeometry reflection escapes its root layout: "
                        "kind=" + std::to_string(
                            static_cast<uint32_t>(binding.kind)) +
                        " register=" +
                        std::to_string(binding.registerIndex) +
                        " space=" +
                        std::to_string(binding.registerSpace) +
                        " count=" + std::to_string(binding.count) +
                        " visibility=" +
                        std::to_string(binding.visibilityMask);
                    return false;
                }
            }
            requiresMeshShader = requiresMeshShader ||
                entry.entry.stage == ShaderStage::Amplification ||
                entry.entry.stage == ShaderStage::Mesh;
        }
        const uint32_t expectedShaderModel =
            requiresMeshShader ? 65u : 60u;
        const uint64_t expectedFeatureMask =
            requiresMeshShader ? ShaderDeviceFeature::MeshShader : 0u;
        if (expected.engineInterfaceHash.IsZero() ||
            expected.rootLayoutHash.IsZero() ||
            artifact.engineInterfaceId != expected.engineInterfaceId ||
            artifact.engineInterfaceVersion !=
                expected.engineInterfaceVersion ||
            artifact.engineInterfaceHash != expected.engineInterfaceHash ||
            artifact.rootLayoutId != expected.rootLayoutId ||
            artifact.rootLayoutVersion != expected.rootLayoutVersion ||
            artifact.rootLayoutHash != expected.rootLayoutHash ||
            artifact.requiredCaps.minimumShaderModel !=
                expectedShaderModel ||
            artifact.requiredCaps.minimumRootSignatureVersion != 10u ||
            artifact.requiredCaps.requiredFeatureMask !=
                expectedFeatureMask) {
            outDiagnostic =
                "RenderGeometry artifact contract is invalid";
            return false;
        }
        bool ownsInterface = false;
        for (const ShaderSourceDependency& dependency :
             artifact.dependencies) {
            ownsInterface = ownsInterface ||
                (dependency.logicalPath ==
                     kRenderGeometryInterfaceDependency &&
                 dependency.contentHash ==
                     expected.engineInterfaceHash);
        }
        if (!ownsInterface) {
            outDiagnostic =
                "RenderGeometry interface dependency is missing";
            return false;
        }
        outDiagnostic.clear();
        return true;
    }

} // namespace HIKARI::ASSETS::SHADER
