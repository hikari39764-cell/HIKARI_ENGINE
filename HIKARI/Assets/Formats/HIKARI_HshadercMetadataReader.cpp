#include "Assets/Shader/Internal/HIKARI_ShaderJson.h"
#include "Assets/Formats/Internal/HIKARI_HshadercMetadata.h"
#include "Assets/Formats/HIKARI_HshadercFormat.h"

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <utility>

#include <json.hpp>

namespace HIKARI::ASSETS::SHADER::INTERNAL {
namespace {

template <class T>
bool ReadUnsigned(const nlohmann::json& object, const char* key, T& outValue) {
    static_assert(std::is_unsigned_v<T>);
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number_unsigned()) {
        return false;
    }
    const uint64_t value = found->get<uint64_t>();
    if (value > static_cast<uint64_t>((std::numeric_limits<T>::max)())) {
        return false;
    }
    outValue = static_cast<T>(value);
    return true;
}

bool IsZeroPadding(std::span<const uint8_t> payload, uint64_t begin, uint64_t end) noexcept {
    if (begin > end || end > payload.size()) {
        return false;
    }
    return std::all_of(payload.begin() + static_cast<size_t>(begin), payload.begin() + static_cast<size_t>(end),
                       [](uint8_t value) { return value == 0u; });
}

} // namespace

bool ParseHshadercMetadata(std::string_view jsonText, std::span<const uint8_t> payload,
                           ShaderCookedArtifact& outArtifact, std::string& outError) {
    if (jsonText.empty() || jsonText.size() > kMaximumHshadercMetadataBytes ||
        payload.size() > kMaximumShaderArtifactBytes) {
        outError = "shader cooked metadata or payload size is invalid";
        return false;
    }
    try {
        bool depthExceeded = false;
        const nlohmann::json root = JSON::Parse(jsonText, depthExceeded);
        if (depthExceeded || !JSON::HasOnlyKeys(root, {"schema", "version", "sourceGuid", "programId", "manifestHash",
                                                       "sourceClosureHash", "compiler", "engineInterface", "rootLayout",
                                                       "requiredCaps", "dependencies", "entries"})) {
            outError = "shader cooked metadata schema is invalid";
            return false;
        }

        std::string schema{};
        ShaderCookedArtifact artifact{};
        if (!JSON::ReadString(root, "schema", schema) || schema != "HIKARI.ShaderCookedArtifact" ||
            !ReadUnsigned(root, "version", artifact.version) ||
            !JSON::ReadString(root, "sourceGuid", artifact.sourceGuid.value) ||
            !JSON::ReadString(root, "programId", artifact.programId) ||
            !JSON::ReadHash(root, "manifestHash", artifact.manifestHash) ||
            !JSON::ReadHash(root, "sourceClosureHash", artifact.sourceClosureHash)) {
            outError = "shader cooked metadata identity is invalid";
            return false;
        }

        const auto compiler = root.find("compiler");
        const auto engineInterface = root.find("engineInterface");
        const auto rootLayout = root.find("rootLayout");
        const auto caps = root.find("requiredCaps");
        if (compiler == root.end() ||
            !JSON::HasOnlyKeys(*compiler, {"id", "version", "contractVersion", "binaryHash", "validatorBinaryHash"}) ||
            !JSON::ReadString(*compiler, "id", artifact.compiler.id) ||
            !JSON::ReadString(*compiler, "version", artifact.compiler.version) ||
            !ReadUnsigned(*compiler, "contractVersion", artifact.compiler.contractVersion) ||
            !JSON::ReadHash(*compiler, "binaryHash", artifact.compiler.binaryHash) ||
            !JSON::ReadHash(*compiler, "validatorBinaryHash", artifact.compiler.validatorBinaryHash) ||
            engineInterface == root.end() || !JSON::HasOnlyKeys(*engineInterface, {"id", "version", "hash"}) ||
            !JSON::ReadString(*engineInterface, "id", artifact.engineInterfaceId) ||
            !ReadUnsigned(*engineInterface, "version", artifact.engineInterfaceVersion) ||
            !JSON::ReadHash(*engineInterface, "hash", artifact.engineInterfaceHash) || rootLayout == root.end() ||
            !JSON::HasOnlyKeys(*rootLayout, {"id", "version", "hash"}) ||
            !JSON::ReadString(*rootLayout, "id", artifact.rootLayoutId) ||
            !ReadUnsigned(*rootLayout, "version", artifact.rootLayoutVersion) ||
            !JSON::ReadHash(*rootLayout, "hash", artifact.rootLayoutHash) || caps == root.end() ||
            !JSON::HasOnlyKeys(*caps, {"minimumShaderModel", "minimumRootSignatureVersion", "requiredFeatureMask"}) ||
            !ReadUnsigned(*caps, "minimumShaderModel", artifact.requiredCaps.minimumShaderModel) ||
            !ReadUnsigned(*caps, "minimumRootSignatureVersion", artifact.requiredCaps.minimumRootSignatureVersion) ||
            !ReadUnsigned(*caps, "requiredFeatureMask", artifact.requiredCaps.requiredFeatureMask)) {
            outError = "shader compiler/interface/root/caps metadata is invalid";
            return false;
        }

        const auto dependencies = root.find("dependencies");
        if (dependencies == root.end() || !dependencies->is_array() || dependencies->empty() ||
            dependencies->size() > kMaximumShaderDependencyCount) {
            outError = "shader dependency metadata is invalid";
            return false;
        }
        artifact.dependencies.reserve(dependencies->size());
        for (const nlohmann::json& item : *dependencies) {
            ShaderSourceDependency dependency{};
            if (!JSON::HasOnlyKeys(item, {"logicalPath", "byteSize", "contentHash"}) ||
                !JSON::ReadString(item, "logicalPath", dependency.logicalPath) ||
                !ReadUnsigned(item, "byteSize", dependency.byteSize) ||
                !JSON::ReadHash(item, "contentHash", dependency.contentHash)) {
                outError = "shader dependency record is invalid";
                return false;
            }
            artifact.dependencies.push_back(std::move(dependency));
        }

        const auto entries = root.find("entries");
        if (entries == root.end() || !entries->is_array() || entries->empty() ||
            entries->size() > kMaximumShaderEntryCount) {
            outError = "shader cooked entry metadata is invalid";
            return false;
        }
        artifact.entries.reserve(entries->size());
        std::vector<HshadercDxilRange> dxilRanges{};
        dxilRanges.reserve(entries->size());
        uint64_t previousEnd = 0u;
        for (const nlohmann::json& item : *entries) {
            if (!JSON::HasOnlyKeys(item,
                                   {"id", "stage", "entry", "profile", "compilerArguments", "compilerArgumentsHash",
                                    "reflectionHash", "dxilOffset", "dxilBytes", "dxilHash", "bindings"})) {
                outError = "shader cooked entry contains unknown fields";
                return false;
            }
            ShaderCookedEntry entry{};
            std::string stage{};
            uint64_t dxilOffset = 0u;
            uint64_t dxilBytes = 0u;
            if (!JSON::ReadString(item, "id", entry.entry.id) || !JSON::ReadString(item, "stage", stage) ||
                !ParseShaderStage(stage, entry.entry.stage) || !JSON::ReadString(item, "entry", entry.entry.entry) ||
                !JSON::ReadString(item, "profile", entry.entry.profile) ||
                !JSON::ReadHash(item, "compilerArgumentsHash", entry.compilerArgumentsHash) ||
                !JSON::ReadHash(item, "reflectionHash", entry.reflectionHash) ||
                !JSON::ReadHash(item, "dxilHash", entry.dxilHash) || !ReadUnsigned(item, "dxilOffset", dxilOffset) ||
                !ReadUnsigned(item, "dxilBytes", dxilBytes) || dxilOffset % 16u != 0u || dxilBytes < 4u ||
                dxilBytes > kMaximumShaderDxilBytesPerEntry || dxilOffset > payload.size() ||
                dxilBytes > payload.size() - dxilOffset || dxilOffset < previousEnd ||
                !IsZeroPadding(payload, previousEnd, dxilOffset)) {
                outError = "shader DXIL range metadata is invalid";
                return false;
            }

            const auto arguments = item.find("compilerArguments");
            const auto bindings = item.find("bindings");
            if (arguments == item.end() || !arguments->is_array() || arguments->empty() ||
                arguments->size() > kMaximumShaderArgumentCount || bindings == item.end() || !bindings->is_array() ||
                bindings->size() > kMaximumShaderBindingsPerEntry) {
                outError = "shader arguments or bindings are invalid";
                return false;
            }
            entry.compilerArguments.reserve(arguments->size());
            for (const nlohmann::json& argument : *arguments) {
                if (!argument.is_string()) {
                    outError = "shader compiler argument is not a string";
                    return false;
                }
                entry.compilerArguments.push_back(argument.get<std::string>());
            }
            entry.bindings.reserve(bindings->size());
            for (const nlohmann::json& bindingJson : *bindings) {
                ShaderResourceBinding binding{};
                uint32_t kind = 0u;
                if (!JSON::HasOnlyKeys(bindingJson,
                                       {"kind", "registerIndex", "registerSpace", "count", "visibilityMask"}) ||
                    !ReadUnsigned(bindingJson, "kind", kind) ||
                    kind >= static_cast<uint32_t>(ShaderResourceBindingKind::Count) ||
                    !ReadUnsigned(bindingJson, "registerIndex", binding.registerIndex) ||
                    !ReadUnsigned(bindingJson, "registerSpace", binding.registerSpace) ||
                    !ReadUnsigned(bindingJson, "count", binding.count) ||
                    !ReadUnsigned(bindingJson, "visibilityMask", binding.visibilityMask)) {
                    outError = "shader reflection binding record is invalid";
                    return false;
                }
                binding.kind = static_cast<ShaderResourceBindingKind>(kind);
                entry.bindings.push_back(binding);
            }
            entry.dxil.assign(payload.begin() + static_cast<size_t>(dxilOffset),
                              payload.begin() + static_cast<size_t>(dxilOffset + dxilBytes));
            dxilRanges.push_back({dxilOffset, dxilBytes});
            previousEnd = dxilOffset + dxilBytes;
            artifact.entries.push_back(std::move(entry));
        }
        if (!IsZeroPadding(payload, previousEnd, payload.size()) || !ValidateShaderCookedArtifact(artifact, outError)) {
            if (outError.empty()) {
                outError = "shader payload trailing bytes are invalid";
            }
            return false;
        }
        std::string canonicalMetadata{};
        if (!SerializeHshadercMetadata(artifact, dxilRanges, canonicalMetadata, outError) ||
            canonicalMetadata != jsonText) {
            if (outError.empty()) {
                outError = "shader cooked metadata is not canonical";
            }
            return false;
        }
        outArtifact = std::move(artifact);
        outError.clear();
        return true;
    } catch (const std::exception& error) {
        outError = "shader cooked metadata parse failed: " + std::string(error.what());
        return false;
    }
}

} // namespace HIKARI::ASSETS::SHADER::INTERNAL
