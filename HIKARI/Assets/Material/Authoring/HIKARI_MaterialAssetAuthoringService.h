#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Material/Authoring/HIKARI_MaterialInstanceResolver.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"

namespace HIKARI {
class AssetDatabase;
}

namespace HIKARI::ASSETS::MATERIAL {

// 型と親の宣言を保ち、UI と外部操作で同じパラメータ編集を使う。
bool SetMaterialParameterValue(MaterialAssetData& source, std::string_view parameterId, const MaterialValueData& value,
                               std::string& outError, const MaterialAssetData* instanceParent = nullptr);

struct MaterialAuthoringDocument {
    AssetGuid assetGuid{};
    std::filesystem::path sourcePath{};
    MaterialAssetData source{};
    MaterialCompileResult compile{};
    std::vector<std::string> migrationDiagnostics{};
};

struct MaterialAuthoringResult {
    bool success = false;
    bool sourceSaved = false;
    bool assetScanned = false;
    bool imported = false;
    AssetGuid assetGuid{};
    std::filesystem::path sourcePath{};
    MaterialCompileResult compile{};
    std::string message{};
};

class MaterialAssetAuthoringService {
public:
    explicit MaterialAssetAuthoringService(AssetDatabase& assetDatabase) noexcept;

    bool Load(const AssetGuid& assetGuid, MaterialAuthoringDocument& outDocument, std::string& outMessage) const;

    MaterialInstanceResolveResult ResolveInstance(const MaterialAssetData& instanceSource) const;

    MaterialAuthoringResult Create(MaterialTemplateId templateId, std::string_view displayName,
                                   const std::filesystem::path& directory = {});

    MaterialAuthoringResult Create(const MaterialAssetData& source, const std::filesystem::path& directory = {});

    MaterialAuthoringResult Update(const AssetGuid& assetGuid, const MaterialAssetData& source);

    MaterialAuthoringResult Save(const AssetGuid& assetGuid, const MaterialAssetData& source);

    MaterialAuthoringResult SaveAs(const MaterialAssetData& source, const std::filesystem::path& destination);

    MaterialAuthoringResult ImportAndScan(const std::filesystem::path& sourcePath, bool createMissingMeta = true);

private:
    MaterialAuthoringResult SaveToPath(const MaterialAssetData& source, const std::filesystem::path& destination,
                                       bool createMissingMeta);

    AssetDatabase& assetDatabase_;
};

} // namespace HIKARI::ASSETS::MATERIAL
