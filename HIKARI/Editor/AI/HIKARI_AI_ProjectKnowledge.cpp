#include "Editor/AI/HIKARI_AI_ProjectKnowledge.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
namespace {
constexpr size_t kMaximumGuideBytes = 64u * 1024u;
constexpr size_t kMaximumContextGuideBytes = 6u * 1024u;

bool ReadText(const std::filesystem::path& path, std::string& text) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return text;
}

AssetDatabase* ResolveDatabase(const std::function<DocumentSceneBase*()>& provider) {
    DocumentSceneBase* scene = provider ? provider() : nullptr;
    return scene ? &scene->GetAssetDatabase() : nullptr;
}

EditorOperationResult ReadGuide(const std::function<DocumentSceneBase*()>& provider) {
    const AssetDatabase* database = ResolveDatabase(provider);
    if (database == nullptr) {
        return OperationFailure("NO_PROJECT", "No project is open.");
    }
    const std::filesystem::path path = GetProjectGuidePath(database->GetProjectRoot());
    std::string text;
    const bool exists = ReadText(path, text);
    return OperationOk(exists ? "Project guide." : "The project has no guide yet.",
                       {{"path", TEXT::PathToUtf8(path)}, {"exists", exists}, {"content", std::move(text)}});
}

EditorOperationResult WriteGuide(const std::function<DocumentSceneBase*()>& provider,
                                 const nlohmann::json& parameters) {
    const AssetDatabase* database = ResolveDatabase(provider);
    if (database == nullptr) {
        return OperationFailure("NO_PROJECT", "No project is open.");
    }
    const std::filesystem::path path = GetProjectGuidePath(database->GetProjectRoot());
    std::string text = parameters.value("content", std::string{});
    if (parameters.value("append", false)) {
        std::string existing;
        if (ReadText(path, existing) && !existing.empty()) {
            text = existing + (existing.back() == '\n' ? "" : "\n") + text;
        }
    }
    if (text.size() > kMaximumGuideBytes) {
        return OperationFailure("GUIDE_TOO_LARGE", "The project guide is limited to 64 KiB.");
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    if (error || !output) {
        return OperationFailure("GUIDE_WRITE_FAILED", "Could not write " + TEXT::PathToUtf8(path));
    }
    return OperationOk("Project guide saved.", {{"path", TEXT::PathToUtf8(path)}, {"bytes", text.size()}});
}

EditorOperationResult QueryAssets(const std::function<DocumentSceneBase*()>& provider,
                                  const nlohmann::json& parameters) {
    const AssetDatabase* database = ResolveDatabase(provider);
    if (database == nullptr) {
        return OperationFailure("NO_PROJECT", "No project is open.");
    }
    const std::string typeName = parameters.value("type", std::string{});
    const AssetType type = ASSETS::SEMANTICS::ParseAssetType(typeName);
    if (!typeName.empty() && type == AssetType::Unknown) {
        return OperationFailure("UNKNOWN_ASSET_TYPE", "Unknown asset type: " + typeName);
    }
    const std::string keyword = Lower(parameters.value("keyword", std::string{}));
    const size_t offset = static_cast<size_t>((std::max)(0, parameters.value("offset", 0)));
    const size_t limit = static_cast<size_t>(std::clamp(parameters.value("limit", 32), 1, 128));
    const std::vector<const AssetRecord*> records =
        typeName.empty() ? database->CollectAll() : database->CollectByType(type);
    nlohmann::json assets = nlohmann::json::array();
    size_t matched = 0;
    for (const AssetRecord* record : records) {
        const std::string name = GetAssetRecordDisplayName(*record);
        const std::string path = TEXT::PathToUtf8(record->sourcePath);
        if (!keyword.empty() && Lower(name).find(keyword) == std::string::npos &&
            Lower(path).find(keyword) == std::string::npos) {
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

std::filesystem::path GetProjectGuidePath(const std::filesystem::path& projectRoot) {
    return projectRoot / "ProjectSettings" / "AI" / "ProjectGuide.md";
}

std::string ReadProjectGuideForContext(const std::filesystem::path& projectRoot) {
    std::string text;
    if (!ReadText(GetProjectGuidePath(projectRoot), text) || text.empty()) {
        return {};
    }
    if (text.size() > kMaximumContextGuideBytes) {
        // UTF-8 の途中で切らないよう、継続バイトの手前まで戻す。
        size_t end = kMaximumContextGuideBytes;
        while (end > 0u && (static_cast<unsigned char>(text[end]) & 0xC0u) == 0x80u) {
            --end;
        }
        text.resize(end);
        text += "\n...(truncated; read the rest with project.guide.read)";
    }
    return text;
}

void RegisterProjectKnowledgeOperations(EditorOperationRegistry& registry, std::function<DocumentSceneBase*()> scene) {
    registry.Register(
        {"project.guide.read",
         "Read the project guide (art direction, naming conventions, preferred assets) written by the user.",
         "general",
         {},
         [scene](const nlohmann::json&) { return ReadGuide(scene); }},
        EditorOperationKind::Query, false);
    EditorOperationDefinition write{
        "project.guide.write",
        "Replace or append the project guide after the user asked to record conventions. Requires user approval.",
        "general",
        {{"content", EditorOperationParameterType::String, true, "Markdown text"},
         {"append", EditorOperationParameterType::Bool, false, "Append instead of replacing"}},
        [scene](const nlohmann::json& parameters) { return WriteGuide(scene, parameters); }};
    write.permission = EditorOperationPermission::ModifyProjectFiles;
    registry.Register(std::move(write), EditorOperationKind::Mutation, false);
    registry.Register(
        {"asset.query",
         "Find project assets by type and name or path keyword. Returns guid, type, name and source path.",
         "assets",
         {{"type", EditorOperationParameterType::String, false,
           "Asset type such as Model, Texture, Material or VfxEffect"},
          {"keyword", EditorOperationParameterType::String, false, "Case-insensitive name or path fragment"},
          {"offset", EditorOperationParameterType::Int, false, "Next page offset"},
          {"limit", EditorOperationParameterType::Int, false, "Page size 1 to 128, default 32"}},
         [scene](const nlohmann::json& parameters) { return QueryAssets(scene, parameters); }},
        EditorOperationKind::Query, false);
}

} // namespace HIKARI::EDITOR
