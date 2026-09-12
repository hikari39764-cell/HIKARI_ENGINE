#pragma once

#include <array>
#include <string>
#include <string_view>

#include "Assets/Shader/HIKARI_ShaderAssetResolver.h"
#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::ASSETS::SHADER {

    inline constexpr std::string_view kTemporalEngineInterfaceId = "HIKARI.Temporal";
    inline constexpr std::string_view kTemporalInterfaceDependency =
        "EngineSdk/Include/Temporal/HIKARI_TemporalShaderInterface.hlsli";
    inline constexpr std::string_view kTemporalEngineInterfaceHash =
        "de3b1faf6870cd737485e21ae917687558ebcf884f14089ed826e482e32a31f0";

    enum class TemporalShaderPass : uint8_t {
        Geometry, Masks, MotionVector, TaaResolve, Count
    };

    struct TemporalShaderDefinition {
        std::string_view programId;
        std::string_view rootLayoutId;
        std::string_view rootCanonical;
    };

    inline constexpr std::array<TemporalShaderDefinition, 4> kTemporalShaders{{
        { "HIKARI.EngineShader.Temporal_GeometryMotion",
          "HIKARI.Temporal.Geometry.Root",
          "HIKARI.Temporal.Geometry.Root.v1|0:CBV(b0,space0,All)|1:CBV(b1,space0,All)|Pipeline(Graphics)|Flags(AllowInputAssembler)" },
        { "HIKARI.EngineShader.Temporal_Masks",
          "HIKARI.Temporal.Masks.Root",
          "HIKARI.Temporal.Masks.Root.v1|0:Table(SRV(t0,space0,count1),Pixel)|1:Table(SRV(t1,space0,count1),Pixel)|StaticSampler(s0,PointClamp,Pixel)|Pipeline(Graphics)|Flags(None)" },
        { "HIKARI.EngineShader.Temporal_MotionVector",
          "HIKARI.Temporal.MotionVector.Root",
          "HIKARI.Temporal.MotionVector.Root.v1|0:CBV(b0,space0,All)|1:Table(SRV(t0,space0,count1),Pixel)|Pipeline(Graphics)|Flags(None)" },
        { "HIKARI.EngineShader.Temporal_TaaResolve",
          "HIKARI.Temporal.TaaResolve.Root",
          "HIKARI.Temporal.TaaResolve.Root.v1|0:CBV(b0,space0,All)|1:Table(SRV(t0,space0,count1),Pixel)|2:Table(SRV(t1,space0,count1),Pixel)|3:Table(SRV(t2,space0,count1),Pixel)|4:Table(SRV(t3,space0,count1),Pixel)|5:Table(SRV(t4,space0,count1),Pixel)|6:Table(SRV(t5,space0,count1),Pixel)|7:Table(SRV(t6,space0,count1),Pixel)|8:Table(SRV(t7,space0,count1),Pixel)|9:Table(SRV(t8,space0,count1),Pixel)|10:Table(SRV(t9,space0,count1),Pixel)|StaticSampler(s0,LinearClamp,Pixel)|Pipeline(Graphics)|Flags(None)" },
    }};

    inline ShaderAssetContractExpectation MakeTemporalShaderContract(
        TemporalShaderPass pass,
        ShaderRuntimeDeviceCapabilities capabilities) {
        const auto index = static_cast<size_t>(pass);
        if (index >= kTemporalShaders.size()) {
            return {};
        }
        const auto& definition = kTemporalShaders[index];
        ShaderAssetContractExpectation result{};
        result.engineInterfaceId = kTemporalEngineInterfaceId;
        result.engineInterfaceVersion = 1u;
        result.rootLayoutId = definition.rootLayoutId;
        result.rootLayoutVersion = 1u;
        result.availableDeviceCaps = capabilities;
        if (!HASH::ParseSha256(kTemporalEngineInterfaceHash, result.engineInterfaceHash) ||
            !HASH::ComputeSha256(definition.rootCanonical, result.rootLayoutHash)) {
            return {};
        }
        return result;
    }

    inline bool ValidateTemporalShaderArtifact(
        const ShaderCookedArtifact& artifact, std::string& diagnostic) {
        size_t index = 0u;
        while (index < kTemporalShaders.size() &&
            kTemporalShaders[index].programId != artifact.programId) {
            ++index;
        }
        if (index == kTemporalShaders.size()) {
            diagnostic = "Unknown Temporal shader program";
            return false;
        }
        const auto pass = static_cast<TemporalShaderPass>(index);
        const auto expected = MakeTemporalShaderContract(pass, {60u, 10u, 0u});
        if (artifact.engineInterfaceId != expected.engineInterfaceId ||
            artifact.engineInterfaceVersion != expected.engineInterfaceVersion ||
            artifact.engineInterfaceHash != expected.engineInterfaceHash ||
            artifact.rootLayoutId != expected.rootLayoutId ||
            artifact.rootLayoutVersion != expected.rootLayoutVersion ||
            artifact.rootLayoutHash != expected.rootLayoutHash ||
            artifact.requiredCaps.minimumShaderModel != 60u ||
            artifact.requiredCaps.minimumRootSignatureVersion != 10u ||
            artifact.requiredCaps.requiredFeatureMask != 0u ||
            artifact.entries.size() != (pass == TemporalShaderPass::Geometry ? 3u : 2u)) {
            diagnostic = "Temporal shader identity or root contract mismatch";
            return false;
        }
        bool ownsInterface = false;
        for (const auto& dependency : artifact.dependencies) {
            ownsInterface |= dependency.logicalPath == kTemporalInterfaceDependency &&
                dependency.contentHash == expected.engineInterfaceHash;
        }
        if (!ownsInterface) {
            diagnostic = "Temporal shader interface dependency mismatch";
            return false;
        }
        uint32_t entryMask = 0u;
        for (const auto& entry : artifact.entries) {
            const bool pixel = entry.entry.id == "PSMain";
            const bool skinned = pass == TemporalShaderPass::Geometry &&
                entry.entry.id == "VSSkinned";
            const bool vertex = entry.entry.id ==
                (pass == TemporalShaderPass::Geometry ? "VSStatic" : "VSMain");
            const uint32_t bit = pixel ? 1u : (skinned ? 2u : 4u);
            if ((!pixel && !skinned && !vertex) || (entryMask & bit) != 0u ||
                entry.entry.entry != entry.entry.id ||
                entry.entry.stage != (pixel ? ShaderStage::Pixel : ShaderStage::Vertex) ||
                entry.entry.profile != (pixel ? "ps_6_0" : "vs_6_0")) {
                diagnostic = "Temporal shader entry mismatch";
                return false;
            }
            entryMask |= bit;
            std::vector<ShaderResourceBinding> bindings{};
            const auto add = [&](ShaderResourceBindingKind kind, uint32_t reg) {
                bindings.push_back({kind, reg, 0u, 1u,
                    1u << static_cast<uint32_t>(entry.entry.stage)});
            };
            if (pass == TemporalShaderPass::Geometry) {
                add(ShaderResourceBindingKind::ConstantBuffer, 0u);
                if (skinned) add(ShaderResourceBindingKind::ConstantBuffer, 1u);
            } else if (pixel) {
                if (pass != TemporalShaderPass::Masks) {
                    add(ShaderResourceBindingKind::ConstantBuffer, 0u);
                }
                const uint32_t count = pass == TemporalShaderPass::TaaResolve ? 10u :
                    (pass == TemporalShaderPass::Masks ? 2u : 1u);
                for (uint32_t reg = 0u; reg < count; ++reg) {
                    add(ShaderResourceBindingKind::ShaderResource, reg);
                }
                // TAA は Load による補間を使う。ルートに残る s0 は DXIL に現れない。
            }
            if (bindings.size() != entry.bindings.size()) {
                diagnostic = "Temporal shader reflection binding count mismatch";
                return false;
            }
            for (const auto& expectedBinding : bindings) {
                size_t matches = 0u;
                for (const auto& actual : entry.bindings) {
                    matches += actual.kind == expectedBinding.kind &&
                        actual.registerIndex == expectedBinding.registerIndex &&
                        actual.registerSpace == expectedBinding.registerSpace &&
                        actual.count == expectedBinding.count &&
                        actual.visibilityMask == expectedBinding.visibilityMask;
                }
                if (matches != 1u) {
                    diagnostic = "Temporal shader reflection binding mismatch";
                    return false;
                }
            }
        }
        diagnostic.clear();
        return true;
    }

} // namespace HIKARI::ASSETS::SHADER
