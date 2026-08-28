#include "Assets/Shader/HIKARI_ShaderCookedArtifact.h"

#include <algorithm>
#include <filesystem>
#include <tuple>

namespace HIKARI::ASSETS::SHADER {
    namespace {

        bool AppendU32(
            HASH::Sha256Hasher& hasher,
            uint32_t value) noexcept {
            const std::array<std::byte, 4u> bytes{
                static_cast<std::byte>(value & 0xffu),
                static_cast<std::byte>((value >> 8u) & 0xffu),
                static_cast<std::byte>((value >> 16u) & 0xffu),
                static_cast<std::byte>((value >> 24u) & 0xffu),
            };
            return hasher.Append(bytes);
        }

        bool AppendString(
            HASH::Sha256Hasher& hasher,
            std::string_view value) noexcept {
            return value.size() <= UINT32_MAX &&
                AppendU32(hasher, static_cast<uint32_t>(value.size())) &&
                hasher.Append(value);
        }

        bool AppendU64(
            HASH::Sha256Hasher& hasher,
            uint64_t value) noexcept {
            const std::array<std::byte, 8u> bytes{
                static_cast<std::byte>(value & 0xffu),
                static_cast<std::byte>((value >> 8u) & 0xffu),
                static_cast<std::byte>((value >> 16u) & 0xffu),
                static_cast<std::byte>((value >> 24u) & 0xffu),
                static_cast<std::byte>((value >> 32u) & 0xffu),
                static_cast<std::byte>((value >> 40u) & 0xffu),
                static_cast<std::byte>((value >> 48u) & 0xffu),
                static_cast<std::byte>((value >> 56u) & 0xffu),
            };
            return hasher.Append(bytes);
        }

        bool IsSafeLogicalDependencyPath(std::string_view text) {
            if (text.empty() || text.size() > kMaximumShaderPathBytes ||
                text.find('\0') != std::string_view::npos ||
                (text.rfind("Project/", 0u) != 0u &&
                 text.rfind("EngineSdk/", 0u) != 0u)) {
                return false;
            }
            const std::filesystem::path path(text);
            if (path.is_absolute() || path.has_root_name() ||
                path.has_root_directory() ||
                path.lexically_normal().generic_string() != text) {
                return false;
            }
            for (const std::filesystem::path& part : path) {
                if (part == "..") {
                    return false;
                }
            }
            return true;
        }

        bool BindingLess(
            const ShaderResourceBinding& lhs,
            const ShaderResourceBinding& rhs) noexcept {
            return std::tie(
                lhs.kind,
                lhs.registerSpace,
                lhs.registerIndex,
                lhs.count,
                lhs.visibilityMask) <
                std::tie(
                    rhs.kind,
                    rhs.registerSpace,
                    rhs.registerIndex,
                    rhs.count,
                    rhs.visibilityMask);
        }

        bool IsCanonicalDefineName(std::string_view name) noexcept {
            if (name.empty() ||
                name.size() > kMaximumShaderIdentifierBytes) {
                return false;
            }
            for (size_t index = 0u; index < name.size(); ++index) {
                const char character = name[index];
                const bool alpha =
                    (character >= 'A' && character <= 'Z') ||
                    (character >= 'a' && character <= 'z');
                const bool digit =
                    character >= '0' && character <= '9';
                if ((!alpha && !digit && character != '_') ||
                    (index == 0u && digit)) {
                    return false;
                }
            }
            return true;
        }

        bool HasCanonicalCompilerArguments(
            const ShaderCookedEntry& entry) noexcept {
            const auto& arguments = entry.compilerArguments;
            if (arguments.size() < 8u ||
                arguments[0] != "-E" ||
                arguments[1] != entry.entry.entry ||
                arguments[2] != "-T" ||
                arguments[3] != entry.entry.profile ||
                arguments[4] != "-HV" ||
                arguments[5] != "2021" ||
                arguments[6] != "-O3" ||
                arguments[7] != "-WX") {
                return false;
            }

            size_t index = 8u;
            int32_t previousIncludeRoot = -1;
            while (index < arguments.size() &&
                arguments[index] == "-I") {
                if (index + 1u >= arguments.size()) {
                    return false;
                }
                const int32_t includeRoot = arguments[index + 1u] ==
                        "Project"
                    ? 0
                    : arguments[index + 1u] == "EngineSdk"
                        ? 1
                        : -1;
                if (includeRoot <= previousIncludeRoot) {
                    return false;
                }
                previousIncludeRoot = includeRoot;
                index += 2u;
            }

            std::string_view previousDefine{};
            for (; index < arguments.size(); ++index) {
                const std::string_view argument = arguments[index];
                if (!argument.starts_with("-D")) {
                    return false;
                }
                const size_t equals = argument.find('=', 2u);
                if (equals == std::string_view::npos ||
                    !IsCanonicalDefineName(
                        argument.substr(2u, equals - 2u)) ||
                    argument.size() - equals - 1u >
                        kMaximumShaderDefineValueBytes) {
                    return false;
                }
                const std::string_view defineName =
                    argument.substr(2u, equals - 2u);
                if (!previousDefine.empty() &&
                    defineName <= previousDefine) {
                    return false;
                }
                previousDefine = defineName;
            }
            return true;
        }

    } // namespace

    bool ComputeShaderArgumentsHash(
        const std::vector<std::string>& arguments,
        HASH::Sha256Digest& outHash) noexcept {
        if (arguments.size() > kMaximumShaderArgumentCount) {
            return false;
        }
        HASH::Sha256Hasher hasher{};
        if (!hasher.IsReady() ||
            !AppendU32(hasher, static_cast<uint32_t>(arguments.size()))) {
            return false;
        }
        for (const std::string& argument : arguments) {
            if (argument.size() > kMaximumShaderArgumentBytes ||
                argument.find('\0') != std::string::npos ||
                !AppendString(hasher, argument)) {
                return false;
            }
        }
        return hasher.Finish(outHash);
    }

    bool ComputeShaderReflectionHash(
        const std::vector<ShaderResourceBinding>& bindings,
        HASH::Sha256Digest& outHash) noexcept {
        if (bindings.size() > kMaximumShaderBindingsPerEntry) {
            return false;
        }
        HASH::Sha256Hasher hasher{};
        if (!hasher.IsReady() ||
            !AppendU32(hasher, static_cast<uint32_t>(bindings.size()))) {
            return false;
        }
        for (const ShaderResourceBinding& binding : bindings) {
            if (!AppendU32(
                    hasher,
                    static_cast<uint32_t>(binding.kind)) ||
                !AppendU32(hasher, binding.registerIndex) ||
                !AppendU32(hasher, binding.registerSpace) ||
                !AppendU32(hasher, binding.count) ||
                !AppendU32(hasher, binding.visibilityMask)) {
                return false;
            }
        }
        return hasher.Finish(outHash);
    }

    bool ComputeShaderSourceClosureHash(
        const HASH::Sha256Digest& manifestHash,
        const std::vector<ShaderSourceDependency>& dependencies,
        HASH::Sha256Digest& outHash) noexcept {
        if (manifestHash.IsZero() || dependencies.empty() ||
            dependencies.size() > kMaximumShaderDependencyCount) {
            return false;
        }
        HASH::Sha256Hasher hasher{};
        if (!hasher.IsReady() ||
            !hasher.Append(std::as_bytes(std::span(
                manifestHash.bytes))) ||
            !AppendU32(
                hasher,
                static_cast<uint32_t>(dependencies.size()))) {
            return false;
        }
        std::string_view previous{};
        uint64_t totalDependencyBytes = 0u;
        for (const ShaderSourceDependency& dependency : dependencies) {
            if (!IsSafeLogicalDependencyPath(dependency.logicalPath) ||
                dependency.byteSize == 0u ||
                dependency.byteSize > kMaximumShaderDependencyBytes ||
                dependency.byteSize >
                    kMaximumShaderSourceClosureBytes -
                        totalDependencyBytes ||
                dependency.contentHash.IsZero() ||
                (!previous.empty() &&
                 dependency.logicalPath <= previous) ||
                !AppendString(hasher, dependency.logicalPath) ||
                !AppendU64(hasher, dependency.byteSize) ||
                !hasher.Append(std::as_bytes(std::span(
                    dependency.contentHash.bytes)))) {
                return false;
            }
            totalDependencyBytes += dependency.byteSize;
            previous = dependency.logicalPath;
        }
        return hasher.Finish(outHash);
    }

    bool ValidateShaderCookedArtifact(
        const ShaderCookedArtifact& artifact,
        std::string& outError) {
        if (artifact.version != kShaderCookedArtifactVersion ||
            !IsValidAssetGuid(artifact.sourceGuid.value) ||
            artifact.manifestHash.IsZero() ||
            artifact.sourceClosureHash.IsZero() ||
            artifact.engineInterfaceHash.IsZero() ||
            artifact.rootLayoutHash.IsZero() ||
            artifact.compiler.binaryHash.IsZero() ||
            artifact.compiler.validatorBinaryHash.IsZero() ||
            artifact.compiler.id != "DXC" ||
            artifact.compiler.contractVersion !=
                kShaderCompilerContractVersion ||
            artifact.compiler.version.empty() ||
            artifact.compiler.version.size() >
                kMaximumShaderIdentifierBytes ||
            artifact.compiler.version.find('\0') != std::string::npos) {
            outError = "shader artifact identity or provenance is invalid";
            return false;
        }
        if (artifact.dependencies.empty() ||
            artifact.dependencies.size() >
                kMaximumShaderDependencyCount ||
            artifact.entries.empty() ||
            artifact.entries.size() > kMaximumShaderEntryCount) {
            outError = "shader artifact dependency or entry count is invalid";
            return false;
        }

        std::string previousDependency{};
        uint64_t totalDependencyBytes = 0u;
        for (const ShaderSourceDependency& dependency :
            artifact.dependencies) {
            if (!IsSafeLogicalDependencyPath(dependency.logicalPath) ||
                dependency.byteSize == 0u ||
                dependency.byteSize > kMaximumShaderDependencyBytes ||
                dependency.byteSize >
                    kMaximumShaderSourceClosureBytes -
                        totalDependencyBytes ||
                dependency.contentHash.IsZero() ||
                (!previousDependency.empty() &&
                 dependency.logicalPath <= previousDependency)) {
                outError = "shader source closure is invalid or unsorted";
                return false;
            }
            totalDependencyBytes += dependency.byteSize;
            previousDependency = dependency.logicalPath;
        }
        HASH::Sha256Digest computedClosure{};
        if (!ComputeShaderSourceClosureHash(
                artifact.manifestHash,
                artifact.dependencies,
                computedClosure) ||
            computedClosure != artifact.sourceClosureHash) {
            outError = "shader source closure hash mismatch";
            return false;
        }

        ShaderSourceManifest compatibility{};
        compatibility.programId = artifact.programId;
        compatibility.sourcePath = "Cooked.hlsl";
        compatibility.engineInterfaceId = artifact.engineInterfaceId;
        compatibility.engineInterfaceVersion =
            artifact.engineInterfaceVersion;
        compatibility.engineInterfaceHash =
            artifact.engineInterfaceHash;
        compatibility.rootLayoutId = artifact.rootLayoutId;
        compatibility.rootLayoutVersion =
            artifact.rootLayoutVersion;
        compatibility.rootLayoutHash = artifact.rootLayoutHash;
        compatibility.requiredCaps = artifact.requiredCaps;
        compatibility.includeRoots = {
            ShaderIncludeRoot::Project,
            ShaderIncludeRoot::EngineSdk
        };
        compatibility.entries.reserve(artifact.entries.size());
        for (const ShaderCookedEntry& entry : artifact.entries) {
            compatibility.entries.push_back(entry.entry);
        }
        if (!ValidateShaderSourceManifest(compatibility, outError)) {
            return false;
        }

        size_t totalDxilBytes = 0u;
        std::string previousEntry{};
        const uint32_t validVisibilityMask =
            (1u << static_cast<uint32_t>(ShaderStage::Count)) - 1u;
        for (const ShaderCookedEntry& entry : artifact.entries) {
            if ((!previousEntry.empty() &&
                entry.entry.id <= previousEntry) ||
                entry.compilerArguments.empty() ||
                entry.compilerArguments.size() >
                    kMaximumShaderArgumentCount ||
                !HasCanonicalCompilerArguments(entry) ||
                entry.bindings.size() >
                    kMaximumShaderBindingsPerEntry ||
                entry.dxil.size() < 4u ||
                entry.dxil.size() > kMaximumShaderDxilBytesPerEntry ||
                entry.dxil[0] != 'D' || entry.dxil[1] != 'X' ||
                entry.dxil[2] != 'B' || entry.dxil[3] != 'C') {
                outError = "shader cooked entry ordering or DXIL is invalid";
                return false;
            }
            previousEntry = entry.entry.id;

            HASH::Sha256Digest computed{};
            if (!ComputeShaderArgumentsHash(
                    entry.compilerArguments,
                    computed) ||
                computed != entry.compilerArgumentsHash) {
                outError = "shader compiler argument hash mismatch";
                return false;
            }
            if (!std::is_sorted(
                    entry.bindings.begin(),
                    entry.bindings.end(),
                    BindingLess) ||
                !ComputeShaderReflectionHash(entry.bindings, computed) ||
                computed != entry.reflectionHash) {
                outError = "shader reflection table or hash is invalid";
                return false;
            }
            for (size_t bindingIndex = 0u;
                bindingIndex < entry.bindings.size();
                ++bindingIndex) {
                const ShaderResourceBinding& binding =
                    entry.bindings[bindingIndex];
                if (binding.kind >= ShaderResourceBindingKind::Count ||
                    binding.count == 0u || binding.visibilityMask == 0u ||
                    (binding.visibilityMask & ~validVisibilityMask) != 0u ||
                    (binding.visibilityMask &
                     (1u << static_cast<uint32_t>(
                        entry.entry.stage))) == 0u ||
                    binding.count - 1u > UINT32_MAX -
                        binding.registerIndex ||
                    (bindingIndex > 0u &&
                     !BindingLess(
                        entry.bindings[bindingIndex - 1u],
                        binding)) ||
                    (bindingIndex > 0u &&
                     entry.bindings[bindingIndex - 1u].kind ==
                        binding.kind &&
                     entry.bindings[bindingIndex - 1u].registerSpace ==
                        binding.registerSpace &&
                     binding.registerIndex <=
                        entry.bindings[bindingIndex - 1u].registerIndex +
                        entry.bindings[bindingIndex - 1u].count - 1u)) {
                    outError = "shader reflection binding is invalid";
                    return false;
                }
            }
            if (!HASH::ComputeSha256(
                    std::as_bytes(std::span<const uint8_t>(entry.dxil)),
                    computed) ||
                computed != entry.dxilHash) {
                outError = "shader DXIL hash mismatch";
                return false;
            }
            if (entry.dxil.size() >
                kMaximumShaderArtifactBytes - totalDxilBytes) {
                outError = "shader artifact DXIL budget exceeded";
                return false;
            }
            totalDxilBytes += entry.dxil.size();
        }
        outError.clear();
        return true;
    }

} // namespace HIKARI::ASSETS::SHADER
