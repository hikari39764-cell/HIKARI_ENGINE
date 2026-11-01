#pragma once

#include <span>
#include <string_view>

#include "RenderContracts/Shader/HIKARI_ShaderAssetResolver.h"
#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::ASSETS::SHADER {

// エンジン側が固定する契約。rootLayoutHash が空の場合は canonical 表記から計算する。
struct ShaderContractIdentity {
    std::string_view interfaceId;
    uint32_t interfaceVersion = 1u;
    std::string_view interfaceHash;
    std::string_view rootLayoutId;
    uint32_t rootLayoutVersion = 1u;
    std::string_view rootLayoutHash;
    std::string_view rootLayoutCanonical;
};

struct ShaderBindingExpectation {
    ShaderResourceBindingKind kind = ShaderResourceBindingKind::ShaderResource;
    uint32_t registerIndex = 0u;
    uint32_t visibilityMask = 0u;
    uint32_t registerSpace = 0u;
    uint32_t count = 1u;
};

constexpr uint32_t ShaderStageMask(ShaderStage stage) noexcept { return 1u << static_cast<uint32_t>(stage); }

// 固定ハッシュと canonical 表記が一致しない契約は空（無効）を返す。
inline ShaderAssetContractExpectation MakeShaderContract(const ShaderContractIdentity& identity,
                                                         ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    ShaderAssetContractExpectation contract{};
    HASH::Sha256Digest computedRootLayout{};
    if (!HASH::ParseSha256(identity.interfaceHash, contract.engineInterfaceHash) ||
        !HASH::ComputeSha256(identity.rootLayoutCanonical, computedRootLayout)) {
        return {};
    }
    if (identity.rootLayoutHash.empty()) {
        contract.rootLayoutHash = computedRootLayout;
    } else if (!HASH::ParseSha256(identity.rootLayoutHash, contract.rootLayoutHash) ||
               contract.rootLayoutHash != computedRootLayout) {
        return {};
    }
    contract.engineInterfaceId = identity.interfaceId;
    contract.engineInterfaceVersion = identity.interfaceVersion;
    contract.rootLayoutId = identity.rootLayoutId;
    contract.rootLayoutVersion = identity.rootLayoutVersion;
    contract.availableDeviceCaps = availableCaps;
    return contract;
}

// インターフェース・ルート・必要機能（SM6.0 / RS1.0 / 追加機能なし）が契約どおりか
inline bool MatchesShaderContract(const ShaderCookedArtifact& artifact,
                                  const ShaderAssetContractExpectation& expected) noexcept {
    return !expected.engineInterfaceHash.IsZero() && !expected.rootLayoutHash.IsZero() &&
           artifact.engineInterfaceId == expected.engineInterfaceId &&
           artifact.engineInterfaceVersion == expected.engineInterfaceVersion &&
           artifact.engineInterfaceHash == expected.engineInterfaceHash &&
           artifact.rootLayoutId == expected.rootLayoutId && artifact.rootLayoutVersion == expected.rootLayoutVersion &&
           artifact.rootLayoutHash == expected.rootLayoutHash && artifact.requiredCaps.minimumShaderModel == 60u &&
           artifact.requiredCaps.minimumRootSignatureVersion == 10u && artifact.requiredCaps.requiredFeatureMask == 0u;
}

// 契約の .hlsli を同じ内容で取り込んでいるか
inline bool DependsOnShaderInterface(const ShaderCookedArtifact& artifact, std::string_view logicalPath,
                                     const HASH::Sha256Digest& interfaceHash) noexcept {
    for (const ShaderSourceDependency& dependency : artifact.dependencies) {
        if (dependency.logicalPath == logicalPath && dependency.contentHash == interfaceHash) {
            return true;
        }
    }
    return false;
}

inline bool MatchesBinding(const ShaderResourceBinding& binding, const ShaderBindingExpectation& expected) noexcept {
    return binding.kind == expected.kind && binding.registerIndex == expected.registerIndex &&
           binding.registerSpace == expected.registerSpace && binding.count == expected.count &&
           binding.visibilityMask == expected.visibilityMask;
}

// 反映されたバインディングが期待集合と過不足なく一致するか（順不同）
inline bool HasExactBindings(const ShaderCookedEntry& entry,
                             std::span<const ShaderBindingExpectation> expected) noexcept {
    if (entry.bindings.size() != expected.size()) {
        return false;
    }
    for (const ShaderBindingExpectation& requirement : expected) {
        size_t matches = 0u;
        for (const ShaderResourceBinding& binding : entry.bindings) {
            matches += MatchesBinding(binding, requirement) ? 1u : 0u;
        }
        if (matches != 1u) {
            return false;
        }
    }
    return true;
}

inline bool HasBinding(const ShaderCookedEntry& entry, const ShaderBindingExpectation& expected) noexcept {
    for (const ShaderResourceBinding& binding : entry.bindings) {
        if (MatchesBinding(binding, expected)) {
            return true;
        }
    }
    return false;
}

inline bool MatchesEntryPoint(const ShaderCookedEntry& entry, std::string_view id, std::string_view name,
                              ShaderStage stage, std::string_view profile) noexcept {
    return entry.entry.id == id && entry.entry.entry == name && entry.entry.stage == stage &&
           entry.entry.profile == profile;
}

} // namespace HIKARI::ASSETS::SHADER
