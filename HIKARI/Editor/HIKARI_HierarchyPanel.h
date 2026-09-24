#pragma once

#include <array>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace HIKARI {

struct EditorSelection;
class GameObject;
class World;
struct SceneDocument;
namespace EDITOR {
class EditorOperationRegistry;
}

class HierarchyPanel {
public:
    void Draw(World& world, EditorSelection& selection);
    void DrawContents(World& world, EditorSelection& selection);
    void DrawContents(World& world, EditorSelection& selection,
                      const std::function<void(GameObject&)>& drawObjectContextMenu,
                      const std::function<bool(const GameObject&)>& isObjectLocked = {},
                      const SceneDocument* document = nullptr,
                      const EDITOR::EditorOperationRegistry* operations = nullptr, std::uint64_t documentRevision = 0);

private:
    std::array<char, 128> searchBuffer_{};
    std::array<char, 512> folderNameBuffer_{};
    std::string folderSource_, folderMessage_;
    bool folderRename_ = false, folderPopupRequested_ = false;
    std::uint64_t folderDocumentRevision_ = 0;
    void DrawFolderContents(World& world, EditorSelection& selection, const std::vector<GameObject*>& objects,
                            std::string_view search, const SceneDocument& document,
                            const EDITOR::EditorOperationRegistry& operations,
                            const std::function<void(GameObject*)>& drawObject, std::uint64_t documentRevision);
};

} // namespace HIKARI
