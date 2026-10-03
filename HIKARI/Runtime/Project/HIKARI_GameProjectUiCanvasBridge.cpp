#include "HIKARI/Project/UiCanvasView.h"
#include "HIKARI/Project/WorldView.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/Capture/HIKARI_SceneCaptureService.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/Components/Rendering/WorldText/HIKARI_WorldTextComponent.h"
#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpriteComponent.h"
#include <cmath>
#include <cstring>

namespace HIKARI {
namespace {
using Service = SCENE_CAPTURE::SceneCaptureService;
World* CanvasWorld(GameProjectWorldHandle handle) {
    World* world = World::ResolveRuntimeWorld(handle.value);
    return world && world->IsOwnerThread() && RuntimeSceneContext::GetCurrentWorld() == world &&
                   !GameProjectSdkAccess::IsWorldClosing(*world)
               ? world
               : nullptr;
}
Service* CanvasService(World* world) { return world ? world->Services().Find<Service>() : nullptr; }
GameProjectSceneCaptureHandle Internal(GameProjectUiCanvasHandle h) { return {h.value, h.epoch}; }
Service::CanvasContent* Content(World* world, GameProjectUiCanvasHandle h) {
    auto* service = CanvasService(world);
    return service && h.IsValid() && h.worldIdentity == world->GetRuntimeIdentity() ? service->Canvas(Internal(h))
                                                                                    : nullptr;
}
} // namespace

UiCanvasView WorldView::GetUiCanvases() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world ? UiCanvasView(GameProjectWorldHandle{world->GetRuntimeIdentity()}) : UiCanvasView{};
}
bool UiCanvasView::IsValid() const noexcept {
    try {
        return CanvasService(CanvasWorld(world_)) != nullptr;
    } catch (...) {
        return false;
    }
}
GameProjectUiCanvasHandle UiCanvasView::Create(const GameProjectUiCanvasDesc& desc) const noexcept {
    try {
        auto* world = CanvasWorld(world_);
        auto* service = CanvasService(world);
        const auto& c = desc.clearColor;
        if (!service || desc.structSize != sizeof(desc) || desc.version != kGameProjectUiCanvasVersion ||
            desc.width < 64 || desc.height < 64 || desc.width > 2048 || desc.height > 2048 || !std::isfinite(c.r) ||
            !std::isfinite(c.g) || !std::isfinite(c.b) || !std::isfinite(c.a) || c.r < 0 || c.g < 0 || c.b < 0 ||
            c.a < 0 || c.r > 1 || c.g > 1 || c.b > 1 || c.a > 1)
            return {};
        const auto h = service->CreateCanvas(desc);
        return {h.value, h.epoch, world->GetRuntimeIdentity()};
    } catch (...) {
        return {};
    }
}
bool UiCanvasView::Destroy(GameProjectUiCanvasHandle h) const noexcept {
    try {
        auto* w = CanvasWorld(world_);
        return Content(w, h) && CanvasService(w)->Destroy(Internal(h));
    } catch (...) {
        return false;
    }
}
bool UiCanvasView::NeedsUpdate(GameProjectUiCanvasHandle h, uint64_t revision) const noexcept {
    try {
        auto* c = Content(CanvasWorld(world_), h);
        return c && revision && !c->updating && c->revision != revision;
    } catch (...) {
        return false;
    }
}
bool UiCanvasView::BeginUpdate(GameProjectUiCanvasHandle h, uint64_t revision) const noexcept {
    try {
        auto* w = CanvasWorld(world_);
        return Content(w, h) && CanvasService(w)->BeginCanvasUpdate(Internal(h), revision);
    } catch (...) {
        return false;
    }
}
bool UiCanvasView::SubmitSprite(GameProjectUiCanvasHandle h, std::string_view path,
                                const GameProjectUiSpriteRequest& request) const noexcept {
    try {
        auto* c = Content(CanvasWorld(world_), h);
        if (!c || !c->updating)
            return false;
        if (++c->drawCount > kGameProjectUiCanvasMaximumDraws || !c->ui.SubmitSprite(path, request)) {
            c->failed = true;
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}
bool UiCanvasView::SubmitText(GameProjectUiCanvasHandle h, std::string_view font, std::string_view text,
                              const GameProjectUiTextRequest& request) const noexcept {
    try {
        auto* c = Content(CanvasWorld(world_), h);
        if (!c || !c->updating)
            return false;
        if (++c->drawCount > kGameProjectUiCanvasMaximumDraws || !c->ui.SubmitText(font, text, request)) {
            c->failed = true;
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}
bool UiCanvasView::SetSourceOnly(GameProjectObjectHandle h, bool enabled) const noexcept {
    try {
        auto* world = CanvasWorld(world_);
        if (!world || !PROJECT::INTERNAL::IsObjectHandleForWorld(h, *world))
            return false;
        auto* object = world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(h));
        if (!object)
            return false;
        if (auto* text = object->GetComponent<WorldTextComponent>()) {
            text->SetCanvasSourceOnly(enabled);
            return true;
        }
        if (auto* sprite = object->GetComponent<WorldSpriteComponent>()) {
            sprite->SetCanvasSourceOnly(enabled);
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}
bool UiCanvasView::SubmitObject(GameProjectUiCanvasHandle h, GameProjectObjectHandle source,
                                const GameProjectUiSpriteRequest& geometry,
                                const GameProjectUiTextRequest& typography) const noexcept {
    try {
        auto* world = CanvasWorld(world_);
        auto* content = Content(world, h);
        if (!content || !content->updating || !PROJECT::INTERNAL::IsObjectHandleForWorld(source, *world))
            return false;
        auto* object = world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(source));
        if (!object) {
            content->failed = true;
            return false;
        }
        if (auto* text = object->GetComponent<WorldTextComponent>()) {
            if (!text->IsVisible() || text->GetOpacity() <= 0 || text->GetText().empty())
                return true;
            auto request = typography;
            const auto* store = world->Services().Find<TextAssetStore>();
            const auto appearance = text->ResolveAppearance(store);
            appearance.effects.Apply(request);
            const float scale = request.pixelHeight / appearance.pixelHeight;
            request.letterSpacing *= scale;
            request.outlineWidth *= scale;
            request.shadowOffsetX *= scale;
            request.shadowOffsetY *= scale;
            request.textColor = appearance.color;
            const float opacity = text->GetOpacity() * geometry.tint.a;
            request.textColor.a *= opacity;
            request.outlineColor.a *= opacity;
            request.shadowColor.a *= opacity;
            request.horizontalAlign = static_cast<GameProjectUiTextAlign>(text->GetHorizontalAlign());
            request.verticalAlign = static_cast<GameProjectUiVerticalAlign>(text->GetVerticalAlign());
            auto glow = appearance.effects.glow;
            glow.glowColor.a *= opacity;
            glow.glowWidth *= scale;
            const auto font =
                appearance.font.IsValid() && store ? store->ResolveFont(appearance.font) : text->GetFontAssetPath();
            if (++content->drawCount > kGameProjectUiCanvasMaximumDraws ||
                !content->ui.SubmitStyledText(font, text->GetText(), request, glow)) {
                content->failed = true;
                return false;
            }
            return true;
        }
        if (auto* sprite = object->GetComponent<WorldSpriteComponent>()) {
            if (sprite->GetSpriteAssetGuid().IsValid()) {
                content->failed = true;
                return false;
            }
            if (!sprite->IsVisible() || sprite->GetOpacity() <= 0 || !sprite->GetTextureAssetGuid().IsValid())
                return true;
            auto request = geometry;
            const auto color = sprite->GetTint();
            request.tint = {color.x, color.y, color.z, sprite->GetOpacity() * geometry.tint.a};
            if (sprite->GetPreserveAspect())
                request.flags |= kGameProjectUiPreserveSpriteAspect;
            if (++content->drawCount > kGameProjectUiCanvasMaximumDraws ||
                !content->ui.SubmitSpriteAsset(sprite->GetTextureAssetGuid().value, request)) {
                content->failed = true;
                return false;
            }
            return true;
        }
        content->failed = true;
        return false;
    } catch (...) {
        auto* c = Content(CanvasWorld(world_), h);
        if (c)
            c->failed = true;
        return false;
    }
}
bool UiCanvasView::CommitUpdate(GameProjectUiCanvasHandle h) const noexcept {
    try {
        auto* w = CanvasWorld(world_);
        return Content(w, h) && CanvasService(w)->EndCanvasUpdate(Internal(h), true);
    } catch (...) {
        return false;
    }
}
bool UiCanvasView::CancelUpdate(GameProjectUiCanvasHandle h) const noexcept {
    try {
        auto* w = CanvasWorld(world_);
        return Content(w, h) && CanvasService(w)->EndCanvasUpdate(Internal(h), false);
    } catch (...) {
        return false;
    }
}
bool UiCanvasView::BindSurface(const GameProjectUiCanvasSurfaceBinding& b) const noexcept {
    try {
        auto* w = CanvasWorld(world_);
        if (!Content(w, b.canvas) || b.structSize != sizeof(b) || b.version != kGameProjectUiCanvasVersion ||
            !PROJECT::INTERNAL::IsObjectHandleForWorld(b.modelObject, *w) ||
            !w->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(b.modelObject)) || !b.materialSlotName[0] ||
            !std::memchr(b.materialSlotName, 0, sizeof(b.materialSlotName)) || !std::isfinite(b.x) ||
            !std::isfinite(b.y) || !std::isfinite(b.width) || !std::isfinite(b.height) || b.x < 0 || b.y < 0 ||
            b.width <= 0 || b.height <= 0 || b.x + b.width > 1.00001f || b.y + b.height > 1.00001f || b.flipX > 1 ||
            b.flipY > 1 || b.quarterTurns > 3)
            return false;
        SCENE_CAPTURE::SceneCaptureMaterialBinding binding{};
        binding.capture = Internal(b.canvas);
        binding.modelObject = PROJECT::INTERNAL::DecodeRuntimeHandle(b.modelObject);
        binding.materialSlotName = b.materialSlotName;
        binding.sourceRect = {b.x, b.y, b.width, b.height};
        binding.flags = b.flipX | (b.flipY << 1);
        binding.quarterTurns = b.quarterTurns;
        return CanvasService(w)->BindMaterial(std::move(binding));
    } catch (...) {
        return false;
    }
}
bool UiCanvasView::ClearSurface(GameProjectUiCanvasHandle h, GameProjectObjectHandle object) const noexcept {
    try {
        auto* w = CanvasWorld(world_);
        return Content(w, h) && PROJECT::INTERNAL::IsObjectHandleForWorld(object, *w) &&
               CanvasService(w)->ClearMaterialBinding(Internal(h), PROJECT::INTERNAL::DecodeRuntimeHandle(object));
    } catch (...) {
        return false;
    }
}
bool UiCanvasView::GetSnapshot(GameProjectUiCanvasHandle h, GameProjectUiCanvasSnapshot& out) const noexcept {
    out = {};
    try {
        auto* w = CanvasWorld(world_);
        auto* c = Content(w, h);
        GameProjectSceneCaptureSnapshot s{};
        if (!c || !CanvasService(w)->TryGetSnapshot(Internal(h), s))
            return false;
        out = {s.width,
               s.height,
               c->revision,
               s.outputRevision,
               s.renderedFrameIndex,
               s.status == GameProjectSceneCaptureStatus::Ready ? 1u : 0u,
               c->updating ? 1u : 0u};
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace HIKARI
