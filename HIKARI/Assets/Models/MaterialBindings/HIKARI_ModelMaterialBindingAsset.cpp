#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingAsset.h"

#include <algorithm>
#include <exception>
#include <unordered_set>
#include <utility>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI::ASSETS::MODELS {

namespace {

std::optional<MaterialSlotId> ReadSlotId(const nlohmann::json& node) noexcept {
    try {
        if (node.is_string()) {
            return ParseMaterialSlotId(node.get<std::string>());
        }
        if (node.is_number_unsigned()) {
            const uint64_t value = node.get<uint64_t>();
            return value == 0u ? std::nullopt : std::optional<MaterialSlotId>{MaterialSlotId{value}};
        }
    } catch (...) {
    }
    return std::nullopt;
}

nlohmann::json ToJson(const ModelMaterialBindingAssetData& data) {
    auto bindings = data.bindings;
    std::sort(bindings.begin(), bindings.end(), [](const auto& left, const auto& right) {
        if (left.slotId.value != right.slotId.value) {
            return left.slotId.value < right.slotId.value;
        }
        return left.materialAssetGuid.value < right.materialAssetGuid.value;
    });
    nlohmann::json array = nlohmann::json::array();
    for (const ModelMaterialBindingEntry& binding : bindings) {
        array.push_back(nlohmann::json{
            {"slotId", MaterialSlotIdToString(binding.slotId)},
            {"materialAssetGuid", binding.materialAssetGuid.value},
        });
    }
    return nlohmann::json{
        {"version", kModelMaterialBindingAssetVersion},
        {"displayName", data.displayName},
        {"modelAssetGuid", data.modelAssetGuid.value},
        {"bindings", std::move(array)},
    };
}

} // namespace

bool ValidateModelMaterialBindingAsset(const ModelMaterialBindingAssetData& data,
                                       std::vector<std::string>& outDiagnostics) {
    outDiagnostics.clear();
    if (data.version != kModelMaterialBindingAssetVersion) {
        outDiagnostics.push_back("ModelMaterialBinding.UnsupportedVersion");
    }
    if (!data.modelAssetGuid.IsValid()) {
        outDiagnostics.push_back("ModelMaterialBinding.MissingModelAsset");
    }
    if (data.bindings.size() > 1024u) {
        outDiagnostics.push_back("ModelMaterialBinding.BindingLimitExceeded");
    }
    std::unordered_set<uint64_t> slots{};
    for (const ModelMaterialBindingEntry& binding : data.bindings) {
        if (!binding.slotId.IsValid()) {
            outDiagnostics.push_back("ModelMaterialBinding.InvalidSlotId");
        } else if (!slots.emplace(binding.slotId.value).second) {
            outDiagnostics.push_back("ModelMaterialBinding.DuplicateSlotId:" + MaterialSlotIdToString(binding.slotId));
        }
        if (!binding.materialAssetGuid.IsValid()) {
            outDiagnostics.push_back("ModelMaterialBinding.InvalidMaterialAsset:" +
                                     MaterialSlotIdToString(binding.slotId));
        }
    }
    return outDiagnostics.empty();
}

bool ParseModelMaterialBindingAssetJson(std::string_view jsonText, ModelMaterialBindingAssetData& outData,
                                        std::string& outError, std::vector<std::string>* outDiagnostics) {
    if (outDiagnostics) {
        outDiagnostics->clear();
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object()) {
            outError = "invalid ModelMaterialBinding JSON document";
            return false;
        }
        const uint32_t version = root.value("version", 0u);
        if (version != kModelMaterialBindingAssetVersion) {
            outError = "unsupported ModelMaterialBinding version " + std::to_string(version);
            return false;
        }
        ModelMaterialBindingAssetData data{};
        data.displayName = root.value("displayName", data.displayName);
        data.modelAssetGuid.value = root.value("modelAssetGuid", std::string{});
        const auto bindings = root.find("bindings");
        if (bindings != root.end()) {
            if (!bindings->is_array()) {
                outError = "ModelMaterialBinding bindings must be an array";
                return false;
            }
            for (const nlohmann::json& item : *bindings) {
                if (!item.is_object()) {
                    outError = "ModelMaterialBinding entry must be an object";
                    return false;
                }
                const auto slot = item.find("slotId");
                const std::optional<MaterialSlotId> slotId = slot == item.end() ? std::nullopt : ReadSlotId(*slot);
                if (!slotId.has_value()) {
                    outError = "ModelMaterialBinding entry requires a stable slotId";
                    return false;
                }
                data.bindings.push_back(
                    ModelMaterialBindingEntry{*slotId, AssetGuid{item.value("materialAssetGuid", std::string{})}});
            }
        }
        std::vector<std::string> validation{};
        if (!ValidateModelMaterialBindingAsset(data, validation)) {
            outError = validation.empty() ? "ModelMaterialBinding validation failed" : validation.front();
            if (outDiagnostics) {
                *outDiagnostics = std::move(validation);
            }
            return false;
        }
        outData = std::move(data);
        outError.clear();
        return true;
    } catch (const std::exception& exception) {
        outError = "ModelMaterialBinding schema error: " + std::string(exception.what());
        return false;
    }
}

bool LoadModelMaterialBindingAsset(const std::filesystem::path& path, ModelMaterialBindingAssetData& outData,
                                   std::string& outError, std::vector<std::string>* outDiagnostics) {
    nlohmann::json root{};
    if (!SERIALIZATION::JSON::ReadJsonFile(path, root, &outError)) {
        return false;
    }
    return ParseModelMaterialBindingAssetJson(root.dump(), outData, outError, outDiagnostics);
}

bool SaveModelMaterialBindingAsset(const std::filesystem::path& path, const ModelMaterialBindingAssetData& data,
                                   std::string& outError) {
    std::vector<std::string> diagnostics{};
    if (!ValidateModelMaterialBindingAsset(data, diagnostics)) {
        outError = diagnostics.empty() ? "ModelMaterialBinding validation failed" : diagnostics.front();
        return false;
    }
    return SERIALIZATION::JSON::WriteJsonFile(path, ToJson(data), &outError);
}

std::vector<ModelMaterialBindingDependency> CollectModelMaterialBindingDependencies(
    const ModelMaterialBindingAssetData& data) {
    std::vector<ModelMaterialBindingDependency> result{};
    if (data.modelAssetGuid.IsValid()) {
        result.push_back({ModelMaterialBindingDependencyKind::Model, data.modelAssetGuid, "Model"});
    }
    std::vector<std::string> materialGuids{};
    for (const auto& binding : data.bindings) {
        if (binding.materialAssetGuid.IsValid()) {
            materialGuids.push_back(binding.materialAssetGuid.value);
        }
    }
    std::sort(materialGuids.begin(), materialGuids.end());
    materialGuids.erase(std::unique(materialGuids.begin(), materialGuids.end()), materialGuids.end());
    for (const std::string& guid : materialGuids) {
        result.push_back({ModelMaterialBindingDependencyKind::Material, AssetGuid{guid}, "Material"});
    }
    return result;
}

} // namespace HIKARI::ASSETS::MODELS
