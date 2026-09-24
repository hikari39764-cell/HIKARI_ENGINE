#include "HIKARI/Project/WorldView.h"

#include <cmath>
#include <string>
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/Components/Rendering/WorldText/HIKARI_WorldTextComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
namespace {
WorldTextComponent* ResolveText(const ObjectView& view, GameProjectWorldResult& result) noexcept {
    const auto handle = view.GetHandle();
    RuntimeWorldAccessResult access{};
    World* world = World::ResolveRuntimeWorld(handle.world.value, &access);
    if (!world) {
        result = PROJECT::INTERNAL::MakeWorldAccessFailure(access);
        return nullptr;
    }
    auto code = GameProjectWorldResultCode::Success;
    if (!world->IsOwnerThread())
        code = GameProjectWorldResultCode::WrongThread;
    else if (GameProjectSdkAccess::IsWorldClosing(*world))
        code = GameProjectWorldResultCode::WorldClosing;
    else if (!PROJECT::INTERNAL::IsObjectHandleForWorld(handle, *world))
        code = GameProjectWorldResultCode::InvalidObject;
    if (code != GameProjectWorldResultCode::Success) {
        result = PROJECT::INTERNAL::MakeWorldResult(code, handle);
        return nullptr;
    }
    auto* object = world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(handle));
    auto* text = object ? object->GetComponent<WorldTextComponent>() : nullptr;
    result = PROJECT::INTERNAL::MakeWorldResult(text     ? code
                                                : object ? GameProjectWorldResultCode::Unsupported
                                                         : GameProjectWorldResultCode::InvalidObject,
                                                handle);
    return text;
}
bool Finite(GameProjectFloat3 v) noexcept { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
} // namespace

bool ObjectView::TryGetRuntimeWorldTextState(GameProjectWorldTextState& outState) const noexcept {
    outState = {};
    try {
        GameProjectWorldResult result{};
        auto* text = ResolveText(*this, result);
        if (!text)
            return false;
        auto color = text->GetColor();
        auto outline = text->GetOutlineColor();
        auto size = text->GetLayoutSize();
        outState.color = {color.x, color.y, color.z};
        outState.outlineColor = {outline.x, outline.y, outline.z};
        outState.layoutSize = {size.x, size.y};
        outState.fontHeight = text->GetFontHeight();
        outState.outlineWidth = text->GetOutlineWidth();
        outState.opacity = text->GetOpacity();
        outState.revealAmount = text->GetRevealAmount();
        outState.visible = text->IsVisible() ? 1u : 0u;
        return true;
    } catch (...) {
        return false;
    }
}

GameProjectWorldResult ObjectView::ApplyRuntimeWorldTextPatch(const GameProjectWorldTextPatch& patch) const noexcept {
    GameProjectWorldResult result{};
    try {
        auto* text = ResolveText(*this, result);
        if (!text)
            return result;
        const auto& v = patch.values;
        using F = GameProjectWorldTextPatchField;
        const auto has = [&](F field) { return (patch.fieldMask & GameProjectWorldTextPatchFields(field)) != 0u; };
        if (patch.structSize != sizeof(patch) || patch.version != 1u || patch.reserved || v.structSize != sizeof(v) ||
            v.version != 1u || v.reserved || !patch.fieldMask ||
            (patch.fieldMask & ~kGameProjectWorldTextPatchKnownFields) || (has(F::Visible) && v.visible > 1u) ||
            (has(F::Color) && !Finite(v.color)) || (has(F::OutlineColor) && !Finite(v.outlineColor)) ||
            (has(F::Opacity) && !std::isfinite(v.opacity)) ||
            (has(F::RevealAmount) && !std::isfinite(v.revealAmount)) ||
            (has(F::OutlineWidth) && !std::isfinite(v.outlineWidth)) ||
            (has(F::FontHeight) && (!std::isfinite(v.fontHeight) || v.fontHeight <= 0.0f)) ||
            (has(F::LayoutSize) && (!std::isfinite(v.layoutSize.x) || !std::isfinite(v.layoutSize.y) ||
                                    v.layoutSize.x <= 0.0f || v.layoutSize.y <= 0.0f))) {
            return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidRequest, handle_);
        }
        if (has(F::Visible))
            text->SetVisible(v.visible != 0u);
        if (has(F::Color))
            text->SetColor({v.color.x, v.color.y, v.color.z});
        if (has(F::OutlineColor))
            text->SetOutlineColor({v.outlineColor.x, v.outlineColor.y, v.outlineColor.z});
        if (has(F::OutlineWidth))
            text->SetOutlineWidth(v.outlineWidth);
        if (has(F::Opacity))
            text->SetOpacity(v.opacity);
        if (has(F::RevealAmount))
            text->SetRevealAmount(v.revealAmount);
        if (has(F::LayoutSize))
            text->SetLayoutSize({v.layoutSize.x, v.layoutSize.y});
        if (has(F::FontHeight))
            text->SetFontHeight(v.fontHeight);
        return result;
    } catch (...) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InternalError, handle_);
    }
}

GameProjectWorldResult ObjectView::SetRuntimeWorldTextContent(std::string_view value,
                                                              std::string_view fontAssetPath) const noexcept {
    GameProjectWorldResult result{};
    try {
        auto* text = ResolveText(*this, result);
        if (!text)
            return result;
        if (value.size() > kGameProjectWorldTextMaximumBytes || fontAssetPath.size() > 4096u ||
            value.find('\0') != std::string_view::npos || fontAssetPath.find('\0') != std::string_view::npos) {
            return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidRequest, handle_);
        }
        if (text->GetText() == value && (fontAssetPath.empty() || text->GetFontAssetPath() == fontAssetPath))
            return result;
        // 文字列確保が失敗しても一部だけ更新しない。
        std::string content(value);
        std::string font(fontAssetPath);
        text->SetText(std::move(content));
        if (!font.empty())
            text->SetFontAssetPath(std::move(font));
        return result;
    } catch (...) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InternalError, handle_);
    }
}
} // namespace HIKARI
