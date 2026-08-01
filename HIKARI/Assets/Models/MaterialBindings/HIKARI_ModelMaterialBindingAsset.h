#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Models/HIKARI_ModelMaterialSlot.h"

namespace HIKARI::ASSETS::MODELS {

    inline constexpr uint32_t kModelMaterialBindingAssetVersion = 1u;

    struct ModelMaterialBindingEntry {
        MaterialSlotId slotId{};
        AssetGuid materialAssetGuid{};
    };

    struct ModelMaterialBindingAssetData {
        uint32_t version = kModelMaterialBindingAssetVersion;
        uint32_t sourceVersion = kModelMaterialBindingAssetVersion;
        bool migratedFromLegacy = false;
        std::string displayName = "Model Material Bindings";
        AssetGuid modelAssetGuid{};
        std::vector<ModelMaterialBindingEntry> bindings{};
    };

    enum class ModelMaterialBindingDependencyKind : uint8_t {
        Model,
        Material,
    };

    struct ModelMaterialBindingDependency {
        ModelMaterialBindingDependencyKind kind =
            ModelMaterialBindingDependencyKind::Material;
        AssetGuid assetGuid{};
        std::string role{};
    };

    bool ValidateModelMaterialBindingAsset(
        const ModelMaterialBindingAssetData& data,
        std::vector<std::string>& outDiagnostics);

    bool ParseModelMaterialBindingAssetJson(
        std::string_view jsonText,
        ModelMaterialBindingAssetData& outData,
        std::string& outError,
        std::vector<std::string>* outDiagnostics = nullptr);

    bool SerializeModelMaterialBindingAssetJson(
        const ModelMaterialBindingAssetData& data,
        std::string& outJsonText,
        std::string& outError);

    bool LoadModelMaterialBindingAsset(
        const std::filesystem::path& path,
        ModelMaterialBindingAssetData& outData,
        std::string& outError,
        std::vector<std::string>* outDiagnostics = nullptr);

    bool SaveModelMaterialBindingAsset(
        const std::filesystem::path& path,
        const ModelMaterialBindingAssetData& data,
        std::string& outError);

    const ModelMaterialBindingEntry* FindModelMaterialBinding(
        const ModelMaterialBindingAssetData& data,
        MaterialSlotId slotId) noexcept;

    bool SetModelMaterialBinding(
        ModelMaterialBindingAssetData& data,
        MaterialSlotId slotId,
        const AssetGuid& materialAssetGuid,
        std::string& outError);

    bool RestoreImportedModelMaterialBinding(
        ModelMaterialBindingAssetData& data,
        MaterialSlotId slotId) noexcept;

    std::vector<ModelMaterialBindingDependency>
        CollectModelMaterialBindingDependencies(
            const ModelMaterialBindingAssetData& data);

} // namespace HIKARI::ASSETS::MODELS
