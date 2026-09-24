#include "Editor/Authoring/HIKARI_SceneFolderAuthoring.h"
#include <algorithm>
#include <set>
#include <utility>
#include <unordered_map>

namespace HIKARI::EDITOR {
namespace {
bool Inside(std::string_view value, std::string_view folder) {
    return value == folder ||
           (value.size() > folder.size() && value.starts_with(folder) && value[folder.size()] == '/');
}
std::string Parent(std::string_view path) {
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string{} : std::string(path.substr(0, slash));
}
struct FolderState {
    std::vector<std::string> folders;
    std::vector<std::pair<SceneObjectId, std::string>> membership;
    bool operator==(const FolderState&) const = default;
};
FolderState Capture(const SceneDocument& document) {
    FolderState state{document.editorFolders, {}};
    for (const auto& object : document.objects)
        state.membership.emplace_back(object.id, object.editorFolder);
    return state;
}
void Restore(SceneDocument& document, const FolderState& state) {
    document.editorFolders = state.folders;
    std::unordered_map<std::uint64_t, const std::string*> memberships;
    memberships.reserve(state.membership.size());
    for (const auto& [id, folder] : state.membership)
        memberships.emplace(id.value, &folder);
    for (auto& object : document.objects) {
        const auto found = memberships.find(object.id.value);
        if (found != memberships.end())
            object.editorFolder = *found->second;
    }
}
class SceneFolderHistoryCommand final : public IEditorDocumentCommand {
public:
    SceneFolderHistoryCommand(FolderState before, FolderState after, std::string label)
        : before_(std::move(before)), after_(std::move(after)), label_(std::move(label)) {}
    const std::string& GetLabel() const noexcept override { return label_; }
    EditorDocumentImpact GetImpact() const noexcept override { return EditorDocumentImpact::None; }
    void Undo(SceneDocument& document) const override { Restore(document, before_); }
    void Redo(SceneDocument& document) const override { Restore(document, after_); }
    bool TryMergeApplied(const IEditorDocumentCommand&) override { return false; }

private:
    FolderState before_, after_;
    std::string label_;
};
} // namespace

bool IsValidSceneFolderPath(std::string_view path) noexcept {
    if (path.empty() || path.size() > 512 || path.front() == '/' || path.back() == '/')
        return false;
    std::size_t start = 0, depth = 0;
    while (start < path.size()) {
        const auto end = path.find('/', start);
        const auto part = path.substr(start, end == std::string_view::npos ? path.size() - start : end - start);
        if (part.empty() || part == "." || part == ".." || part.front() == ' ' || part.back() == ' ' || ++depth > 16)
            return false;
        for (unsigned char c : part)
            if (c < 32 || c == 127 || c == '\\')
                return false;
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return true;
}

std::vector<std::string> CollectSceneFolderPaths(const SceneDocument& document) {
    std::set<std::string> paths;
    const auto add = [&](std::string path) {
        if (!IsValidSceneFolderPath(path))
            return;
        while (!path.empty()) {
            paths.insert(path);
            path = Parent(path);
        }
    };
    for (const auto& path : document.editorFolders)
        add(path);
    for (const auto& object : document.objects)
        add(object.editorFolder);
    return {paths.begin(), paths.end()};
}

SceneFolderEdit ApplySceneFolderOperation(SceneDocument& document, std::string_view operation,
                                          const nlohmann::json& parameters) {
    SceneFolderEdit edit;
    const auto fail = [&](std::string code, std::string message) {
        edit.result.code = std::move(code);
        edit.result.message = std::move(message);
    };
    if (!parameters.is_object()) {
        fail("INVALID_PARAMETERS", "パラメータはオブジェクト形式で指定してください。");
        return edit;
    }
    if (operation == "query") {
        edit.result.ok = true;
        edit.result.code = "OK";
        edit.result.message = "フォルダー一覧";
        edit.result.data["folders"] = CollectSceneFolderPaths(document);
        edit.result.data["objects"] = nlohmann::json::array();
        for (const auto& object : document.objects)
            edit.result.data["objects"].push_back({{"objectId", object.id.value}, {"folder", object.editorFolder}});
        return edit;
    }
    if (!parameters.contains("path") || !parameters["path"].is_string()) {
        fail("INVALID_PATH", "フォルダーのパスを指定してください。");
        return edit;
    }
    const auto path = parameters["path"].get<std::string>();
    if (!(operation == "assign" && path.empty()) && !IsValidSceneFolderPath(path)) {
        fail("INVALID_PATH", "フォルダー名が無効です。階層は / で区切ります。");
        return edit;
    }
    const auto before = Capture(document);
    auto after = before;
    after.folders = CollectSceneFolderPaths(document);
    const bool exists = std::find(after.folders.begin(), after.folders.end(), path) != after.folders.end();
    if (operation != "create" && !path.empty() && !exists) {
        fail("FOLDER_NOT_FOUND", "フォルダーが見つかりません。");
        return edit;
    }
    std::string label;
    if (operation == "create") {
        if (after.folders.size() >= 4096) {
            fail("FOLDER_LIMIT", "フォルダー数の上限です。");
            return edit;
        }
        after.folders.push_back(path);
        label = "フォルダーを作成";
    } else if (operation == "rename") {
        if (!parameters.contains("newPath") || !parameters["newPath"].is_string()) {
            fail("INVALID_PATH", "変更先のパスを指定してください。");
            return edit;
        }
        const auto target = parameters["newPath"].get<std::string>();
        if (!IsValidSceneFolderPath(target) || (target != path && Inside(target, path))) {
            fail("INVALID_PATH", "自分の下へ移動できません。有効な変更先を指定してください。");
            return edit;
        }
        if (target != path && std::find(after.folders.begin(), after.folders.end(), target) != after.folders.end()) {
            fail("FOLDER_EXISTS", "同じ名前のフォルダーが存在します。");
            return edit;
        }
        for (auto& folder : after.folders)
            if (Inside(folder, path))
                folder = target + folder.substr(path.size());
        for (auto& [id, folder] : after.membership)
            if (Inside(folder, path))
                folder = target + folder.substr(path.size());
        for (const auto& folder : after.folders)
            if (!IsValidSceneFolderPath(folder)) {
                fail("INVALID_PATH", "移動後の階層または名前が長すぎます。");
                return edit;
            }
        label = "フォルダー名を変更";
    } else if (operation == "remove") {
        std::erase_if(after.folders, [&](const auto& folder) { return Inside(folder, path); });
        for (auto& [id, folder] : after.membership)
            if (Inside(folder, path))
                folder = Parent(path);
        label = "フォルダーを解散";
    } else if (operation == "assign") {
        if (!parameters.contains("objectIds") || !parameters["objectIds"].is_array() ||
            parameters["objectIds"].empty() || parameters["objectIds"].size() > document.objects.size()) {
            fail("INVALID_OBJECT_IDS", "移動するオブジェクトを選択してください。");
            return edit;
        }
        std::unordered_map<std::uint64_t, std::size_t> indices;
        indices.reserve(after.membership.size());
        for (std::size_t i = 0; i < after.membership.size(); ++i)
            indices.emplace(after.membership[i].first.value, i);
        for (const auto& value : parameters["objectIds"]) {
            if (!value.is_number_unsigned() && (!value.is_number_integer() || value.get<std::int64_t>() <= 0)) {
                fail("INVALID_OBJECT_IDS", "オブジェクトIDが無効です。");
                return edit;
            }
            const auto id = value.get<std::uint64_t>();
            const auto found = indices.find(id);
            if (found == indices.end()) {
                fail("OBJECT_NOT_FOUND", "オブジェクトが見つかりません。");
                return edit;
            }
            after.membership[found->second].second = path;
        }
        label = "フォルダーへ移動";
    } else {
        fail("UNKNOWN_OPERATION", "不明なフォルダー操作です。");
        return edit;
    }
    SceneDocument normalized;
    normalized.editorFolders = after.folders;
    after.folders = CollectSceneFolderPaths(normalized);
    if (after.folders.size() > 4096) {
        fail("FOLDER_LIMIT", "フォルダー数の上限です。");
        return edit;
    }
    if (after != before) {
        Restore(document, after);
        edit.command = std::make_unique<SceneFolderHistoryCommand>(before, after, label);
    }
    edit.result.ok = true;
    edit.result.code = "OK";
    edit.result.message = label;
    edit.result.data = {{"workspace", "scene"},
                        {"path", path},
                        {"changed", edit.command != nullptr},
                        {"folderCount", document.editorFolders.size()}};
    return edit;
}
} // namespace HIKARI::EDITOR
