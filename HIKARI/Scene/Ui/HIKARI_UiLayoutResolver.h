#pragma once
#include "Render3D/HIKARI_Math3D.h"
#include "Core/Math/HIKARI_Affine2D.h"
#include "Scene/HIKARI_SceneObjectId.h"
#include <unordered_map>
#include <unordered_set>

namespace HIKARI {
class World;
class UiAnimationPlayer;
class GameObject;
class UiLayoutGroupComponent;
namespace BINDING {
class RuntimeBindingStore;
}
struct UiResolvedLayout {
    float referenceWidth = 0, referenceHeight = 0;
    float x = 0, y = 0, width = 0, height = 0, pivotX = .5f, pivotY = .5f, rotationRadians = 0;
    float opacity = 1, reveal = 1;
    MATH::Vec3 color{1, 1, 1};
    Affine2D transform;
    int order = 0;
    bool interactable = false, inputEnabled = false, valid = false;
    bool clipEnabled = false, clipSupported = true;
    float clipLeft = 0.0f, clipTop = 0.0f, clipRight = 0.0f, clipBottom = 0.0f;
    SceneObjectId canvas{};
};
bool ContainsUiPoint(const UiResolvedLayout& layout, float x, float y) noexcept;
// フレーム内の配置結果。実行時、ヒットテスト、編集プレビューで共有する。
class UiLayoutResolver {
public:
    explicit UiLayoutResolver(const UiAnimationPlayer* player = nullptr) : player_(player) {}
    void SetAnimationPlayer(const UiAnimationPlayer* player) {
        player_ = player;
        Clear();
    }
    void SetPreviewRoot(SceneObjectId root) {
        previewRoot_ = root;
        Clear();
    }
    void SetPreviewBindings(const BINDING::RuntimeBindingStore* bindings) {
        previewBindings_ = bindings;
        Clear();
    }
    void SetAuthoringPreview(bool active) {
        authoringPreview_ = active;
        Clear();
    }
    void Clear() {
        layouts_.clear();
        arranged_.clear();
        groups_.clear();
    }
    UiResolvedLayout Resolve(World& world, GameObject& object, uint32_t depth = 0);

private:
    void ArrangeChildren(World& world, GameObject& object, const UiResolvedLayout& parent,
                         UiLayoutGroupComponent& group, bool previewBranch,
                         const BINDING::RuntimeBindingStore* bindings);

    const UiAnimationPlayer* player_ = nullptr;
    const BINDING::RuntimeBindingStore* previewBindings_ = nullptr;
    SceneObjectId previewRoot_{};
    bool authoringPreview_ = false;
    std::unordered_map<uint64_t, UiResolvedLayout> layouts_;
    std::unordered_map<uint64_t, MATH::Vec4> arranged_;
    std::unordered_set<uint64_t> groups_;
};
} // namespace HIKARI
