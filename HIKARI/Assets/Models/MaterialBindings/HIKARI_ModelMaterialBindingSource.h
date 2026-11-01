#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Core/HIKARI_AssetGuid.h"
#include "RenderContracts/Model/HIKARI_ModelMaterialBindingAsset.h"

// 作者が編集するモデル材質バインディング原稿（JSON）の検証・読み書きと、依存の収集。
// 描画側が読むデータ本体は RenderContracts の HIKARI_ModelMaterialBindingAsset.h。
namespace HIKARI::ASSETS::MODELS {

enum class ModelMaterialBindingDependencyKind : uint8_t {
    Model,
    Material,
};

struct ModelMaterialBindingDependency {
    ModelMaterialBindingDependencyKind kind = ModelMaterialBindingDependencyKind::Material;
    AssetGuid assetGuid{};
    std::string role{};
};

bool ValidateModelMaterialBindingAsset(const ModelMaterialBindingAssetData& data,
                                       std::vector<std::string>& outDiagnostics);

bool ParseModelMaterialBindingAssetJson(std::string_view jsonText, ModelMaterialBindingAssetData& outData,
                                        std::string& outError, std::vector<std::string>* outDiagnostics = nullptr);

bool LoadModelMaterialBindingAsset(const std::filesystem::path& path, ModelMaterialBindingAssetData& outData,
                                   std::string& outError, std::vector<std::string>* outDiagnostics = nullptr);

bool SaveModelMaterialBindingAsset(const std::filesystem::path& path, const ModelMaterialBindingAssetData& data,
                                   std::string& outError);

std::vector<ModelMaterialBindingDependency> CollectModelMaterialBindingDependencies(
    const ModelMaterialBindingAssetData& data);

} // namespace HIKARI::ASSETS::MODELS
