#include "Scene/Ui/HIKARI_UiDataBindingComponent.h"
#include "Scene/Ui/HIKARI_UiDisplaySettings.h"
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
#include "Scene/Ui/HIKARI_UiUtilityComponents.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"

namespace HIKARI {
namespace {

int AddOrder(int base, int offset) noexcept {
    return static_cast<int>(std::clamp(static_cast<int64_t>(base) + static_cast<int64_t>(offset),
                                       static_cast<int64_t>((std::numeric_limits<int>::min)()),
                                       static_cast<int64_t>((std::numeric_limits<int>::max)())));
}

} // namespace

void UiSceneSystem::SubmitImages(World& world, GAME_UI::GameUiPresentationService& presentation, SceneObjectId canvas,
                                 float timeSeconds) {
    const auto* bindings = world.Services().Find<BINDING::RuntimeBindingStore>();
    (void)world.ForEachObjectWith<UiRectComponent, UiImageComponent>(
        WorldComponentQueryAnchor::SmallestUniqueBucket,
        [&](GameObject& object, UiRectComponent&, UiImageComponent& image) {
            if (!image.IsEnabled())
                return;
            const ResolvedLayout layout = ResolveLayout(world, object);
            if (!layout.valid || (canvas.value && layout.canvas != canvas))
                return;
            GameProjectUiSpriteRequest request{};
            request.sourceId = object.GetRuntimeHandle().ToValue();
            request.order = AddOrder(layout.order, image.GetOrderOffset());
            request.referenceWidth = layout.referenceWidth;
            request.referenceHeight = layout.referenceHeight;
            request.x = layout.x;
            request.y = layout.y;
            request.width = layout.width;
            request.height = layout.height;
            request.pivotX = layout.pivotX;
            request.pivotY = layout.pivotY;
            request.rotationRadians = 0;
            request.timeSeconds = timeSeconds;
            request.tint = image.GetTint();
            request.tint.a *= layout.opacity;
            request.tint.r *= layout.color.x;
            request.tint.g *= layout.color.y;
            request.tint.b *= layout.color.z;
            if (const auto* progress = object.GetComponent<UiProgressComponent>(); progress && progress->IsEnabled()) {
                const float fraction = progress->ResolveFraction(bindings);
                if (fraction <= 0)
                    return;
                float dx = 0, dy = 0;
                if (progress->GetDirection() == UiFillDirection::Right ||
                    progress->GetDirection() == UiFillDirection::Left) {
                    request.width *= fraction;
                    dx = (progress->GetDirection() == UiFillDirection::Left ? layout.width - request.width : 0) +
                         (request.width - layout.width) * layout.pivotX;
                } else {
                    request.height *= fraction;
                    dy = (progress->GetDirection() == UiFillDirection::Up ? layout.height - request.height : 0) +
                         (request.height - layout.height) * layout.pivotY;
                }
                request.x = layout.x + layout.width * layout.pivotX + dx - request.width * layout.pivotX;
                request.y = layout.y + layout.height * layout.pivotY + dy - request.height * layout.pivotY;
            }
            bool submitted = false;
            if (image.GetTextureAssetGuid().IsValid()) {
                submitted =
                    presentation.SubmitSpriteAsset(image.GetTextureAssetGuid().value, request, layout.transform);
            }
            if (!submitted && !image.GetTextureAssetPath().empty()) {
                submitted = presentation.SubmitSprite(image.GetTextureAssetPath(), request, layout.transform);
            }
            (void)submitted;
        });
}

void UiSceneSystem::SubmitTexts(World& world, GAME_UI::GameUiPresentationService& presentation, SceneObjectId canvas,
                                float timeSeconds) {
    (void)world.ForEachObjectWith<UiRectComponent, UiTextComponent>(
        WorldComponentQueryAnchor::SmallestUniqueBucket,
        [&](GameObject& object, UiRectComponent&, UiTextComponent& text) {
            if (!text.IsEnabled())
                return;
            const auto* binding = object.GetComponent<UiDataBindingComponent>();
            const auto* values = world.Services().Find<BINDING::RuntimeBindingStore>();
            const auto boundText = binding ? binding->ResolveText(values, text.GetText()) : std::string{};
            const std::string_view content = binding ? std::string_view(boundText) : std::string_view(text.GetText());
            if (content.empty())
                return;
            const ResolvedLayout layout = ResolveLayout(world, object);
            if (!layout.valid || (canvas.value && layout.canvas != canvas))
                return;
            const auto* store = world.Services().Find<TextAssetStore>();
            TextStyleAsset local;
            local.effects = text.GetStyle();
            local.pixelHeight = text.GetPixelHeight();
            local.color = text.GetColor();
            const auto appearance = ResolveTextStyle(store, text.GetStyleBinding(), local);
            const auto font =
                appearance.font.IsValid() && store ? store->ResolveFont(appearance.font) : text.GetFontAssetPath();
            if (font.empty()) {
                ++rejectedTextRequests_;
                return;
            }
            GameProjectUiTextRequest request{};
            request.sourceId = object.GetRuntimeHandle().ToValue();
            request.order = AddOrder(layout.order, text.GetOrderOffset());
            request.referenceWidth = layout.referenceWidth;
            request.referenceHeight = layout.referenceHeight;
            request.x = layout.x;
            request.y = layout.y;
            request.maxWidth = layout.width;
            request.maxHeight = layout.height;
            request.pivotX = layout.pivotX;
            request.pivotY = layout.pivotY;
            request.rotationRadians = 0;
            request.pixelHeight = appearance.pixelHeight;
            request.textColor = appearance.color;
            request.horizontalAlign = text.GetHorizontalAlign();
            request.verticalAlign = text.GetVerticalAlign();
            appearance.effects.Apply(request);
            request.textColor.a *= layout.opacity;
            request.outlineColor.a *= layout.opacity;
            request.shadowColor.a *= layout.opacity;
            for (auto* color : {&request.textColor, &request.outlineColor, &request.shadowColor}) {
                color->r *= layout.color.x;
                color->g *= layout.color.y;
                color->b *= layout.color.z;
            }
            request.revealAmount *= layout.reveal;
            request.timeSeconds = timeSeconds;
            auto glow = appearance.effects.glow;
            glow.glowColor.a *= layout.opacity;
            glow.glowColor.r *= layout.color.x;
            glow.glowColor.g *= layout.color.y;
            glow.glowColor.b *= layout.color.z;
            if (!presentation.SubmitStyledText(font, content, request, glow, layout.transform))
                ++rejectedTextRequests_;
        });
}

void UiSceneSystem::PreRender(World& world, const FrameContext& frame) {
    // Authoring views render the same Scene components without enabling
    // interaction. This keeps the Editor preview and Play output on one
    // projection path.
    if (presentation_ == nullptr) {
        return;
    }
    UpdateUiDisplaySettings(world, frame);
    BuildPresentation(world, frame, *presentation_, {}, elapsedSeconds_);
}

void UiSceneSystem::BuildPresentation(World& world, const FrameContext&,
                                      GAME_UI::GameUiPresentationService& presentation, SceneObjectId canvas,
                                      float timeSeconds) {
    layouts_.Clear();
    rejectedTextRequests_ = 0;
    SubmitImages(world, presentation, canvas, timeSeconds);
    SubmitTexts(world, presentation, canvas, timeSeconds);
}

} // namespace HIKARI
