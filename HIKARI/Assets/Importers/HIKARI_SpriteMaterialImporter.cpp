#include "Assets/Importers/HIKARI_SpriteMaterialImporter.h"

#include "Assets/HIKARI_AssetDatabase.h"
#include "RenderContracts/Shader/HIKARI_HshadercFormat.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "RenderContracts/Shader/HIKARI_ProjectSpriteShaderContract.h"
#include "Assets/Sprite/HIKARI_SpriteMaterialAsset.h"
#include "Assets/Sprite/HIKARI_SpriteMaterialSerialization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

namespace HIKARI {

ASSETS::SEMANTICS::AssetImporterKind SpriteMaterialImporter::GetImporterKind() const noexcept {
    return ASSETS::SEMANTICS::AssetImporterKind::SpriteMaterial;
}

AssetMeta SpriteMaterialImporter::CreateDefaultMeta(const std::filesystem::path& sourcePath,
                                                    const AssetGuid& guid) const {
    return ASSETS::SEMANTICS::MakeBaseAssetMeta(sourcePath, guid, GetImporterKind());
}

AssetImportResult SpriteMaterialImporter::Import(const AssetRecord& record, const AssetImportContext& context) {
    AssetImportResult result{};
    if (!record.guid.IsValid() || record.type != AssetType::SpriteMaterial ||
        !ASSETS::SEMANTICS::IsAssetSourceForImporter(record.sourcePath, GetImporterKind())) {
        result.message = "[SpriteMaterialImporter] source record has the wrong identity or extension";
        return result;
    }
    if (context.assetDatabase == nullptr || context.projectRoot.empty() || context.importedDirectory.empty()) {
        result.message = "[SpriteMaterialImporter] project asset database and output paths are required";
        return result;
    }

    const std::filesystem::path sourcePath = PROJECT_PATHS::ResolveProjectPath(context.projectRoot, record.sourcePath);
    SPRITE::SpriteMaterialAssetData asset{};
    if (!SPRITE::LoadSpriteMaterialAssetData(sourcePath, asset, result.message)) {
        result.message = "[SpriteMaterialImporter] " + result.message;
        return result;
    }

    const AssetRecord* shaderRecord = context.assetDatabase->FindByGuid(asset.shaderAssetGuid);
    if (shaderRecord == nullptr || shaderRecord->duplicateGuid || shaderRecord->type != AssetType::Shader ||
        !shaderRecord->lastImportSucceeded) {
        result.message = "[SpriteMaterialImporter] referenced Shader is missing, duplicated, or not imported: " +
                         asset.shaderAssetGuid.value;
        return result;
    }
    const std::string shaderArtifact =
        ASSETS::SEMANTICS::FindAssetArtifactPath(*shaderRecord, ASSETS::SEMANTICS::AssetArtifactKind::ShaderProgram);
    if (shaderArtifact.empty()) {
        result.message =
            "[SpriteMaterialImporter] referenced Shader has no cooked artifact: " + asset.shaderAssetGuid.value;
        return result;
    }
    const std::filesystem::path shaderArtifactPath =
        PROJECT_PATHS::ResolveProjectPath(context.projectRoot, TEXT::PathFromUtf8(shaderArtifact));
    ASSETS::SHADER::ShaderCookedArtifact shader{};
    if (!ASSETS::SHADER::ReadHshadercFile(shaderArtifactPath, shader, result.message)) {
        result.message =
            "[SpriteMaterialImporter] referenced Shader is not a compatible Sprite shader: " + result.message;
        return result;
    }
    if (shader.sourceGuid != asset.shaderAssetGuid) {
        result.message = "[SpriteMaterialImporter] Shader artifact source GUID does not match the referenced asset";
        return result;
    }
    if (!ASSETS::SHADER::ValidateProjectSpriteShaderArtifact(shader, result.message)) {
        result.message =
            "[SpriteMaterialImporter] referenced Shader is not a compatible Sprite shader: " + result.message;
        return result;
    }
    if (!SPRITE::ValidateSpriteMaterialAsset(asset, shader.parameters, result.message)) {
        result.message = "[SpriteMaterialImporter] " + result.message;
        return result;
    }

    const std::filesystem::path runtimeArtifactPath = context.importedDirectory / "sprite-material.hspritematerialc";
    if (!SPRITE::WriteSpriteMaterialAssetArtifact(context.StageFile(runtimeArtifactPath), record.guid, asset,
                                                  result.message)) {
        result.message = "[SpriteMaterialImporter] runtime artifact write failed: " + result.message;
        return result;
    }
    const std::string relativeArtifact =
        PROJECT_PATHS::MakeProjectRelativeString(context.projectRoot, runtimeArtifactPath);
    if (relativeArtifact.empty()) {
        result.message = "[SpriteMaterialImporter] runtime artifact escaped the project root";
        return result;
    }
    result.artifacts.push_back(ASSETS::SEMANTICS::MakeAssetArtifact(
        ASSETS::SEMANTICS::AssetArtifactKind::SpriteMaterialRuntime, relativeArtifact));
    result.dependencies.push_back(AssetDependencyDesc{asset.shaderAssetGuid, {}, "SpriteMaterialShader"});
    result.success = true;
    result.message = "[SpriteMaterialImporter] imported SpriteMaterial for Shader " + asset.shaderAssetGuid.value;
    return result;
}

} // namespace HIKARI
