#include "Editor/Assets/HIKARI_FarModelBakeOperation.h"

#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Models/Loading/HIKARI_ModelSourceLoader.h"
#include "Assets/Models/Processing/HIKARI_FarModelBake.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

FarModelBakeOutcome BakeFarModelAsset(AssetDatabase& database, std::string_view modelAssetGuid, uint32_t meshIndex) {
    FarModelBakeOutcome outcome{};
    AssetGuid guid{};
    guid.value = std::string(modelAssetGuid);
    const AssetRecord* record = database.FindByGuid(guid);
    if (record == nullptr || record->type != AssetType::Model) {
        outcome.message = "The far model bake needs a model asset.";
        return outcome;
    }
    const std::filesystem::path projectRoot = database.GetProjectRoot();
    const std::filesystem::path source =
        record->sourcePath.is_absolute() ? record->sourcePath : projectRoot / record->sourcePath;
    ModelAsset model{};
    model.SetName(record->guid.value);
    model.SetSourcePath(source.generic_string());
    if (!ASSETS::MODELS::LoadModelSource(model)) {
        outcome.message = "Cannot read the model source: " + TEXT::PathToUtf8(record->sourcePath);
        return outcome;
    }

    std::string name = TEXT::PathToUtf8(source.stem());
    if (meshIndex != 0u) {
        name += "_mesh" + std::to_string(meshIndex);
    }
    name += "_far";
    const std::filesystem::path relativeDirectory =
        std::filesystem::path("Assets") / "Models" / TEXT::PathFromUtf8(name);

    FarModelBakeRequest request{};
    request.meshIndex = meshIndex;
    request.outputDirectory = projectRoot / relativeDirectory;
    request.name = name;
    request.resolveTexture = [&](const TextureAsset3D& texture) {
        const std::filesystem::path path = TEXT::PathFromUtf8(texture.sourcePath);
        return path.is_absolute() ? path : projectRoot / path;
    };
    if (!BakeFarModel(model, request, outcome.message)) {
        return outcome;
    }

    const std::filesystem::path relativeModel = relativeDirectory / TEXT::PathFromUtf8(name + ".gltf");
    if (!database.ScanAssets(true)) {
        outcome.message = "The asset scan failed after the far model bake.";
        return outcome;
    }
    const AssetRecord* farRecord = database.FindByPath(relativeModel);
    if (farRecord == nullptr) {
        outcome.message = "The far model was not registered: " + TEXT::PathToUtf8(relativeModel);
        return outcome;
    }
    outcome.farModelGuid = farRecord->guid.value;
    outcome.ok = true;
    outcome.message = database.QueueImportAssets({farRecord->guid}, "Far model")
                          ? "Far model baked; its import is queued."
                          : "Far model baked; another import batch is running, so import it again afterwards.";
    return outcome;
}

void RegisterFarModelBakeOperation(EditorOperationRegistry& registry, std::function<DocumentSceneBase*()> scene) {
    EditorOperationDefinition bake{
        "asset.bake_far_model",
        "Bake a low detail far model (a few textured cards) from a model asset for instance sets, and queue its "
        "import. Returns the new guid to use as farModelAssetGuid.",
        "assets",
        {{"modelAssetGuid", EditorOperationParameterType::String, true, "The model to bake"},
         {"meshIndex", EditorOperationParameterType::Int, false, "Which mesh of the model, default 0"}},
        [scene = std::move(scene)](const nlohmann::json& parameters) {
            DocumentSceneBase* document = scene ? scene() : nullptr;
            if (document == nullptr) {
                return OperationFailure("NO_PROJECT", "No project is open.");
            }
            const FarModelBakeOutcome outcome =
                BakeFarModelAsset(document->GetAssetDatabase(), parameters.value("modelAssetGuid", std::string{}),
                                  parameters.value("meshIndex", 0u));
            if (!outcome.ok) {
                return OperationFailure("FAR_MODEL_BAKE_FAILED", outcome.message);
            }
            return OperationOk(outcome.message, nlohmann::json{{"farModelAssetGuid", outcome.farModelGuid}});
        }};
    bake.permission = EditorOperationPermission::ModifyProjectFiles;
    registry.Register(std::move(bake), EditorOperationKind::Mutation, false);
}

} // namespace HIKARI::EDITOR
