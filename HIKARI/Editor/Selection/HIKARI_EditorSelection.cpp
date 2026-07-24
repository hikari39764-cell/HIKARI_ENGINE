#include "Editor/Selection/HIKARI_EditorSelection.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
    namespace {
        void AppendUnique(
            std::vector<SceneObjectId>& destination,
            SceneObjectId objectId) {

            if (objectId.value == 0u ||
                std::find(
                    destination.begin(),
                    destination.end(),
                    objectId) != destination.end()) {
                return;
            }
            destination.push_back(objectId);
        }
    }

    void EditorSelection::ClearObjects() noexcept {
        selectedObject = nullptr;
        selectedObjectIds_.clear();
        activeObjectId_ = {};
    }

    void EditorSelection::ClearAsset() noexcept {
        selectedAsset = nullptr;
        selectedAssetGuid.clear();
        selectedAssetPath.clear();
    }

    void EditorSelection::SelectObject(
        World& world,
        GameObject* object,
        EditorObjectSelectionMode mode) {

        const std::vector<SceneObjectId> ids =
            object != nullptr
            ? std::vector<SceneObjectId>{ object->GetDocumentId() }
            : std::vector<SceneObjectId>{};
        SelectObjectIds(world, ids, mode);
    }

    void EditorSelection::SelectObjectIds(
        World& world,
        const std::vector<SceneObjectId>& objectIds,
        EditorObjectSelectionMode mode) {

        std::vector<SceneObjectId> validIds{};
        validIds.reserve(objectIds.size());
        for (SceneObjectId objectId : objectIds) {
            if (objectId.value != 0u &&
                world.FindObject(objectId) != nullptr) {
                AppendUnique(validIds, objectId);
            }
        }

        switch (mode) {
        case EditorObjectSelectionMode::Add:
            for (SceneObjectId objectId : validIds) {
                AppendUnique(selectedObjectIds_, objectId);
            }
            break;
        case EditorObjectSelectionMode::Toggle:
            for (SceneObjectId objectId : validIds) {
                const auto found = std::find(
                    selectedObjectIds_.begin(),
                    selectedObjectIds_.end(),
                    objectId);
                if (found == selectedObjectIds_.end()) {
                    selectedObjectIds_.push_back(objectId);
                } else {
                    selectedObjectIds_.erase(found);
                }
            }
            break;
        case EditorObjectSelectionMode::Replace:
        default:
            selectedObjectIds_ = std::move(validIds);
            break;
        }

        ClearAsset();
        RepairObjectSelection(world);

        if (!objectIds.empty()) {
            const SceneObjectId requestedPrimary = objectIds.back();
            const bool canPromoteToActive =
                mode != EditorObjectSelectionMode::Toggle ||
                objectIds.size() == 1u;
            if (canPromoteToActive &&
                IsObjectSelected(requestedPrimary)) {
                selectedObject = world.FindObject(requestedPrimary);
                activeObjectId_ = requestedPrimary;
            }
        }
    }

    void EditorSelection::RepairObjectSelection(
        World& world,
        SceneObjectId preferredActiveObject) {
        std::unordered_set<uint64_t> seen{};
        std::erase_if(
            selectedObjectIds_,
            [&](SceneObjectId objectId) {
                return objectId.value == 0u ||
                    world.FindObject(objectId) == nullptr ||
                    !seen.insert(objectId.value).second;
            });

        const SceneObjectId activeId =
            preferredActiveObject.value != 0u
            ? preferredActiveObject
            : activeObjectId_;
        if (activeId.value != 0u && IsObjectSelected(activeId)) {
            selectedObject = world.FindObject(activeId);
            activeObjectId_ = activeId;
            return;
        }
        selectedObject = selectedObjectIds_.empty()
            ? nullptr
            : world.FindObject(selectedObjectIds_.back());
        activeObjectId_ = selectedObject != nullptr
            ? selectedObject->GetDocumentId()
            : SceneObjectId{};
    }

    bool EditorSelection::IsObjectSelected(
        SceneObjectId objectId) const noexcept {

        return objectId.value != 0u &&
            std::find(
                selectedObjectIds_.begin(),
                selectedObjectIds_.end(),
                objectId) != selectedObjectIds_.end();
    }

    std::size_t EditorSelection::GetSelectedObjectCount() const noexcept {
        return selectedObjectIds_.size();
    }

    SceneObjectId EditorSelection::GetActiveObjectId() const noexcept {
        return activeObjectId_;
    }

    const std::vector<SceneObjectId>&
        EditorSelection::GetSelectedObjectIds() const noexcept {
        return selectedObjectIds_;
    }

    std::vector<GameObject*> EditorSelection::ResolveSelectedObjects(
        World& world) const {

        std::vector<GameObject*> objects{};
        objects.reserve(selectedObjectIds_.size());
        for (SceneObjectId objectId : selectedObjectIds_) {
            if (GameObject* object = world.FindObject(objectId)) {
                objects.push_back(object);
            }
        }
        return objects;
    }

} // namespace HIKARI
