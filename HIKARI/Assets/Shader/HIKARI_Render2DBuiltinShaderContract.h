#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>

#include "Assets/Shader/HIKARI_ShaderAssetResolver.h"
#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::ASSETS::SHADER {

    inline constexpr std::string_view kRender2DBuiltinEngineInterfaceId =
        "HIKARI.Render2D.Builtin";
    inline constexpr uint32_t kRender2DBuiltinEngineInterfaceVersion = 1u;
    inline constexpr std::string_view kRender2DBuiltinEngineInterfaceHash =
        "4b0ba8ed3f6c38b45dcaef7b3cb5e5986f56a7d42b74de41848f320a0606bf37";
    inline constexpr std::string_view kRender2DBuiltinInterfaceDependency =
        "EngineSdk/Include/Render2D/HIKARI_Render2DBuiltinInterface.hlsli";
    inline constexpr std::string_view kRender2DBuiltinRootLayoutId =
        "HIKARI.Render2D.Builtin.Root";
    inline constexpr uint32_t kRender2DBuiltinRootLayoutVersion = 1u;
    inline constexpr std::string_view kRender2DBuiltinRootLayoutHash =
        "10eec9a9b4514842837498f655e1ddfd0f1d02fccf04aece3b508f709dd1366c";
    inline constexpr std::string_view kRender2DBuiltinRootLayoutCanonical =
        "HIKARI.Render2D.Builtin.Root.v1|"
        "0:CBV(b0,space0,All)|"
        "1:DescriptorTable(SRV(t0,space0,count1,offset0),All)|"
        "2:CBV(b1,space0,All)|"
        "StaticSampler(s0,space0,LinearWrap,Pixel)|"
        "Pipeline(Graphics)|Flags(AllowInputAssembler)";

    inline ShaderAssetContractExpectation MakeRender2DBuiltinShaderContract(
        ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
        ShaderAssetContractExpectation contract{};
        contract.engineInterfaceId = kRender2DBuiltinEngineInterfaceId;
        contract.engineInterfaceVersion =
            kRender2DBuiltinEngineInterfaceVersion;
        contract.rootLayoutId = kRender2DBuiltinRootLayoutId;
        contract.rootLayoutVersion = kRender2DBuiltinRootLayoutVersion;
        contract.availableDeviceCaps = availableCaps;
        HASH::Sha256Digest interfaceHash{};
        HASH::Sha256Digest rootLayoutHash{};
        HASH::Sha256Digest computedRootLayoutHash{};
        if (!HASH::ParseSha256(
                kRender2DBuiltinEngineInterfaceHash,
                interfaceHash) ||
            !HASH::ParseSha256(
                kRender2DBuiltinRootLayoutHash,
                rootLayoutHash) ||
            !HASH::ComputeSha256(
                kRender2DBuiltinRootLayoutCanonical,
                computedRootLayoutHash) ||
            computedRootLayoutHash != rootLayoutHash) {
            return {};
        }
        contract.engineInterfaceHash = interfaceHash;
        contract.rootLayoutHash = rootLayoutHash;
        return contract;
    }

    namespace DETAIL {

        struct Render2DBindingExpectation {
            ShaderResourceBindingKind kind{};
            uint32_t registerIndex = 0u;
            uint32_t visibilityMask = 0u;
        };

        inline const ShaderCookedEntry* FindRender2DEntry(
            const ShaderCookedArtifact& artifact,
            std::string_view id) noexcept {
            for (const ShaderCookedEntry& entry : artifact.entries) {
                if (entry.entry.id == id) {
                    return &entry;
                }
            }
            return nullptr;
        }

        inline bool MatchesRender2DBindings(
            const ShaderCookedEntry& entry,
            std::span<const Render2DBindingExpectation> expected) noexcept {
            if (entry.bindings.size() != expected.size()) {
                return false;
            }
            for (const Render2DBindingExpectation& requirement : expected) {
                bool matched = false;
                for (const ShaderResourceBinding& binding : entry.bindings) {
                    matched = matched ||
                        (binding.kind == requirement.kind &&
                         binding.registerIndex == requirement.registerIndex &&
                         binding.registerSpace == 0u &&
                         binding.count == 1u &&
                         binding.visibilityMask ==
                            requirement.visibilityMask);
                }
                if (!matched) {
                    return false;
                }
            }
            return true;
        }

        inline bool MatchesRender2DEntry(
            const ShaderCookedEntry* entry,
            std::string_view id,
            ShaderStage stage,
            std::string_view profile,
            std::span<const Render2DBindingExpectation> bindings) noexcept {
            return entry != nullptr && entry->entry.id == id &&
                entry->entry.entry == id && entry->entry.stage == stage &&
                entry->entry.profile == profile &&
                MatchesRender2DBindings(*entry, bindings);
        }

    } // namespace DETAIL

    inline bool ValidateRender2DBuiltinShaderArtifact(
        const ShaderCookedArtifact& artifact,
        std::string& outDiagnostic) {
        const ShaderAssetContractExpectation expected =
            MakeRender2DBuiltinShaderContract({
                artifact.requiredCaps.minimumShaderModel,
                artifact.requiredCaps.minimumRootSignatureVersion,
                artifact.requiredCaps.requiredFeatureMask
            });
        if (expected.engineInterfaceHash.IsZero() ||
            expected.rootLayoutHash.IsZero() ||
            artifact.programId !=
                "HIKARI.EngineShader.Render2D_Builtin" ||
            artifact.engineInterfaceId != expected.engineInterfaceId ||
            artifact.engineInterfaceVersion !=
                expected.engineInterfaceVersion ||
            artifact.engineInterfaceHash != expected.engineInterfaceHash ||
            artifact.rootLayoutId != expected.rootLayoutId ||
            artifact.rootLayoutVersion != expected.rootLayoutVersion ||
            artifact.rootLayoutHash != expected.rootLayoutHash ||
            artifact.requiredCaps.minimumShaderModel != 60u ||
            artifact.requiredCaps.minimumRootSignatureVersion != 10u ||
            artifact.requiredCaps.requiredFeatureMask != 0u ||
            artifact.entries.size() != 5u) {
            outDiagnostic = "Render2D built-in artifact identity is invalid";
            return false;
        }
        bool ownsInterface = false;
        for (const ShaderSourceDependency& dependency :
            artifact.dependencies) {
            ownsInterface = ownsInterface ||
                (dependency.logicalPath ==
                    kRender2DBuiltinInterfaceDependency &&
                 dependency.contentHash == expected.engineInterfaceHash);
        }
        const uint32_t vertexVisibility =
            1u << static_cast<uint32_t>(ShaderStage::Vertex);
        const uint32_t pixelVisibility =
            1u << static_cast<uint32_t>(ShaderStage::Pixel);
        const std::array vertexBindings{
            DETAIL::Render2DBindingExpectation{
                ShaderResourceBindingKind::ConstantBuffer,
                0u,
                vertexVisibility }
        };
        const std::array pixelBindings{
            DETAIL::Render2DBindingExpectation{
                ShaderResourceBindingKind::ShaderResource,
                0u,
                pixelVisibility },
            DETAIL::Render2DBindingExpectation{
                ShaderResourceBindingKind::Sampler,
                0u,
                pixelVisibility }
        };
        const std::array maskBindings{
            DETAIL::Render2DBindingExpectation{
                ShaderResourceBindingKind::ConstantBuffer,
                1u,
                pixelVisibility },
            DETAIL::Render2DBindingExpectation{
                ShaderResourceBindingKind::ShaderResource,
                0u,
                pixelVisibility },
            DETAIL::Render2DBindingExpectation{
                ShaderResourceBindingKind::Sampler,
                0u,
                pixelVisibility }
        };
        const std::span<const DETAIL::Render2DBindingExpectation> noBindings{};
        const bool validEntries =
            DETAIL::MatchesRender2DEntry(
                DETAIL::FindRender2DEntry(artifact, "LinePS"),
                "LinePS", ShaderStage::Pixel, "ps_6_0", noBindings) &&
            DETAIL::MatchesRender2DEntry(
                DETAIL::FindRender2DEntry(artifact, "LineVS"),
                "LineVS", ShaderStage::Vertex, "vs_6_0", vertexBindings) &&
            DETAIL::MatchesRender2DEntry(
                DETAIL::FindRender2DEntry(artifact, "MaskPS"),
                "MaskPS", ShaderStage::Pixel, "ps_6_0", maskBindings) &&
            DETAIL::MatchesRender2DEntry(
                DETAIL::FindRender2DEntry(artifact, "MeshPS"),
                "MeshPS", ShaderStage::Pixel, "ps_6_0", pixelBindings) &&
            DETAIL::MatchesRender2DEntry(
                DETAIL::FindRender2DEntry(artifact, "MeshVS"),
                "MeshVS", ShaderStage::Vertex, "vs_6_0", vertexBindings);
        if (!ownsInterface || !validEntries) {
            outDiagnostic =
                "Render2D built-in reflection does not match its root layout";
            return false;
        }
        outDiagnostic.clear();
        return true;
    }

} // namespace HIKARI::ASSETS::SHADER
