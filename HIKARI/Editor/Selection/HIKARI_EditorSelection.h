#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    class GameObject;
    class ModelAsset;
    class World;

    enum class EditorObjectSelectionMode {
        Replace,
        Add,
        Toggle,
    };

    // Stable document ids are the source of truth. selectedObject remains the
    // active object used by single-object inspectors and legacy integrations.
    struct EditorSelection {
        GameObject* selectedObject = nullptr;
        ModelAsset* selectedAsset = nullptr;
        std::string selectedAssetGuid{};
        std::string selectedAssetPath{};

        void ClearObjects() noexcept;
        void ClearAsset() noexcept;

        void SelectObject(
            World& world,
            GameObject* object,
            EditorObjectSelectionMode mode =
                EditorObjectSelectionMode::Replace);
        void SelectObjectIds(
            World& world,
            const std::vector<SceneObjectId>& objectIds,
            EditorObjectSelectionMode mode =
                EditorObjectSelectionMode::Replace);

        void RepairObjectSelection(
            World& world,
            SceneObjectId preferredActiveObject = {});
        bool IsObjectSelected(SceneObjectId objectId) const noexcept;
        std::size_t GetSelectedObjectCount() const noexcept;
        SceneObjectId GetActiveObjectId() const noexcept;
        const std::vector<SceneObjectId>& GetSelectedObjectIds()
            const noexcept;
        std::vector<GameObject*> ResolveSelectedObjects(
            World& world) const;

    private:
        std::vector<SceneObjectId> selectedObjectIds_{};
        SceneObjectId activeObjectId_{};
    };

} // namespace HIKARI
