#include "Scene/Ui/HIKARI_UiDataBindingComponent.h"
#include "Scene/HIKARI_World.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Scene/Ui/HIKARI_UiLayoutResolver.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Scene/Ui/HIKARI_UiUtilityComponents.h"
#include "Scene/Ui/HIKARI_UiAnimationPlayer.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>

namespace HIKARI {
bool ContainsUiPoint(const UiResolvedLayout& layout, float x, float y) noexcept {
    if (!layout.valid || !std::isfinite(x) || !std::isfinite(y))
        return false;
    if (!layout.transform.InverseTransform(x, y))
        return false;
    return x >= layout.x && x <= layout.x + layout.width && y >= layout.y && y <= layout.y + layout.height;
}
UiResolvedLayout UiLayoutResolver::Resolve(World& world, GameObject& object, uint32_t depth) {
    const auto key = object.GetRuntimeHandle().ToValue();
    if (const auto found = layouts_.find(key); found != layouts_.end())
        return found->second;
    UiResolvedLayout result;
    auto finish = [&] {
        layouts_[key] = result;
        return result;
    };
    if (depth >= 64)
        return finish();
    auto* rect = object.GetComponent<UiRectComponent>();
    const bool ownCanvas = object.GetComponent<UiCanvasComponent>() != nullptr;
    if ((!ownCanvas && rect && !rect->IsEnabled()) || (!rect && !ownCanvas))
        return finish();
    bool previewBranch = false;
    if (previewRoot_.value)
        for (auto* ancestor = &object; ancestor; ancestor = ancestor->GetParent())
            if (ancestor->GetDocumentId() == previewRoot_) {
                previewBranch = true;
                break;
            }
    GameObject* owner = &object;
    UiCanvasComponent* canvas = nullptr;
    auto* bindings = world.Services().Find<BINDING::RuntimeBindingStore>();
    for (uint32_t i = 0; owner && i < 64; ++i, owner = owner->GetParent()) {
        if (const auto* binding = owner->GetComponent<UiDataBindingComponent>();
            binding && !binding->IsVisible(bindings) && !previewBranch)
            return finish();
        if (auto* group = owner->GetComponent<UiGroupComponent>()) {
            if (!group->IsVisible() && !previewBranch)
                return finish();
            result.opacity *= group->GetOpacity();
        }
        if (const auto* button = owner->GetComponent<UiButtonComponent>();
            button && (!player_ || !player_->HasInstance(owner->GetRuntimeHandle()))) {
            const auto tint = button->GetStateTint();
            result.opacity *= tint.a;
            result.color.x *= tint.r;
            result.color.y *= tint.g;
            result.color.z *= tint.b;
        }
        if (player_) {
            if (auto found = player_->Poses().find(owner->GetRuntimeHandle().ToValue());
                found != player_->Poses().end()) {
                const auto& pose = found->second;
                result.opacity *= pose.Get(UiMotionProperty::Opacity);
                result.color.x *= pose.Get(UiMotionProperty::Red);
                result.color.y *= pose.Get(UiMotionProperty::Green);
                result.color.z *= pose.Get(UiMotionProperty::Blue);
                result.reveal *= pose.Get(UiMotionProperty::Reveal);
            }
        }
        if ((canvas = owner->GetComponent<UiCanvasComponent>()))
            break;
    }
    if (!canvas || !canvas->IsEnabled() || !owner || result.opacity <= 0)
        return finish();
    result.canvas = owner->GetDocumentId();
    result.referenceWidth = canvas->GetReferenceWidth();
    result.referenceHeight = canvas->GetReferenceHeight();
    result.interactable = canvas->IsInteractable();
    for (auto* ancestor = &object; ancestor; ancestor = ancestor->GetParent()) {
        if (auto* group = ancestor->GetComponent<UiGroupComponent>())
            result.interactable &= group->IsInteractable();
        if (player_ && player_->IsClosing(ancestor->GetRuntimeHandle()))
            result.interactable = false;
        if (const auto* binding = ancestor->GetComponent<UiDataBindingComponent>())
            result.interactable &= binding->IsInteractable(bindings);
        if (ancestor == owner)
            break;
    }
    UiResolvedLayout parent;
    if (ownCanvas) {
        result.width = result.referenceWidth;
        result.height = result.referenceHeight;
        result.order = canvas->GetSortOrder() * kUiCanvasOrderStride;
        UiVisualPose pose;
        if (player_)
            if (auto found = player_->Poses().find(key); found != player_->Poses().end())
                pose = found->second;
        result.transform = Affine2D::Around(result.width * .5f, result.height * .5f,
                                            pose.Get(UiMotionProperty::Rotation) * .01745329251994329577f,
                                            pose.Get(UiMotionProperty::ScaleX), pose.Get(UiMotionProperty::ScaleY),
                                            pose.Get(UiMotionProperty::OffsetX), pose.Get(UiMotionProperty::OffsetY));
        result.valid = true;
        return finish();
    }
    parent.width = result.referenceWidth;
    parent.height = result.referenceHeight;
    parent.valid = true;
    auto* parentObject = object.GetParent();
    for (uint32_t i = 0;
         parentObject && parentObject != owner && !parentObject->GetComponent<UiRectComponent>() && i < 64; ++i)
        parentObject = parentObject->GetParent();
    if (parentObject && (parentObject == owner || parentObject->GetComponent<UiRectComponent>())) {
        parent = Resolve(world, *parentObject, depth + 1);
        if (!parent.valid)
            return finish();
    }
    const auto a = rect->GetAnchorMinimum(), b = rect->GetAnchorMaximum();
    const auto low = rect->GetOffsetMinimum(), high = rect->GetOffsetMaximum();
    float left = parent.x + parent.width * a.x + low.x, top = parent.y + parent.height * a.y + low.y;
    float right = parent.x + parent.width * b.x + high.x, bottom = parent.y + parent.height * b.y + high.y;
    if (parentObject) {
        if (auto* group = parentObject->GetComponent<UiLayoutGroupComponent>(); group && group->IsEnabled()) {
            const auto parentKey = parentObject->GetRuntimeHandle().ToValue();
            if (groups_.insert(parentKey).second) {
                int index = 0;
                const auto cell = group->GetCellSize();
                for (auto* child : parentObject->GetChildren()) {
                    const auto* childRect = child->GetComponent<UiRectComponent>();
                    const auto* childGroup = child->GetComponent<UiGroupComponent>();
                    if (!childRect || !childRect->IsEnabled() || (childGroup && !childGroup->IsVisible()))
                        continue;
                    int col = 0, row = 0;
                    switch (group->GetDirection()) {
                    case UiLayoutDirection::Horizontal: col = index; break;
                    case UiLayoutDirection::Vertical: row = index; break;
                    case UiLayoutDirection::Grid:
                        col = index % group->GetColumns();
                        row = index / group->GetColumns();
                        break;
                    }
                    arranged_[child->GetRuntimeHandle().ToValue()] = {
                        parent.x + group->GetPadding() + col * (cell.x + group->GetSpacing()),
                        parent.y + group->GetPadding() + row * (cell.y + group->GetSpacing()), cell.x, cell.y};
                    ++index;
                }
            }
            if (const auto slot = arranged_.find(key); slot != arranged_.end()) {
                left = slot->second.x;
                top = slot->second.y;
                right = left + slot->second.z;
                bottom = top + slot->second.w;
            }
        }
    }
    if (left > right)
        std::swap(left, right);
    if (top > bottom)
        std::swap(top, bottom);
    result.x = left;
    result.y = top;
    result.width = right - left;
    result.height = bottom - top;
    const auto pivot = rect->GetPivot();
    result.pivotX = pivot.x;
    result.pivotY = pivot.y;
    UiVisualPose pose;
    if (player_)
        if (auto found = player_->Poses().find(key); found != player_->Poses().end())
            pose = found->second;
    const float angle = (rect->GetRotationDegrees() + pose.Get(UiMotionProperty::Rotation)) * .01745329251994329577f;
    result.rotationRadians = parent.rotationRadians + angle;
    result.transform =
        parent.transform * Affine2D::Around(left + result.width * pivot.x, top + result.height * pivot.y, angle,
                                            pose.Get(UiMotionProperty::ScaleX), pose.Get(UiMotionProperty::ScaleY),
                                            pose.Get(UiMotionProperty::OffsetX), pose.Get(UiMotionProperty::OffsetY));
    const int64_t order = static_cast<int64_t>(canvas->GetSortOrder()) * kUiCanvasOrderStride + rect->GetOrder();
    result.order = static_cast<int>(std::clamp(order, static_cast<int64_t>(INT_MIN), static_cast<int64_t>(INT_MAX)));
    result.valid = result.transform.IsFinite() && std::isfinite(result.x) && std::isfinite(result.y) &&
                   std::isfinite(result.rotationRadians) && std::isfinite(result.width) &&
                   std::isfinite(result.height) && result.width > 0 && result.height > 0;
    return finish();
}
} // namespace HIKARI
