#include "Assets/Shader/HIKARI_ShaderSourceManifest.h"

#include <algorithm>
#include <unordered_set>

#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::ASSETS::SHADER {
    namespace {

        bool IsIdentifier(
            std::string_view value,
            bool allowStablePunctuation) noexcept {
            if (value.empty() ||
                value.size() > kMaximumShaderIdentifierBytes) {
                return false;
            }
            for (size_t index = 0u; index < value.size(); ++index) {
                const char character = value[index];
                const bool asciiAlpha =
                    (character >= 'A' && character <= 'Z') ||
                    (character >= 'a' && character <= 'z');
                const bool digit = character >= '0' && character <= '9';
                const bool alphaNumeric = asciiAlpha || digit;
                const bool common = character == '_';
                const bool stablePunctuation =
                    allowStablePunctuation &&
                    (character == '.' || character == '-');
                if ((!alphaNumeric && !common && !stablePunctuation) ||
                    (index == 0u && digit)) {
                    return false;
                }
            }
            return true;
        }

        bool IsSafeRelativeShaderPath(
            const std::filesystem::path& path) {
            if (path.empty() || path.is_absolute() ||
                path.has_root_name() || path.has_root_directory()) {
                return false;
            }
            for (const std::filesystem::path& part : path) {
                if (part == "..") {
                    return false;
                }
            }
            const std::filesystem::path normalized =
                path.lexically_normal();
            const std::string generic = normalized.generic_string();
            if (generic.empty() ||
                generic.size() > kMaximumShaderPathBytes ||
                generic.find(':') != std::string::npos) {
                return false;
            }
            return true;
        }

        std::string_view ExpectedProfile(
            ShaderStage stage) noexcept {
            switch (stage) {
            case ShaderStage::Vertex: return "vs_6_0";
            case ShaderStage::Pixel: return "ps_6_0";
            case ShaderStage::Compute: return "cs_6_0";
            case ShaderStage::Amplification: return "as_6_5";
            case ShaderStage::Mesh: return "ms_6_5";
            case ShaderStage::Count: break;
            }
            return {};
        }

        bool ValidateProfile(
            ShaderStage stage,
            std::string_view profile,
            uint32_t& outModel) noexcept {
            const std::string_view expected = ExpectedProfile(stage);
            if (expected.empty() || profile != expected) {
                return false;
            }
            outModel = stage == ShaderStage::Mesh ||
                stage == ShaderStage::Amplification
                ? 65u
                : 60u;
            return true;
        }

    } // namespace

    const char* ToString(ShaderStage stage) noexcept {
        switch (stage) {
        case ShaderStage::Vertex: return "Vertex";
        case ShaderStage::Pixel: return "Pixel";
        case ShaderStage::Compute: return "Compute";
        case ShaderStage::Amplification: return "Amplification";
        case ShaderStage::Mesh: return "Mesh";
        case ShaderStage::Count: break;
        }
        return "Unknown";
    }

    bool ParseShaderStage(
        std::string_view text,
        ShaderStage& outStage) noexcept {
        for (uint32_t index = 0u;
            index < static_cast<uint32_t>(ShaderStage::Count);
            ++index) {
            const ShaderStage stage = static_cast<ShaderStage>(index);
            if (text == ToString(stage)) {
                outStage = stage;
                return true;
            }
        }
        return false;
    }

    const char* ToString(ShaderIncludeRoot root) noexcept {
        switch (root) {
        case ShaderIncludeRoot::Project: return "Project";
        case ShaderIncludeRoot::EngineSdk: return "EngineSdk";
        case ShaderIncludeRoot::Count: break;
        }
        return "Unknown";
    }

    bool ParseShaderIncludeRoot(
        std::string_view text,
        ShaderIncludeRoot& outRoot) noexcept {
        for (uint32_t index = 0u;
            index < static_cast<uint32_t>(ShaderIncludeRoot::Count);
            ++index) {
            const ShaderIncludeRoot root =
                static_cast<ShaderIncludeRoot>(index);
            if (text == ToString(root)) {
                outRoot = root;
                return true;
            }
        }
        return false;
    }

    bool ValidateShaderSourceManifest(
        const ShaderSourceManifest& manifest,
        std::string& outError) {
        if (manifest.version != kShaderSourceManifestVersion) {
            outError = "unsupported shader source manifest version";
            return false;
        }
        if (!IsIdentifier(manifest.programId, true) ||
            !IsIdentifier(manifest.engineInterfaceId, true) ||
            !IsIdentifier(manifest.rootLayoutId, true) ||
            manifest.engineInterfaceVersion == 0u) {
            outError = "shader program/interface/root-layout id is invalid";
            return false;
        }
        if (!IsSafeRelativeShaderPath(manifest.sourcePath) ||
            TEXT::ToLowerAsciiCopy(
                manifest.sourcePath.extension().string()) != ".hlsl") {
            outError = "shader source must be a safe relative .hlsl path";
            return false;
        }
        if (manifest.entries.empty() ||
            manifest.entries.size() > kMaximumShaderEntryCount ||
            manifest.defines.size() > kMaximumShaderDefineCount ||
            manifest.includeRoots.size() >
                static_cast<size_t>(ShaderIncludeRoot::Count)) {
            outError = "shader entry, define, or include-root count is invalid";
            return false;
        }
        if ((manifest.requiredCaps.requiredFeatureMask &
             ~ShaderDeviceFeature::All) != 0u ||
            (manifest.requiredCaps.minimumShaderModel != 60u &&
             manifest.requiredCaps.minimumShaderModel != 65u) ||
            (manifest.requiredCaps.minimumRootSignatureVersion != 10u &&
             manifest.requiredCaps.minimumRootSignatureVersion != 11u)) {
            outError = "shader device capability contract is invalid";
            return false;
        }

        std::unordered_set<std::string> entryIds{};
        uint32_t highestShaderModel = 60u;
        bool usesMeshPipeline = false;
        for (const ShaderEntryPoint& entry : manifest.entries) {
            uint32_t shaderModel = 0u;
            if (entry.stage >= ShaderStage::Count ||
                !IsIdentifier(entry.id, true) ||
                !IsIdentifier(entry.entry, false) ||
                !ValidateProfile(
                    entry.stage,
                    entry.profile,
                    shaderModel) ||
                !entryIds.insert(entry.id).second) {
                outError = "shader entry id, symbol, stage, or profile is invalid";
                return false;
            }
            usesMeshPipeline = usesMeshPipeline ||
                entry.stage == ShaderStage::Mesh ||
                entry.stage == ShaderStage::Amplification;
            highestShaderModel = (std::max)(
                highestShaderModel,
                shaderModel);
        }
        if (manifest.requiredCaps.minimumShaderModel <
                highestShaderModel ||
            (usesMeshPipeline &&
             (manifest.requiredCaps.requiredFeatureMask &
              ShaderDeviceFeature::MeshShader) == 0u)) {
            outError = "shader entry requirements exceed device capabilities";
            return false;
        }

        std::unordered_set<std::string> defineNames{};
        for (const ShaderDefine& define : manifest.defines) {
            if (!IsIdentifier(define.name, false) ||
                define.value.size() > kMaximumShaderDefineValueBytes ||
                define.value.find('\0') != std::string::npos ||
                !defineNames.insert(define.name).second) {
                outError = "shader define is invalid or duplicated";
                return false;
            }
        }

        uint32_t includeRootMask = 0u;
        for (ShaderIncludeRoot root : manifest.includeRoots) {
            const uint32_t index = static_cast<uint32_t>(root);
            if (root >= ShaderIncludeRoot::Count ||
                (includeRootMask & (1u << index)) != 0u) {
                outError = "shader include root is invalid or duplicated";
                return false;
            }
            includeRootMask |= 1u << index;
        }
        outError.clear();
        return true;
    }

} // namespace HIKARI::ASSETS::SHADER
