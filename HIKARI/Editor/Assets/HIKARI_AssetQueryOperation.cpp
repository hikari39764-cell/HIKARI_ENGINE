#include "Editor/Assets/HIKARI_AssetQueryOperation.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
namespace {

EditorOperationResult QueryAssets(const std::function<DocumentSceneBase*()>& provider,
                                  const nlohmann::json& parameters) {
    DocumentSceneBase* scene = provider ? provider() : nullptr;
    if (scene == nullptr) {
        return OperationFailure("NO_PROJECT", "No project is open.");
    }
    const AssetDatabase& database = scene->GetAssetDatabase();
    const std::string typeName = parameters.value("type", std::string{});
    const AssetType type = ASSETS::SEMANTICS::ParseAssetType(typeName);
    if (!typeName.empty() && type == AssetType::Unknown) {
        return OperationFailure("UNKNOWN_ASSET_TYPE", "Unknown asset type: " + typeName);
    }
    const std::string keyword = TEXT::ToLowerAsciiCopy(parameters.value("keyword", std::string{}));
    const size_t offset = static_cast<size_t>((std::max)(0, parameters.value("offset", 0)));
    const size_t limit = static_cast<size_t>(std::clamp(parameters.value("limit", 32), 1, 128));
    const std::vector<const AssetRecord*> records =
        typeName.empty() ? database.CollectAll() : database.CollectByType(type);
    nlohmann::json assets = nlohmann::json::array();
    size_t matched = 0;
    for (const AssetRecord* record : records) {
        const std::string name = GetAssetRecordDisplayName(*record);
        const std::string path = TEXT::PathToUtf8(record->sourcePath);
        if (!keyword.empty() && TEXT::ToLowerAsciiCopy(name).find(keyword) == std::string::npos &&
            TEXT::ToLowerAsciiCopy(path).find(keyword) == std::string::npos) {
            continue;
        }
        if (matched++ < offset || assets.size() >= limit) {
            continue;
        }
        assets.push_back({{"guid", record->guid.value},
                          {"type", std::string(ASSETS::SEMANTICS::ToString(record->type))},
                          {"name", name},
                          {"path", path}});
    }
    nlohmann::json data{{"assets", std::move(assets)}, {"total", matched}};
    if (offset + limit < matched) {
        data["nextOffset"] = offset + limit;
    }
    return OperationOk("Project assets.", std::move(data));
}

} // namespace

void RegisterAssetQueryOperation(EditorOperationRegistry& registry, std::function<DocumentSceneBase*()> scene) {
    EditorOperationDefinition query{
        "asset.query",
        "Find project assets by type and name or path keyword. Returns guid, type, name and source path.",
        "assets",
        {{"type", EditorOperationParameterType::String, false,
          "Asset type such as Model, Texture, Material or VfxEffect"},
         {"keyword", EditorOperationParameterType::String, false, "Case-insensitive name or path fragment"},
         {"offset", EditorOperationParameterType::Int, false, "Next page offset"},
         {"limit", EditorOperationParameterType::Int, false, "Page size 1 to 128, default 32"}},
        [scene = std::move(scene)](const nlohmann::json& parameters) { return QueryAssets(scene, parameters); }};
    query.permission = EditorOperationPermission::ReadEditorState;
    registry.Register(std::move(query), EditorOperationKind::Query, false);
}

} // namespace HIKARI::EDITOR
