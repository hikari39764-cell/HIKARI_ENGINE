#include "Scene/Ui/HIKARI_UiDataBindingComponent.h"
#include "Scene/Ui/HIKARI_UiBindingSourceComponent.h"
#include "Scene/HIKARI_World.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Scene/Ui/HIKARI_UiLayoutResolver.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Scene/Ui/HIKARI_UiUtilityComponents.h"
#include "Scene/Ui/HIKARI_UiAnimationPlayer.h"
#include "Scene/Ui/HIKARI_UiScrollViewComponent.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>

namespace HIKARI {
bool ContainsUiPoint(const UiResolvedLayout& layout, float x, float y) noexcept {
    if (!layout.valid || !layout.clipSupported || !std::isfinite(x) || !std::isfinite(y))
        return false;
    if (layout.clipEnabled &&
        (x < layout.clipLeft || x > layout.clipRight || y < layout.clipTop || y > layout.clipBottom))
        return false;
    if (!layout.transform.InverseTransform(x, y))
        return false;
    return x >= layout.x && x <= layout.x + layout.width && y >= layout.y && y <= layout.y + layout.height;
}

void UiLayoutResolver::ArrangeChildren(World&, GameObject& object, const UiResolvedLayout& parent,
                                       UiLayoutGroupComponent& group, bool previewBranch,
                                       const BINDING::RuntimeBindingStore* bindings) {
    const uint64_t parentKey = object.GetRuntimeHandle().ToValue();
    if (!groups_.insert(parentKey).second)
        return;

    const auto cell = group.GetCellSize();
    const float spacing = group.GetSpacing();
    const float padding = group.GetPadding();
    float contentWidth = padding * 2.0f;
    float contentHeight = padding * 2.0f;
    int index = 0;
    for (auto* child : object.GetChildren()) {
        const auto* childRect = child->GetComponent<UiRectComponent>();
        if (!childRect || !childRect->IsEnabled())
            continue;
        bool visible = true;
        for (auto* branch = child; branch && branch != &object; branch = branch->GetParent()) {
            if (const auto* childGroup = branch->GetComponent<UiGroupComponent>();
                childGroup && !childGroup->IsVisible() && !previewBranch) {
                visible = false;
                break;
            }
            if (!previewBranch) {
                RuntimeObjectHandle bindingTarget{};
                if (ResolveInheritedUiBindingSource(*branch, bindingTarget) == UiBindingSourceState::Invalid) {
                    visible = false;
                    break;
                }
            }
            if (const auto* childBinding = branch->GetComponent<UiDataBindingComponent>()) {
                const bool bindingVisible = authoringPreview_ || previewBranch
                                                ? childBinding->IsVisibleForPreview(bindings)
                                                : childBinding->IsVisible(bindings);
                if (!bindingVisible) {
                    visible = false;
                    break;
                }
            }
        }
        if (!visible)
            continue;

        int column = 0, row = 0;
        switch (group.GetDirection()) {
        case UiLayoutDirection::Horizontal: column = index; break;
        case UiLayoutDirection::Vertical: row = index; break;
        case UiLayoutDirection::Grid:
            column = index % group.GetColumns();
            row = index / group.GetColumns();
            break;
        }
        const float x = parent.x + padding + column * (cell.x + spacing);
        const float y = parent.y + padding + row * (cell.y + spacing);
        arranged_[child->GetRuntimeHandle().ToValue()] = {x, y, cell.x, cell.y};
        contentWidth = (std::max)(contentWidth, padding + column * (cell.x + spacing) + cell.x + padding);
        contentHeight = (std::max)(contentHeight, padding + row * (cell.y + spacing) + cell.y + padding);
        ++index;
    }
    if (auto* scroll = object.GetComponent<UiScrollViewComponent>(); scroll && scroll->IsEnabled()) {
        scroll->SetResolvedContentExtent(scroll->GetAxis() == UiScrollAxis::Vertical ? contentHeight : contentWidth);
    }
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
    if (!authoringPreview_ && !previewBranch) {
        RuntimeObjectHandle bindingTarget{};
        if (ResolveInheritedUiBindingSource(object, bindingTarget) == UiBindingSourceState::Invalid)
            return finish();
    }
    GameObject* owner = &object;
    UiCanvasComponent* canvas = nullptr;
    const auto* bindings = authoringPreview_ && previewBindings_
                               ? previewBindings_
                               : world.Services().Find<BINDING::RuntimeBindingStore>();
    for (uint32_t i = 0; owner && i < 64; ++i, owner = owner->GetParent()) {
        if (const auto* binding = owner->GetComponent<UiDataBindingComponent>()) {
            const bool visible = authoringPreview_ || previewBranch ? binding->IsVisibleForPreview(bindings)
                                                                    : binding->IsVisible(bindings);
            if (!visible)
                return finish();
        }
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
    result.inputEnabled = canvas->IsInputActive();
    for (auto* ancestor = &object; ancestor; ancestor = ancestor->GetParent()) {
        if (auto* group = ancestor->GetComponent<UiGroupComponent>())
            result.interactable &= group->IsInteractable();
        if (player_ && player_->IsClosing(ancestor->GetRuntimeHandle()))
            result.interactable = false;
        if (const auto* binding = ancestor->GetComponent<UiDataBindingComponent>())
            result.interactable &= (authoringPreview_ || previewBranch) ? binding->IsInteractableForPreview(bindings)
                                                                        : binding->IsInteractable(bindings);
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
        if (!parent.valid || !parent.clipSupported)
            return finish();
    }
    if (parentObject) {
        if (auto* group = parentObject->GetComponent<UiLayoutGroupComponent>(); group && group->IsEnabled()) {
            ArrangeChildren(world, *parentObject, parent, *group, previewBranch, bindings);
        } else if (auto* scroll = parentObject->GetComponent<UiScrollViewComponent>()) {
            scroll->SetResolvedContentExtent(0.0f);
        }
    }
    result.clipEnabled = parent.clipEnabled;
    result.clipSupported = parent.clipSupported;
    result.clipLeft = parent.clipLeft;
    result.clipTop = parent.clipTop;
    result.clipRight = parent.clipRight;
    result.clipBottom = parent.clipBottom;
    if (parentObject) {
        if (auto* scroll = parentObject->GetComponent<UiScrollViewComponent>(); scroll && scroll->IsEnabled()) {
            const float viewportExtent = scroll->GetAxis() == UiScrollAxis::Vertical ? parent.height : parent.width;
            const float offset = scroll->GetScrollOffset(viewportExtent);
            scroll->SetScrollOffset(offset, viewportExtent);
            if (scroll->GetAxis() == UiScrollAxis::Vertical)
                parent.transform = parent.transform * Affine2D{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, -offset};
            else
                parent.transform = parent.transform * Affine2D{1.0f, 0.0f, 0.0f, 1.0f, -offset, 0.0f};
        }
    }
    const auto a = rect->GetAnchorMinimum(), b = rect->GetAnchorMaximum();
    const auto low = rect->GetOffsetMinimum(), high = rect->GetOffsetMaximum();
    float left = parent.x + parent.width * a.x + low.x, top = parent.y + parent.height * a.y + low.y;
    float right = parent.x + parent.width * b.x + high.x, bottom = parent.y + parent.height * b.y + high.y;
    if (const auto slot = arranged_.find(key); slot != arranged_.end()) {
        left = slot->second.x;
        top = slot->second.y;
        right = left + slot->second.z;
        bottom = top + slot->second.w;
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
    if (auto* scroll = object.GetComponent<UiScrollViewComponent>(); scroll && scroll->IsEnabled()) {
        const Affine2D& transform = result.transform;
        constexpr float epsilon = 1.0e-4f;
        const bool axisAligned = (std::abs(transform.b) <= epsilon && std::abs(transform.c) <= epsilon) ||
                                 (std::abs(transform.a) <= epsilon && std::abs(transform.d) <= epsilon);
        if (!axisAligned) {
            result.clipSupported = false;
        } else {
            float x0 = result.x, y0 = result.y;
            float x1 = result.x + result.width, y1 = result.y + result.height;
            transform.Transform(x0, y0);
            transform.Transform(x1, y1);
            const float leftClip = (std::min)(x0, x1);
            const float topClip = (std::min)(y0, y1);
            const float rightClip = (std::max)(x0, x1);
            const float bottomClip = (std::max)(y0, y1);
            if (result.clipEnabled) {
                result.clipLeft = (std::max)(result.clipLeft, leftClip);
                result.clipTop = (std::max)(result.clipTop, topClip);
                result.clipRight = (std::min)(result.clipRight, rightClip);
                result.clipBottom = (std::min)(result.clipBottom, bottomClip);
            } else {
                result.clipLeft = leftClip;
                result.clipTop = topClip;
                result.clipRight = rightClip;
                result.clipBottom = bottomClip;
                result.clipEnabled = true;
            }
        }
    }
    const int64_t order = static_cast<int64_t>(canvas->GetSortOrder()) * kUiCanvasOrderStride + rect->GetOrder();
    result.order = static_cast<int>(std::clamp(order, static_cast<int64_t>(INT_MIN), static_cast<int64_t>(INT_MAX)));
    result.valid = result.transform.IsFinite() && std::isfinite(result.x) && std::isfinite(result.y) &&
                   std::isfinite(result.rotationRadians) && std::isfinite(result.width) &&
                   std::isfinite(result.height) && result.width > 0 && result.height > 0;
    return finish();
}
} // namespace HIKARI
