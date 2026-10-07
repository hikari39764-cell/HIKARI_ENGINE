#include "Editor/History/HIKARI_SceneObjectsHistoryCommand.h"

#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Scene/HIKARI_SceneDocumentQueries.h"

namespace HIKARI::EDITOR {
namespace {
// 前後の全オブジェクトではなく、変わったオブジェクトと並び順だけを保持する。
class SceneObjectsHistoryCommand final : public IEditorDocumentCommand {
public:
    SceneObjectsHistoryCommand(std::string label, std::span<const SceneObjectData> beforeObjects,
                               std::span<const SceneObjectData> afterObjects, SceneCameraSettings beforeCamera,
                               SceneCameraSettings afterCamera, EditorDocumentImpact impact)
        : label_(std::move(label)), changes_(DiffSceneObjects(beforeObjects, afterObjects)),
          beforeCamera_(std::move(beforeCamera)), afterCamera_(std::move(afterCamera)), impact_(impact) {
        beforeOrder_ = CollectOrder(beforeObjects);
        afterOrder_ = CollectOrder(afterObjects);
        const auto keep = [](std::span<const SceneObjectData> objects, std::span<const SceneObjectId> first,
                             std::span<const SceneObjectId> second) {
            std::unordered_set<uint64_t> ids;
            for (SceneObjectId id : first) {
                ids.insert(id.value);
            }
            for (SceneObjectId id : second) {
                ids.insert(id.value);
            }
            std::vector<SceneObjectData> kept;
            for (const SceneObjectData& object : objects) {
                if (ids.contains(object.id.value)) {
                    kept.push_back(object);
                }
            }
            return kept;
        };
        beforeObjects_ = keep(beforeObjects, changes_.removed, changes_.changed);
        afterObjects_ = keep(afterObjects, changes_.added, changes_.changed);
    }

    const std::string& GetLabel() const noexcept override { return label_; }

    EditorDocumentImpact GetImpact() const noexcept override { return impact_; }

    void Undo(SceneDocument& document) const override {
        document.objects = Restore(std::move(document.objects), beforeOrder_, beforeObjects_);
        document.camera = beforeCamera_;
    }

    void Redo(SceneDocument& document) const override {
        document.objects = Restore(std::move(document.objects), afterOrder_, afterObjects_);
        document.camera = afterCamera_;
    }

    bool TryMergeApplied(const IEditorDocumentCommand&) override { return false; }

    std::optional<SceneObjectChanges> GetObjectChanges(bool undo) const override {
        if (!undo) {
            return changes_;
        }
        return SceneObjectChanges{changes_.removed, changes_.added, changes_.changed};
    }

private:
    static std::vector<SceneObjectId> CollectOrder(std::span<const SceneObjectData> objects) {
        std::vector<SceneObjectId> order;
        order.reserve(objects.size());
        for (const SceneObjectData& object : objects) {
            order.push_back(object.id);
        }
        return order;
    }

    // 保存した版を優先し、変わっていないオブジェクトは現在の document から移して並び順を戻す。
    static std::vector<SceneObjectData> Restore(std::vector<SceneObjectData> current,
                                                std::span<const SceneObjectId> order,
                                                std::span<const SceneObjectData> stored) {
        std::unordered_map<uint64_t, SceneObjectData*> currentById;
        currentById.reserve(current.size());
        for (SceneObjectData& object : current) {
            currentById.emplace(object.id.value, &object);
        }
        std::unordered_map<uint64_t, const SceneObjectData*> storedById;
        storedById.reserve(stored.size());
        for (const SceneObjectData& object : stored) {
            storedById.emplace(object.id.value, &object);
        }
        std::vector<SceneObjectData> restored;
        restored.reserve(order.size());
        for (SceneObjectId id : order) {
            if (const auto found = storedById.find(id.value); found != storedById.end()) {
                restored.push_back(*found->second);
            } else if (const auto existing = currentById.find(id.value); existing != currentById.end()) {
                restored.push_back(std::move(*existing->second));
            }
        }
        return restored;
    }

    std::string label_{};
    SceneObjectChanges changes_{};
    std::vector<SceneObjectId> beforeOrder_{};
    std::vector<SceneObjectId> afterOrder_{};
    std::vector<SceneObjectData> beforeObjects_{};
    std::vector<SceneObjectData> afterObjects_{};
    SceneCameraSettings beforeCamera_{};
    SceneCameraSettings afterCamera_{};
    EditorDocumentImpact impact_ = EditorDocumentImpact::RuntimeWorld;
};
} // namespace

std::unique_ptr<IEditorDocumentCommand> MakeSceneObjectsHistoryCommand(
    std::string label, std::vector<SceneObjectData> beforeObjects, std::vector<SceneObjectData> afterObjects,
    SceneCameraSettings beforeCamera, SceneCameraSettings afterCamera, EditorDocumentImpact impact) {
    return std::make_unique<SceneObjectsHistoryCommand>(std::move(label), beforeObjects, afterObjects,
                                                        std::move(beforeCamera), std::move(afterCamera), impact);
}

} // namespace HIKARI::EDITOR
