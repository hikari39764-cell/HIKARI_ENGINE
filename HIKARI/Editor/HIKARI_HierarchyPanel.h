#pragma once

#include <array>
#include <functional>

namespace HIKARI {

    struct EditorSelection;
    class GameObject;
    class World;

    class HierarchyPanel {
    public:
        void Draw(World& world, EditorSelection& selection);
        void DrawContents(World& world, EditorSelection& selection);
        void DrawContents(
            World& world,
            EditorSelection& selection,
            const std::function<void(GameObject&)>&
                drawObjectContextMenu,
            const std::function<bool(const GameObject&)>&
                isObjectLocked = {});

    private:
        std::array<char, 128> searchBuffer_{};
    };

} // namespace HIKARI
