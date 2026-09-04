#include "Scene/Ui/HIKARI_UiSceneSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "HIKARI/Project/FrameContext.h"
#include "HIKARI/Project/UiTypes.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"

namespace HIKARI {
    namespace {

        constexpr float kDegreesToRadians =
            3.14159265358979323846f / 180.0f;

        template<class T>
        T* FindComponent(
            World& world,
            RuntimeObjectHandle object,
            ComponentTypeId typeId) noexcept {
            return dynamic_cast<T*>(
                world.FindFirstIndexedComponent(typeId, object));
        }

        int ComposeOrder(int canvasOrder, int elementOrder) noexcept {
            const int64_t value =
                static_cast<int64_t>(std::clamp(
                    canvasOrder,
                    -kUiCanvasSortOrderLimit,
                    kUiCanvasSortOrderLimit)) *
                    static_cast<int64_t>(kUiCanvasOrderStride) +
                static_cast<int64_t>(std::clamp(
                    elementOrder,
                    -kUiElementOrderLimit,
                    kUiElementOrderLimit));
            return static_cast<int>(std::clamp(
                value,
                static_cast<int64_t>((std::numeric_limits<int>::min)()),
                static_cast<int64_t>((std::numeric_limits<int>::max)())));
        }

        int AddOrder(int base, int offset) noexcept {
            return static_cast<int>(std::clamp(
                static_cast<int64_t>(base) + static_cast<int64_t>(offset),
                static_cast<int64_t>((std::numeric_limits<int>::min)()),
                static_cast<int64_t>((std::numeric_limits<int>::max)())));
        }

        GameProjectUiColor MultiplyColor(
            const GameProjectUiColor& lhs,
            const GameProjectUiColor& rhs) noexcept {
            return {
                lhs.r * rhs.r,
                lhs.g * rhs.g,
                lhs.b * rhs.b,
                lhs.a * rhs.a
            };
        }

    } // namespace

    UiCanvasComponent* UiSceneSystem::FindCanvas(
        World& world,
        GameObject& object,
        GameObject*& outOwner) noexcept {
        GameObject* current = &object;
        for (uint32_t depth = 0u;
            current != nullptr && depth < 64u;
            ++depth) {
            if (UiCanvasComponent* canvas =
                    FindComponent<UiCanvasComponent>(
                        world,
                        current->GetRuntimeHandle(),
                        kUiCanvasComponentTypeId)) {
                outOwner = current;
                return canvas;
            }
            current = current->GetParent();
        }
        outOwner = nullptr;
        return nullptr;
    }

    UiSceneSystem::ResolvedLayout UiSceneSystem::ResolveLayout(
        World& world,
        GameObject& object,
        uint32_t depth) noexcept {
        const uint64_t key = object.GetRuntimeHandle().ToValue();
        if (const auto found = layouts_.find(key);
            found != layouts_.end()) {
            return found->second;
        }
        ResolvedLayout result{};
        if (depth >= 64u) {
            layouts_.emplace(key, result);
            return result;
        }
        UiRectComponent* rect = FindComponent<UiRectComponent>(
            world, object.GetRuntimeHandle(), kUiRectComponentTypeId);
        GameObject* canvasOwner = nullptr;
        UiCanvasComponent* canvas = FindCanvas(world, object, canvasOwner);
        if (rect == nullptr || !rect->IsEnabled() ||
            canvas == nullptr || !canvas->IsEnabled()) {
            layouts_.emplace(key, result);
            return result;
        }

        float parentX = 0.0f;
        float parentY = 0.0f;
        float parentWidth = canvas->GetReferenceWidth();
        float parentHeight = canvas->GetReferenceHeight();
        if (&object != canvasOwner) {
            GameObject* parent = object.GetParent();
            if (parent != nullptr && parent != canvasOwner &&
                FindComponent<UiRectComponent>(
                    world,
                    parent->GetRuntimeHandle(),
                    kUiRectComponentTypeId) != nullptr) {
                const ResolvedLayout parentLayout =
                    ResolveLayout(world, *parent, depth + 1u);
                if (parentLayout.valid) {
                    parentX = parentLayout.x;
                    parentY = parentLayout.y;
                    parentWidth = parentLayout.width;
                    parentHeight = parentLayout.height;
                }
            }
        }
        const MATH::Vec2 anchorMinimum = rect->GetAnchorMinimum();
        const MATH::Vec2 anchorMaximum = rect->GetAnchorMaximum();
        const MATH::Vec2 offsetMinimum = rect->GetOffsetMinimum();
        const MATH::Vec2 offsetMaximum = rect->GetOffsetMaximum();
        float left = parentX + parentWidth * anchorMinimum.x +
            offsetMinimum.x;
        float top = parentY + parentHeight * anchorMinimum.y +
            offsetMinimum.y;
        float right = parentX + parentWidth * anchorMaximum.x +
            offsetMaximum.x;
        float bottom = parentY + parentHeight * anchorMaximum.y +
            offsetMaximum.y;
        if (left > right) std::swap(left, right);
        if (top > bottom) std::swap(top, bottom);
        const MATH::Vec2 pivot = rect->GetPivot();
        result.referenceWidth = canvas->GetReferenceWidth();
        result.referenceHeight = canvas->GetReferenceHeight();
        result.x = left;
        result.y = top;
        result.width = right - left;
        result.height = bottom - top;
        result.pivotX = pivot.x;
        result.pivotY = pivot.y;
        result.rotationRadians =
            rect->GetRotationDegrees() * kDegreesToRadians;
        result.order = ComposeOrder(
            canvas->GetSortOrder(), rect->GetOrder());
        result.interactable = canvas->IsInteractable();
        result.valid = std::isfinite(result.x) &&
            std::isfinite(result.y) &&
            std::isfinite(result.width) && result.width > 0.0f &&
            std::isfinite(result.height) && result.height > 0.0f;
        layouts_.emplace(key, result);
        return result;
    }

    void UiSceneSystem::SubmitImages(
        World& world,
        const FrameContext&) {
        (void)world.ForEachObjectWith<UiRectComponent, UiImageComponent>(
            WorldComponentQueryAnchor::SmallestUniqueBucket,
            [&](GameObject& object, UiRectComponent&, UiImageComponent& image) {
                if (!image.IsEnabled()) return;
                const ResolvedLayout layout = ResolveLayout(world, object);
                if (!layout.valid) return;
                GameProjectUiSpriteRequest request{};
                request.sourceId = object.GetRuntimeHandle().ToValue();
                request.order = AddOrder(
                    layout.order, image.GetOrderOffset());
                request.referenceWidth = layout.referenceWidth;
                request.referenceHeight = layout.referenceHeight;
                request.x = layout.x;
                request.y = layout.y;
                request.width = layout.width;
                request.height = layout.height;
                request.pivotX = layout.pivotX;
                request.pivotY = layout.pivotY;
                request.rotationRadians = layout.rotationRadians;
                request.timeSeconds = elapsedSeconds_;
                request.tint = image.GetTint();
                if (UiButtonComponent* button =
                        FindComponent<UiButtonComponent>(
                            world,
                            object.GetRuntimeHandle(),
                            kUiButtonComponentTypeId)) {
                    request.tint = MultiplyColor(
                        request.tint, button->GetStateTint());
                }
                bool submitted = false;
                if (image.GetTextureAssetGuid().IsValid()) {
                    submitted = presentation_->SubmitSpriteAsset(
                        image.GetTextureAssetGuid().value, request);
                }
                if (!submitted && !image.GetTextureAssetPath().empty()) {
                    submitted = presentation_->SubmitSprite(
                        image.GetTextureAssetPath(), request);
                }
                (void)submitted;
            });
    }

    void UiSceneSystem::SubmitTexts(
        World& world,
        const FrameContext&) {
        (void)world.ForEachObjectWith<UiRectComponent, UiTextComponent>(
            WorldComponentQueryAnchor::SmallestUniqueBucket,
            [&](GameObject& object, UiRectComponent&, UiTextComponent& text) {
                if (!text.IsEnabled() || text.GetFontAssetPath().empty() ||
                    text.GetText().empty()) return;
                const ResolvedLayout layout = ResolveLayout(world, object);
                if (!layout.valid) return;
                GameProjectUiTextRequest request{};
                request.sourceId = object.GetRuntimeHandle().ToValue();
                request.order = AddOrder(
                    layout.order, text.GetOrderOffset());
                request.referenceWidth = layout.referenceWidth;
                request.referenceHeight = layout.referenceHeight;
                request.x = layout.x;
                request.y = layout.y;
                request.maxWidth = layout.width;
                request.maxHeight = layout.height;
                request.pivotX = layout.pivotX;
                request.pivotY = layout.pivotY;
                request.rotationRadians = layout.rotationRadians;
                request.pixelHeight = text.GetPixelHeight();
                request.textColor = text.GetColor();
                request.horizontalAlign = text.GetHorizontalAlign();
                request.verticalAlign = text.GetVerticalAlign();
                (void)presentation_->SubmitText(
                    text.GetFontAssetPath(), text.GetText(), request);
            });
    }

    void UiSceneSystem::PreRender(
        World& world,
        const FrameContext& frame) {
        // Authoring views render the same Scene components without enabling
        // interaction. This keeps the Editor preview and Play output on one
        // projection path.
        if (presentation_ == nullptr) {
            return;
        }
        layouts_.clear();
        SubmitImages(world, frame);
        SubmitTexts(world, frame);
    }

} // namespace HIKARI
