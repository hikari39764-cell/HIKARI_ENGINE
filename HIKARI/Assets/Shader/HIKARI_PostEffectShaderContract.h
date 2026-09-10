#pragma once

#include <string>
#include <string_view>

#include "Assets/Shader/HIKARI_ShaderAssetResolver.h"
#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::ASSETS::SHADER {

    inline constexpr std::string_view kPostEffectEngineInterfaceId =
        "HIKARI.PostEffect.Common";
    inline constexpr uint32_t kPostEffectEngineInterfaceVersion = 2u;
    inline constexpr std::string_view kPostEffectEngineInterfaceHash =
        "3210d64ed24339581f1173a98ba739837860adae27388299528ccc572bae6ee2";
    inline constexpr std::string_view kPostEffectInterfaceDependency =
        "EngineSdk/Include/Post/HIKARI_PostEffectInterface.hlsli";
    inline constexpr std::string_view kPostEffectRootLayoutId =
        "HIKARI.PostQuad.Root";
    inline constexpr uint32_t kPostEffectRootLayoutVersion = 2u;
    inline constexpr std::string_view kPostEffectRootLayoutHash =
        "7eeba21bdb7d081bf856331d342b65bc573df2808a5112033ed9daac643e9d94";
    inline constexpr std::string_view kPostEffectRootLayoutCanonical =
        "HIKARI.PostQuad.Root.v2|"
        "0:SRVTable(t0,space0,count1,Pixel)|"
        "1:CBV(b0,space0,Pixel)|"
        "2:Constants(b1,space0,count36,Pixel)|"
        "StaticSampler(s0,space0,LinearClamp,Pixel)|"
        "Flags(AllowInputAssembler)";

    inline ShaderAssetContractExpectation MakePostEffectShaderContract(
        ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
        ShaderAssetContractExpectation contract{};
        contract.engineInterfaceId = kPostEffectEngineInterfaceId;
        contract.engineInterfaceVersion = kPostEffectEngineInterfaceVersion;
        contract.rootLayoutId = kPostEffectRootLayoutId;
        contract.rootLayoutVersion = kPostEffectRootLayoutVersion;
        contract.availableDeviceCaps = availableCaps;
        HASH::Sha256Digest interfaceHash{};
        HASH::Sha256Digest rootLayoutHash{};
        HASH::Sha256Digest computedRootLayoutHash{};
        if (!HASH::ParseSha256(kPostEffectEngineInterfaceHash, interfaceHash) ||
            !HASH::ParseSha256(kPostEffectRootLayoutHash, rootLayoutHash) ||
            !HASH::ComputeSha256(
                kPostEffectRootLayoutCanonical,
                computedRootLayoutHash) ||
            computedRootLayoutHash != rootLayoutHash) {
            return {};
        }
        contract.engineInterfaceHash = interfaceHash;
        contract.rootLayoutHash = rootLayoutHash;
        return contract;
    }

    inline bool ValidatePostEffectShaderArtifact(
        const ShaderCookedArtifact& artifact,
        std::string& outDiagnostic) {
        const ShaderAssetContractExpectation expected =
            MakePostEffectShaderContract({
                artifact.requiredCaps.minimumShaderModel,
                artifact.requiredCaps.minimumRootSignatureVersion,
                artifact.requiredCaps.requiredFeatureMask
            });
        if (expected.engineInterfaceHash.IsZero() ||
            expected.rootLayoutHash.IsZero() ||
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
            artifact.entries.empty()) {
            outDiagnostic = "PostEffect artifact identity is invalid";
            return false;
        }
        bool ownsInterface = false;
        for (const ShaderSourceDependency& dependency :
            artifact.dependencies) {
            ownsInterface = ownsInterface ||
                (dependency.logicalPath == kPostEffectInterfaceDependency &&
                 dependency.contentHash == expected.engineInterfaceHash);
        }
        if (!ownsInterface) {
            outDiagnostic =
                "PostEffect artifact is missing its canonical interface";
            return false;
        }
        const uint32_t pixelVisibility =
            1u << static_cast<uint32_t>(ShaderStage::Pixel);
        for (const ShaderCookedEntry& entry : artifact.entries) {
            bool hasConstants = false;
            bool hasTexture = false;
            bool hasSampler = false;
            bool hasReadability = false;
            for (const ShaderResourceBinding& binding : entry.bindings) {
                hasReadability = hasReadability || (binding.registerIndex == 1u &&
                    binding.registerSpace == 0u && binding.count == 1u &&
                    binding.visibilityMask == pixelVisibility &&
                    binding.kind == ShaderResourceBindingKind::ConstantBuffer);
                const bool common = binding.registerIndex == 0u &&
                    binding.registerSpace == 0u && binding.count == 1u &&
                    binding.visibilityMask == pixelVisibility;
                hasConstants = hasConstants ||
                    (common && binding.kind ==
                        ShaderResourceBindingKind::ConstantBuffer);
                hasTexture = hasTexture ||
                    (common && binding.kind ==
                        ShaderResourceBindingKind::ShaderResource);
                hasSampler = hasSampler ||
                    (common && binding.kind ==
                        ShaderResourceBindingKind::Sampler);
            }
            if (entry.entry.stage != ShaderStage::Pixel ||
                entry.entry.profile != "ps_6_0" ||
                entry.bindings.size() != (hasReadability ? 4u : 3u) || !hasConstants ||
                !hasTexture || !hasSampler) {
                outDiagnostic =
                    "PostEffect reflection must contain exact b0, t0 and s0";
                return false;
            }
        }
        outDiagnostic.clear();
        return true;
    }

} // namespace HIKARI::ASSETS::SHADER
