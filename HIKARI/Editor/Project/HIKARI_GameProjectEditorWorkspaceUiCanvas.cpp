#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceUi.h"

#include <algorithm>
#include <array>
#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"

// 作業区の界面のうち、モジュールが自分で線・図形・画像を描く画布と、ゲーム UI の試し描き。
namespace HIKARI::EDITOR {
namespace {

ImU32 ToImGuiColor(EDITOR_PROJECT::EditorWorkspaceColor color) noexcept {
    return ImGui::ColorConvertFloat4ToU32(ImVec4{
        std::clamp(color.r, 0.0f, 1.0f),
        std::clamp(color.g, 0.0f, 1.0f),
        std::clamp(color.b, 0.0f, 1.0f),
        std::clamp(color.a, 0.0f, 1.0f),
    });
}

} // namespace

bool GameProjectEditorWorkspaceUi::CanvasImage(std::string_view guid, EDITOR_PROJECT::EditorWorkspacePoint minimum,
                                               EDITOR_PROJECT::EditorWorkspacePoint maximum,
                                               EDITOR_PROJECT::EditorWorkspaceColor tint) {
    if (!canvasActive_ || !assets_ || guid.empty())
        return false;
    const auto* texture = assets_->FindAs<TextureAssetDescriptor>(AssetGuid{std::string(guid)});
    if (!texture || texture->sourcePath.empty())
        return false;
    const auto space = texture->colorSpace == TextureAssetColorSpace::Linear ? DXTEX::TextureColorSpace::Linear
                       : texture->colorSpace == TextureAssetColorSpace::Srgb ? DXTEX::TextureColorSpace::Srgb
                                                                             : DXTEX::TextureColorSpace::Auto;
    const int handle = DXTEX::DxTextureManager::LoadTextureWithColorSpace("editor/workspace/" + std::string(guid),
                                                                          texture->sourcePath, space);
    if (handle < 0)
        return false;
    const auto srv = DXTEX::DxTextureManager::GetSrvGpuHandle(handle);
    if (!srv.ptr)
        return false;
    canvasDrawList_->AddImage(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr)), ToScreen(minimum),
                              ToScreen(maximum), {0, 0}, {1, 1}, ToImGuiColor(tint));
    return true;
}

void GameProjectEditorWorkspaceUi::CanvasFill(EDITOR_PROJECT::EditorWorkspacePoint a,
                                              EDITOR_PROJECT::EditorWorkspacePoint b,
                                              EDITOR_PROJECT::EditorWorkspaceColor color) {
    if (canvasActive_)
        canvasDrawList_->AddRectFilled(ToScreen(a), ToScreen(b), ToImGuiColor(color), 4.0f);
}

void GameProjectEditorWorkspaceUi::CanvasStyledText(EDITOR_PROJECT::EditorWorkspacePoint p, float height,
                                                    float wrapWidth, EDITOR_PROJECT::EditorWorkspaceColor color,
                                                    std::string_view text) {
    if (canvasActive_)
        canvasDrawList_->AddText(ImGui::GetFont(), std::clamp(height, 4.0f, 256.0f), ToScreen(p), ToImGuiColor(color),
                                 text.data(), text.data() + text.size(), std::max(wrapWidth, 0.0f));
}

bool GameProjectEditorWorkspaceUi::BeginCanvasPreview(uint32_t width, uint32_t height, GameProjectUiColor clear) {
    if (!canvasActive_ || !database_ || previewOpen_ || width < 64 || height < 64 || width > 2048 || height > 2048 ||
        previewRenderedFence_ == GFX::GetCurrentContext().currentFrameRetireFenceValue)
        return false;
    const auto root = database_->GetProjectRoot();
    if (previewRoot_ != root) {
        previewRoot_ = root;
        previewContent_.SetProjectRoot(root);
        previewContent_.SetAssetDatabase(database_);
    }
    previewContent_.DiscardPending();
    previewWidth_ = width;
    previewHeight_ = height;
    previewClear_ = clear;
    previewOpen_ = true;
    previewFailed_ = false;
    return true;
}

bool GameProjectEditorWorkspaceUi::CanvasPreviewSprite(std::string_view guid,
                                                       const GameProjectUiSpriteRequest& request) {
    if (!previewOpen_)
        return false;
    const bool ok = previewContent_.SubmitSpriteAsset(guid, request);
    previewFailed_ |= !ok;
    return ok;
}

bool GameProjectEditorWorkspaceUi::CanvasPreviewText(std::string_view font, std::string_view text,
                                                     const GameProjectUiTextRequest& request) {
    if (!previewOpen_)
        return false;
    const bool ok = previewContent_.SubmitText(font, text, request);
    previewFailed_ |= !ok;
    return ok;
}

bool GameProjectEditorWorkspaceUi::EndCanvasPreview(EDITOR_PROJECT::EditorWorkspacePoint minimum,
                                                    EDITOR_PROJECT::EditorWorkspacePoint maximum) {
    if (!previewOpen_)
        return false;
    previewOpen_ = false;
    if (previewFailed_) {
        previewContent_.DiscardPending();
        return false;
    }
    previewContent_.Tick(++previewSerial_);
    const auto& graphics = GFX::GetCurrentContext();
    const auto srv =
        previewTarget_.Render(previewContent_.GetFrameSource(), previewWidth_, previewHeight_, previewClear_);
    previewRenderedFence_ = graphics.currentFrameRetireFenceValue;
    if (!srv.ptr)
        return false;
    canvasDrawList_->AddImage(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr)), ToScreen(minimum),
                              ToScreen(maximum));
    return true;
}

EDITOR_PROJECT::EditorWorkspaceCanvasInput GameProjectEditorWorkspaceUi::BeginCanvas(std::string_view stableCanvasId,
                                                                                     float requestedHeight) {
    if (canvasActive_ || panelDepth_ == 0u) {
        return {};
    }

    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    const float height = std::max(1.0f, requestedHeight > 0.0f ? requestedHeight : ImGui::GetContentRegionAvail().y);
    const std::string label = MakeWorkspaceControlLabel("Canvas", workspaceId_, stableCanvasId);
    ImGui::InvisibleButton(label.c_str(), ImVec2(width, height),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);

    canvasMinimum_ = ImGui::GetItemRectMin();
    canvasSize_ = ImGui::GetItemRectSize();
    canvasDrawList_ = ImGui::GetWindowDrawList();
    canvasDrawList_->AddRectFilled(canvasMinimum_,
                                   ImVec2(canvasMinimum_.x + canvasSize_.x, canvasMinimum_.y + canvasSize_.y),
                                   IM_COL32(24, 27, 32, 255));
    canvasDrawList_->AddRect(canvasMinimum_, ImVec2(canvasMinimum_.x + canvasSize_.x, canvasMinimum_.y + canvasSize_.y),
                             IM_COL32(78, 86, 98, 255));
    canvasDrawList_->PushClipRect(canvasMinimum_,
                                  ImVec2(canvasMinimum_.x + canvasSize_.x, canvasMinimum_.y + canvasSize_.y), true);
    canvasActive_ = true;

    const ImGuiIO& io = ImGui::GetIO();
    EDITOR_PROJECT::EditorWorkspaceCanvasInput input{};
    input.size = {canvasSize_.x, canvasSize_.y};
    input.pointer = {
        io.MousePos.x - canvasMinimum_.x,
        io.MousePos.y - canvasMinimum_.y,
    };
    input.hovered = ImGui::IsItemHovered();
    input.wheel = input.hovered ? ImGui::GetIO().MouseWheel : 0.0f;
    input.shift = ImGui::GetIO().KeyShift ? 1u : 0u;
    input.control = ImGui::GetIO().KeyCtrl ? 1u : 0u;
    const bool receivesPointer = input.hovered || ImGui::IsItemActive();
    const std::array<int, 3u> buttons{0, 1, 2};
    const std::array<uint32_t, 3u> masks{
        static_cast<uint32_t>(EDITOR_PROJECT::EditorWorkspacePointerButton::Primary),
        static_cast<uint32_t>(EDITOR_PROJECT::EditorWorkspacePointerButton::Secondary),
        static_cast<uint32_t>(EDITOR_PROJECT::EditorWorkspacePointerButton::Middle),
    };
    for (size_t index = 0u; index < buttons.size(); ++index) {
        if (receivesPointer && ImGui::IsMouseDown(buttons[index])) {
            input.heldButtons |= masks[index];
        }
        if (receivesPointer && ImGui::IsMouseClicked(buttons[index])) {
            input.pressedButtons |= masks[index];
        }
        if (receivesPointer && ImGui::IsMouseReleased(buttons[index])) {
            input.releasedButtons |= masks[index];
        }
    }
    return input;
}

void GameProjectEditorWorkspaceUi::EndCanvas() {
    if (!canvasActive_) {
        return;
    }
    canvasDrawList_->PopClipRect();
    canvasActive_ = false;
    canvasDrawList_ = nullptr;
}

void GameProjectEditorWorkspaceUi::CanvasLine(EDITOR_PROJECT::EditorWorkspacePoint from,
                                              EDITOR_PROJECT::EditorWorkspacePoint to,
                                              EDITOR_PROJECT::EditorWorkspaceColor color, float thickness) {
    if (!canvasActive_) {
        return;
    }
    canvasDrawList_->AddLine(ToScreen(from), ToScreen(to), ToImGuiColor(color), std::max(0.5f, thickness));
}

void GameProjectEditorWorkspaceUi::CanvasRect(EDITOR_PROJECT::EditorWorkspacePoint minimum,
                                              EDITOR_PROJECT::EditorWorkspacePoint maximum,
                                              EDITOR_PROJECT::EditorWorkspaceColor color, float thickness) {
    if (!canvasActive_) {
        return;
    }
    canvasDrawList_->AddRect(ToScreen(minimum), ToScreen(maximum), ToImGuiColor(color), 0.0f, 0,
                             std::max(0.5f, thickness));
}

void GameProjectEditorWorkspaceUi::CanvasCircle(EDITOR_PROJECT::EditorWorkspacePoint center, float radius,
                                                EDITOR_PROJECT::EditorWorkspaceColor color, float thickness) {
    if (!canvasActive_) {
        return;
    }
    canvasDrawList_->AddCircle(ToScreen(center), std::max(0.0f, radius), ToImGuiColor(color), 0,
                               std::max(0.5f, thickness));
}

void GameProjectEditorWorkspaceUi::CanvasText(EDITOR_PROJECT::EditorWorkspacePoint position,
                                              EDITOR_PROJECT::EditorWorkspaceColor color, std::string_view utf8Text) {
    if (!canvasActive_) {
        return;
    }
    canvasDrawList_->AddText(ToScreen(position), ToImGuiColor(color), utf8Text.data(),
                             utf8Text.data() + utf8Text.size());
}

ImVec2 GameProjectEditorWorkspaceUi::ToScreen(EDITOR_PROJECT::EditorWorkspacePoint point) const noexcept {
    return {
        canvasMinimum_.x + point.x,
        canvasMinimum_.y + point.y,
    };
}

} // namespace HIKARI::EDITOR
