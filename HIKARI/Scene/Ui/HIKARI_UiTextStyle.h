#pragma once
#include "Render2D/HIKARI_GameUiRenderData.h"
#include <json.hpp>

namespace HIKARI {
class IInspectorBuilder;
struct UiTextStyle {
    float lineHeight = 1.15f, letterSpacing = 0, reveal = 1;
    float outlineWidth = 1.5f, shadowX = 2, shadowY = 2;
    GameProjectUiColor outline{.0156863f, .1686275f, .5058824f, 1};
    GameProjectUiColor shadow{0, 0, 0, .55f};
    GameProjectUiTextWrap wrap = GameProjectUiTextWrap::Character;
    GAME_UI::GameUiTextEffectStyle glow{};
    void Write(nlohmann::json& out) const;
    void Read(const nlohmann::json& in);
    void Inspect(IInspectorBuilder& builder, bool contentControls = true);
    void InspectContent(IInspectorBuilder& builder);
    void Apply(GameProjectUiTextRequest& request) const noexcept;
};
} // namespace HIKARI
