#include "Editor/Views/HIKARI_GameUiPreviewTarget.h"
#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceHost.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>

#include "Editor/Project/HIKARI_GameProjectEditorModuleLibrary.h"
#include "Editor/Project/HIKARI_GameProjectEditorDocumentService.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "RenderCore/Texture/HIKARI_DxTexture.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include "Core/Text/HIKARI_UtfPath.h"

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

std::string MakeStableLabel(std::string_view label, std::string_view workspaceId, std::string_view controlId) {
    std::string result(label);
    result += "###GameProject/";
    result += workspaceId;
    result += '/';
    result += controlId;
    return result;
}

} // namespace

class GameProjectEditorWorkspaceUi final : public EDITOR_PROJECT::IEditorWorkspaceUi {
public:
    ~GameProjectEditorWorkspaceUi() override { previewTarget_.Reset(); }

private:
    GameUiPreviewTarget previewTarget_;
    GAME_UI::GameUiPresentationService previewContent_;
    std::filesystem::path previewRoot_;
    GameProjectUiColor previewClear_{};
    uint32_t previewWidth_{}, previewHeight_{};
    uint64_t previewSerial_{}, previewRenderedFence_ = ~uint64_t(0);
    bool previewOpen_{}, previewFailed_{};

public:
    void SetAssets(AssetDatabase& database, AssetRegistry& registry) {
        database_ = &database;
        assets_ = &registry;
    }
    bool TextureField(std::string_view id, std::string_view label, char* buffer, size_t capacity) override {
        if (!assets_ || !buffer || !capacity)
            return false;
        const auto* record = database_->FindByGuid(AssetGuid{buffer});
        const std::string shown = record ? record->displayName : "画像なし";
        bool changed = false;
        const auto assign = [&](std::string_view guid) {
            if (guid.size() >= capacity)
                return;
            std::copy(guid.begin(), guid.end(), buffer);
            buffer[guid.size()] = '\0';
            changed = true;
        };
        const auto stable = MakeStableLabel(label, workspaceId_, id);
        if (ImGui::BeginCombo(stable.c_str(), shown.c_str())) {
            if (ImGui::Selectable(TEXT::LocalizeWindow("editor.noImage", "画像なし").c_str(), buffer[0] == '\0'))
                assign({});
            for (const auto* descriptor : assets_->CollectByType(AssetType::Texture)) {
                const auto* asset = database_->FindByGuid(descriptor->id);
                const auto name = asset ? asset->displayName : descriptor->sourcePath;
                ImGui::PushID(descriptor->id.value.c_str());
                if (ImGui::Selectable(name.c_str(), descriptor->id.value == buffer))
                    assign(descriptor->id.value);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        DroppedAssetPayload payload;
        if (AcceptAssetDropOfType(*database_, AssetType::Texture, payload) && payload.record)
            assign(payload.guid.value);
        return changed;
    }
    bool CanvasImage(std::string_view guid, EDITOR_PROJECT::EditorWorkspacePoint minimum,
                     EDITOR_PROJECT::EditorWorkspacePoint maximum, EDITOR_PROJECT::EditorWorkspaceColor tint) override {
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
    void CanvasFill(EDITOR_PROJECT::EditorWorkspacePoint a, EDITOR_PROJECT::EditorWorkspacePoint b,
                    EDITOR_PROJECT::EditorWorkspaceColor color) override {
        if (canvasActive_)
            canvasDrawList_->AddRectFilled(ToScreen(a), ToScreen(b), ToImGuiColor(color), 4.0f);
    }
    void CanvasStyledText(EDITOR_PROJECT::EditorWorkspacePoint p, float height, float wrapWidth,
                          EDITOR_PROJECT::EditorWorkspaceColor color, std::string_view text) override {
        if (canvasActive_)
            canvasDrawList_->AddText(ImGui::GetFont(), std::clamp(height, 4.0f, 256.0f), ToScreen(p),
                                     ToImGuiColor(color), text.data(), text.data() + text.size(),
                                     std::max(wrapWidth, 0.0f));
    }
    bool BeginCanvasPreview(uint32_t width, uint32_t height, GameProjectUiColor clear) override {
        if (!canvasActive_ || !database_ || previewOpen_ || width < 64 || height < 64 || width > 2048 ||
            height > 2048 || previewRenderedFence_ == GFX::GetCurrentContext().currentFrameRetireFenceValue)
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
    bool CanvasPreviewSprite(std::string_view guid, const GameProjectUiSpriteRequest& request) override {
        if (!previewOpen_)
            return false;
        const bool ok = previewContent_.SubmitSpriteAsset(guid, request);
        previewFailed_ |= !ok;
        return ok;
    }
    bool CanvasPreviewText(std::string_view font, std::string_view text,
                           const GameProjectUiTextRequest& request) override {
        if (!previewOpen_)
            return false;
        const bool ok = previewContent_.SubmitText(font, text, request);
        previewFailed_ |= !ok;
        return ok;
    }
    bool EndCanvasPreview(EDITOR_PROJECT::EditorWorkspacePoint minimum,
                          EDITOR_PROJECT::EditorWorkspacePoint maximum) override {
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
    void BeginFrame(std::string_view workspaceId) {
        workspaceId_.assign(workspaceId);
        previewOpen_ = false;
        previewContent_.DiscardPending();
        panelDepth_ = 0u;
        tableDepth_ = 0u;
        canvasActive_ = false;
        canvasDrawList_ = nullptr;
    }

    void SetWorkspaceId(std::string_view workspaceId) { workspaceId_.assign(workspaceId); }

    // 場景の作業区の窓はプロジェクト作業区のドックに入れない。
    void ClearDockRegions() noexcept {
        dockRegions_.fill(0u);
        forceDock_ = false;
    }

    void DrawDockSpace(bool reset) {
        const auto* viewport = ImGui::GetMainViewport();
        const std::string name = "HIKARI_ProjectWorkspace/" + workspaceId_;
        const ImGuiID root = ImGui::GetID(name.c_str());
        forceDock_ = reset || ImGui::DockBuilderGetNode(root) == nullptr;
        if (forceDock_) {
            ImGui::DockBuilderRemoveNode(root);
            ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodePos(root, viewport->WorkPos);
            ImGui::DockBuilderSetNodeSize(root, viewport->WorkSize);
            ImGuiID body = root;
            ImGui::DockBuilderSplitNode(body, ImGuiDir_Up, 0.08f, &dockRegions_[1], &body);
            ImGui::DockBuilderSplitNode(body, ImGuiDir_Left, 0.21f, &dockRegions_[2], &body);
            ImGui::DockBuilderSplitNode(body, ImGuiDir_Right, 0.30f, &dockRegions_[3], &body);
            ImGui::DockBuilderSplitNode(body, ImGuiDir_Down, 0.25f, &dockRegions_[4], &body);
            dockRegions_[0] = body;
            ImGui::DockBuilderFinish(root);
        } else {
            // 保存済みレイアウトは ImGui が管理する。新規パネルだけ中央へ入れる。
            dockRegions_.fill(root);
        }
        HIKARI::EDITOR::DrawWorkspaceDockSpace(root, viewport);
    }

    void PrepareSceneViewportWindow() {
        if (dockRegions_[0] != 0u) {
            ImGui::SetNextWindowDockID(dockRegions_[0], forceDock_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
        }
    }

    void EndFrame() noexcept {
        if (canvasActive_) {
            EndCanvas();
        }
        while (tableDepth_ > 0u) {
            ImGui::EndTable();
            --tableDepth_;
        }
        while (panelDepth_ > 0u) {
            ImGui::End();
            --panelDepth_;
        }
        workspaceId_.clear();
    }

    bool BeginPanel(std::string_view stablePanelId, std::string_view displayName, bool* open,
                    EDITOR_PROJECT::EditorWorkspaceDockRegion region) override {
        const std::string label = MakeStableLabel(displayName, workspaceId_, stablePanelId);
        ++panelDepth_;
        const size_t slot = static_cast<size_t>(region);
        if (slot < dockRegions_.size() && dockRegions_[slot]) {
            ImGui::SetNextWindowDockID(dockRegions_[slot], forceDock_ ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
        }
        return ImGui::Begin(label.c_str(), open);
    }

    EDITOR_PROJECT::EditorWorkspaceInteraction GetInteraction() const override {
        const auto& io = ImGui::GetIO();
        const bool shortcuts = !io.WantTextInput;
        return {
            ImGui::IsAnyItemActive() || ImGui::IsMouseDown(0) ? 1u : 0u,
            shortcuts && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false) ? 1u : 0u,
            shortcuts && io.KeyCtrl &&
                    (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))
                ? 1u
                : 0u,
            shortcuts && ImGui::IsKeyPressed(ImGuiKey_Delete, false) ? 1u : 0u,
        };
    }

    bool InputTextMultiline(std::string_view id, std::string_view label, char* buffer, size_t capacity,
                            float height) override {
        if (!buffer || !capacity)
            return false;
        Text(label);
        return ImGui::InputTextMultiline(MakeStableLabel(label, workspaceId_, id).c_str(), buffer, capacity,
                                         ImVec2(-1.0f, height));
    }

    void EndPanel() override {
        if (panelDepth_ == 0u) {
            return;
        }
        if (canvasActive_) {
            EndCanvas();
        }
        while (tableDepth_ > 0u) {
            ImGui::EndTable();
            --tableDepth_;
        }
        ImGui::End();
        --panelDepth_;
    }

    void Text(std::string_view utf8Text) override {
        ImGui::TextUnformatted(utf8Text.data(), utf8Text.data() + utf8Text.size());
    }

    void Separator() override { ImGui::Separator(); }

    void SameLine() override { ImGui::SameLine(); }

    bool Button(std::string_view stableControlId, std::string_view label) override {
        const std::string stableLabel = MakeStableLabel(label, workspaceId_, stableControlId);
        return ImGui::Button(stableLabel.c_str());
    }

    bool InputText(std::string_view stableControlId, std::string_view label, char* utf8Buffer,
                   size_t bufferCapacity) override {
        if (utf8Buffer == nullptr || bufferCapacity == 0u) {
            return false;
        }
        const std::string stableLabel = MakeStableLabel(label, workspaceId_, stableControlId);
        return ImGui::InputText(stableLabel.c_str(), utf8Buffer, bufferCapacity);
    }

    bool Checkbox(std::string_view stableControlId, std::string_view label, bool* value) override {
        if (value == nullptr) {
            return false;
        }
        const std::string stableLabel = MakeStableLabel(label, workspaceId_, stableControlId);
        return ImGui::Checkbox(stableLabel.c_str(), value);
    }

    bool InputInt(std::string_view stableControlId, std::string_view label, int32_t* value,
                  EDITOR_PROJECT::EditorWorkspaceIntInputOptions options) override {
        if (value == nullptr) {
            return false;
        }
        const std::string stableLabel = MakeStableLabel(label, workspaceId_, stableControlId);
        int edited = static_cast<int>(*value);
        const bool changed = ImGui::InputInt(stableLabel.c_str(), &edited, std::max(1, static_cast<int>(options.step)));
        if (changed) {
            if (options.clampToRange != 0u && options.minimum <= options.maximum) {
                edited = std::clamp(edited, static_cast<int>(options.minimum), static_cast<int>(options.maximum));
            }
            *value = static_cast<int32_t>(edited);
        }
        return changed;
    }

    bool InputFloat(std::string_view stableControlId, std::string_view label, float* value,
                    EDITOR_PROJECT::EditorWorkspaceFloatInputOptions options) override {
        if (value == nullptr) {
            return false;
        }
        const std::string stableLabel = MakeStableLabel(label, workspaceId_, stableControlId);
        const bool changed = ImGui::InputFloat(stableLabel.c_str(), value, std::max(0.0f, options.step));
        if (changed && options.clampToRange != 0u && options.minimum <= options.maximum) {
            *value = std::clamp(*value, options.minimum, options.maximum);
        }
        return changed;
    }

    bool InputFloat2(std::string_view stableControlId, std::string_view label,
                     EDITOR_PROJECT::EditorWorkspacePoint* value,
                     EDITOR_PROJECT::EditorWorkspaceFloatInputOptions options) override {
        if (value == nullptr) {
            return false;
        }
        const std::string stableLabel = MakeStableLabel(label, workspaceId_, stableControlId);
        float values[2]{value->x, value->y};
        const float speed = std::max(0.001f, options.step);
        const bool hasRange = options.clampToRange != 0u && options.minimum <= options.maximum;
        const float minimum = hasRange ? options.minimum : 0.0f;
        const float maximum = hasRange ? options.maximum : 0.0f;
        const bool changed = ImGui::DragFloat2(stableLabel.c_str(), values, speed, minimum, maximum);
        if (changed) {
            value->x = values[0];
            value->y = values[1];
            if (hasRange) {
                value->x = std::clamp(value->x, options.minimum, options.maximum);
                value->y = std::clamp(value->y, options.minimum, options.maximum);
            }
        }
        return changed;
    }

    bool Color4(std::string_view stableControlId, std::string_view label,
                EDITOR_PROJECT::EditorWorkspaceColor* value) override {
        if (value == nullptr) {
            return false;
        }
        const std::string stableLabel = MakeStableLabel(label, workspaceId_, stableControlId);
        float color[4]{value->r, value->g, value->b, value->a};
        const bool changed = ImGui::ColorEdit4(stableLabel.c_str(), color);
        if (changed) {
            value->r = color[0];
            value->g = color[1];
            value->b = color[2];
            value->a = color[3];
        }
        return changed;
    }

    bool Selectable(std::string_view stableControlId, std::string_view label, bool selected) override {
        const std::string stableLabel = MakeStableLabel(label, workspaceId_, stableControlId);
        return ImGui::Selectable(stableLabel.c_str(), selected);
    }

    bool BeginTable(std::string_view stableTableId, uint32_t columnCount) override {
        if (columnCount == 0u || columnCount > 64u || panelDepth_ == 0u || canvasActive_) {
            return false;
        }
        const std::string stableLabel = MakeStableLabel("Table", workspaceId_, stableTableId);
        if (!ImGui::BeginTable(stableLabel.c_str(), static_cast<int>(columnCount),
                               ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            return false;
        }
        ++tableDepth_;
        return true;
    }

    void TableNextRow() override {
        if (tableDepth_ != 0u) {
            ImGui::TableNextRow();
        }
    }

    bool TableNextColumn() override { return tableDepth_ != 0u && ImGui::TableNextColumn(); }

    void EndTable() override {
        if (tableDepth_ == 0u) {
            return;
        }
        if (canvasActive_) {
            EndCanvas();
        }
        ImGui::EndTable();
        --tableDepth_;
    }

    EDITOR_PROJECT::EditorWorkspaceCanvasInput BeginCanvas(std::string_view stableCanvasId,
                                                           float requestedHeight) override {
        if (canvasActive_ || panelDepth_ == 0u) {
            return {};
        }

        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float height =
            std::max(1.0f, requestedHeight > 0.0f ? requestedHeight : ImGui::GetContentRegionAvail().y);
        const std::string label = MakeStableLabel("Canvas", workspaceId_, stableCanvasId);
        ImGui::InvisibleButton(label.c_str(), ImVec2(width, height),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                   ImGuiButtonFlags_MouseButtonMiddle);

        canvasMinimum_ = ImGui::GetItemRectMin();
        canvasSize_ = ImGui::GetItemRectSize();
        canvasDrawList_ = ImGui::GetWindowDrawList();
        canvasDrawList_->AddRectFilled(canvasMinimum_,
                                       ImVec2(canvasMinimum_.x + canvasSize_.x, canvasMinimum_.y + canvasSize_.y),
                                       IM_COL32(24, 27, 32, 255));
        canvasDrawList_->AddRect(canvasMinimum_,
                                 ImVec2(canvasMinimum_.x + canvasSize_.x, canvasMinimum_.y + canvasSize_.y),
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

    void EndCanvas() override {
        if (!canvasActive_) {
            return;
        }
        canvasDrawList_->PopClipRect();
        canvasActive_ = false;
        canvasDrawList_ = nullptr;
    }

    void CanvasLine(EDITOR_PROJECT::EditorWorkspacePoint from, EDITOR_PROJECT::EditorWorkspacePoint to,
                    EDITOR_PROJECT::EditorWorkspaceColor color, float thickness) override {
        if (!canvasActive_) {
            return;
        }
        canvasDrawList_->AddLine(ToScreen(from), ToScreen(to), ToImGuiColor(color), std::max(0.5f, thickness));
    }

    void CanvasRect(EDITOR_PROJECT::EditorWorkspacePoint minimum, EDITOR_PROJECT::EditorWorkspacePoint maximum,
                    EDITOR_PROJECT::EditorWorkspaceColor color, float thickness) override {
        if (!canvasActive_) {
            return;
        }
        canvasDrawList_->AddRect(ToScreen(minimum), ToScreen(maximum), ToImGuiColor(color), 0.0f, 0,
                                 std::max(0.5f, thickness));
    }

    void CanvasCircle(EDITOR_PROJECT::EditorWorkspacePoint center, float radius,
                      EDITOR_PROJECT::EditorWorkspaceColor color, float thickness) override {
        if (!canvasActive_) {
            return;
        }
        canvasDrawList_->AddCircle(ToScreen(center), std::max(0.0f, radius), ToImGuiColor(color), 0,
                                   std::max(0.5f, thickness));
    }

    void CanvasText(EDITOR_PROJECT::EditorWorkspacePoint position, EDITOR_PROJECT::EditorWorkspaceColor color,
                    std::string_view utf8Text) override {
        if (!canvasActive_) {
            return;
        }
        canvasDrawList_->AddText(ToScreen(position), ToImGuiColor(color), utf8Text.data(),
                                 utf8Text.data() + utf8Text.size());
    }

private:
    ImVec2 ToScreen(EDITOR_PROJECT::EditorWorkspacePoint point) const noexcept {
        return {
            canvasMinimum_.x + point.x,
            canvasMinimum_.y + point.y,
        };
    }

    std::string workspaceId_{};
    AssetDatabase* database_{};
    AssetRegistry* assets_{};
    size_t panelDepth_ = 0u;
    size_t tableDepth_ = 0u;
    bool canvasActive_ = false;
    std::array<ImGuiID, 5> dockRegions_{};
    bool forceDock_ = false;
    ImVec2 canvasMinimum_{};
    ImVec2 canvasSize_{};
    ImDrawList* canvasDrawList_ = nullptr;
};

GameProjectEditorWorkspaceHost::GameProjectEditorWorkspaceHost()
    : ui_(std::make_unique<GameProjectEditorWorkspaceUi>()) {}

GameProjectEditorWorkspaceHost::~GameProjectEditorWorkspaceHost() = default;

bool GameProjectEditorWorkspaceHost::RegisterModule(const GameProjectEditorModuleLibrary& module,
                                                    std::string_view projectRootUtf8,
                                                    EditorOperationRegistry& operationRegistry,
                                                    std::string& outMessage) {
    if (!module.IsLoaded()) {
        outMessage.clear();
        return true;
    }
    const EditorOperationOwnerToken operationOwnerToken = operationRegistry.CreateOwnerToken();
    if (operationOwnerToken == 0u) {
        outMessage = "Could not allocate an Editor operation owner token for the GameProject module.";
        return false;
    }
    // 文書サービスは項目に一つ。二つ目以降のモジュールは最初の登録で作ったものを共有する。
    const bool firstModule = documents_ == nullptr;
    if (firstModule) {
        documents_ = std::make_unique<GameProjectEditorDocumentService>(TEXT::PathFromUtf8(projectRootUtf8) / "Data");
        registry_.SetDocuments(documents_.get());
    }
    const auto discardDocumentsOfFirstModule = [&]() {
        if (firstModule) {
            registry_.SetDocuments(nullptr);
            documents_.reset();
        }
    };
    std::string operationMessage{};
    if (!module.RegisterEditorOperations(operationRegistry, operationOwnerToken, documents_.get(), operationMessage,
                                         &registry_)) {
        (void)operationRegistry.UnregisterOwner(operationOwnerToken);
        discardDocumentsOfFirstModule();
        outMessage = "GameProject Editor operation registration failed: " + operationMessage;
        return false;
    }

    registry_.SetProjectRootUtf8(std::string(projectRootUtf8));
    std::vector<EDITOR_PROJECT::IEditorWorkspace*> workspaces{};
    workspaces.reserve(module.GetWorkspaceCount());
    for (size_t index = 0u; index < module.GetWorkspaceCount(); ++index) {
        workspaces.push_back(module.GetWorkspace(index));
    }
    const bool registered =
        registry_.RegisterModule(module.GetModuleId(), workspaces.data(), workspaces.size(), outMessage);
    if (registered) {
        operationRegistry_ = &operationRegistry;
        operationOwnerTokens_.push_back(operationOwnerToken);
        extensions_.Add(module);
    } else {
        (void)operationRegistry.UnregisterOwner(operationOwnerToken);
        discardDocumentsOfFirstModule();
    }
    return registered;
}

void GameProjectEditorWorkspaceHost::DrawModulePanels(AssetDatabase& database, AssetRegistry& assets) {
    if (extensions_.GetPanels().empty()) {
        return;
    }
    ui_->SetAssets(database, assets);
    ui_->BeginFrame("scene");
    ui_->ClearDockRegions();
    extensions_.DrawPanels(*ui_);
    ui_->EndFrame();
}

void GameProjectEditorWorkspaceHost::ReportModuleFailure(std::string message) {
    moduleFailureMessage_ = std::move(message);
}

void GameProjectEditorWorkspaceHost::UpdateNavigation(EditorWorkspaceHost& host, float unscaledDeltaSeconds) {
    if (!IsAvailable())
        return;
    if (!requestedWorkspace_.empty() && !host.IsActive(EditorWorkspaceId::Project))
        (void)host.RequestOpen({EditorWorkspaceId::Project});
    ui_->BeginFrame(registry_.GetActiveWorkspaceId());
    registry_.BeginFrame(*ui_, unscaledDeltaSeconds);
    if (host.IsActive(EditorWorkspaceId::Project)) {
        if (const auto* pending = host.PeekPending(); pending && pending->workspaceId != EditorWorkspaceId::Project) {
            guardedNavigation_ = *pending;
            host.CancelPending();
            (void)registry_.RequestCloseActiveWorkspace();
        }
        if (!requestedWorkspace_.empty()) {
            (void)registry_.RequestActivateWorkspace(requestedWorkspace_);
            requestedWorkspace_.clear();
        }
    }
    registry_.ProcessPending();
    const bool wasAwaiting = registry_.IsAwaitingCloseResolution();
    DrawClosePrompt();
    if (guardedNavigation_ && !registry_.HasActiveWorkspace()) {
        (void)host.RequestOpen(*guardedNavigation_);
        guardedNavigation_.reset();
    } else if (wasAwaiting && !registry_.IsAwaitingCloseResolution()) {
        guardedNavigation_.reset();
    }
    registry_.EndFrame();
    ui_->EndFrame();
}

void GameProjectEditorWorkspaceHost::Draw(EditorWorkspaceHost& host, float unscaledDeltaSeconds,
                                          AssetDatabase& database, AssetRegistry& assets) {
    if (!IsAvailable())
        return;
    ui_->SetAssets(database, assets);
    ui_->BeginFrame(registry_.GetActiveWorkspaceId());
    registry_.BeginFrame(*ui_, unscaledDeltaSeconds);
    if (!requestedWorkspace_.empty()) {
        (void)registry_.RequestActivateWorkspace(requestedWorkspace_);
        requestedWorkspace_.clear();
    }
    registry_.ProcessPending();
    ui_->SetWorkspaceId(registry_.GetActiveWorkspaceId());
    ui_->DrawDockSpace(resetLayout_ || host.ConsumeReset(EditorWorkspaceId::Project));
    resetLayout_ = false;
    DrawWorkspaceSelector(host);
    DrawClosePrompt();
    registry_.DrawActive();
    registry_.EndFrame();
    ui_->EndFrame();
    if (!registry_.HasActiveWorkspace() && !registry_.IsAwaitingCloseResolution()) {
        (void)host.RequestOpen({EditorWorkspaceId::Scene});
    }
}

void GameProjectEditorWorkspaceHost::Shutdown() noexcept {
    if (ui_) {
        ui_->BeginFrame(registry_.GetActiveWorkspaceId());
        registry_.Shutdown(*ui_);
        ui_->EndFrame();
    }
    if (operationRegistry_ != nullptr) {
        for (const uint64_t token : operationOwnerTokens_) {
            (void)operationRegistry_->UnregisterOwner(token);
        }
    }
    registry_.SetDocuments(nullptr);
    if (documents_) {
        documents_->CloseAll();
        documents_.reset();
    }
    operationRegistry_ = nullptr;
    operationOwnerTokens_.clear();
    (void)extensions_.ActivateTool({});
    extensions_.Clear();
    moduleFailureMessage_.clear();
    closePromptWasPending_ = false;
}

bool GameProjectEditorWorkspaceHost::IsAvailable() const noexcept {
    return registry_.GetWorkspaceCount() != 0u || !moduleFailureMessage_.empty();
}

bool GameProjectEditorWorkspaceHost::ActiveWorkspaceRequestsSceneViewport() const noexcept {
    return registry_.ActiveWorkspaceRequestsSceneViewport();
}

bool GameProjectEditorWorkspaceHost::HasUnsavedChanges() const noexcept {
    return (documents_ != nullptr && documents_->HasUnsavedDocuments()) || registry_.HasUnsavedWorkspaceChanges();
}

void GameProjectEditorWorkspaceHost::PrepareSceneViewportWindow() {
    if (ui_ && ActiveWorkspaceRequestsSceneViewport()) {
        ui_->PrepareSceneViewportWindow();
    }
}

GameProjectEditorWorkspaceRegistry& GameProjectEditorWorkspaceHost::Registry() noexcept { return registry_; }

const GameProjectEditorWorkspaceRegistry& GameProjectEditorWorkspaceHost::Registry() const noexcept {
    return registry_;
}

void GameProjectEditorWorkspaceHost::DrawWorkspaceEntries(EditorWorkspaceHost& host, const char* filter) {
    if (!IsAvailable())
        return;
    ImGui::SeparatorText(TEXT::Localize("shell.projectWorkspaces").c_str());
    for (size_t i = 0; i < registry_.GetWorkspaceCount(); ++i) {
        const auto info = registry_.GetWorkspaceInfo(i);
        if (filter && filter[0] && info.displayName.find(filter) == std::string_view::npos)
            continue;
        ImGui::BeginDisabled(info.disabled);
        if (IconTextButton(EditorGlyph::Grid, std::string(info.displayName).c_str(),
                           std::string(info.workspaceId).c_str(),
                           info.active && host.IsActive(EditorWorkspaceId::Project) ? EditorButtonTone::Primary
                                                                                    : EditorButtonTone::Quiet,
                           {-1, 36})) {
            requestedWorkspace_.assign(info.workspaceId);
            (void)host.RequestOpen({EditorWorkspaceId::Project});
        }
        ImGui::EndDisabled();
    }
    if (!moduleFailureMessage_.empty())
        ImGui::TextWrapped("%s", moduleFailureMessage_.c_str());
}

void GameProjectEditorWorkspaceHost::DrawWorkspaceSelector(EditorWorkspaceHost& host) {
    if (ui_->BeginPanel("HostToolbar", TEXT::Localize("shell.workspaces"), nullptr,
                        EDITOR_PROJECT::EditorWorkspaceDockRegion::Toolbar)) {
        if (IconButton(EditorGlyph::Scene, "backToScene", EditorButtonTone::Quiet, {},
                       TEXT::Localize("menu.workspace.scene").c_str()))
            (void)host.RequestOpen({EditorWorkspaceId::Scene});
        ImGui::SameLine();
        if (IconButton(EditorGlyph::Save, "saveProjectWorkspace", EditorButtonTone::Quiet, {},
                       TEXT::Localize("menu.edit.save").c_str()))
            (void)registry_.RequestSaveActiveWorkspace();
        ImGui::SameLine();
        if (IconButton(EditorGlyph::Refresh, "resetProjectLayout", EditorButtonTone::Quiet, {},
                       TEXT::Localize("shell.resetLayout").c_str()))
            resetLayout_ = true;
        if (registry_.IsActiveWorkspaceDirty()) {
            ImGui::SameLine();
            StatusBadge(TEXT::Localize("shell.unsaved").c_str(), EditorStatusTone::Warning);
        }
        if (!registry_.GetStatusMessage().empty()) {
            ImGui::TextUnformatted(registry_.GetStatusMessage().data(),
                                   registry_.GetStatusMessage().data() + registry_.GetStatusMessage().size());
        }
    }
    ui_->EndPanel();
}

void GameProjectEditorWorkspaceHost::DrawClosePrompt() {
    const bool pending = registry_.IsAwaitingCloseResolution();
    if (pending && !closePromptWasPending_) {
        ImGui::OpenPopup(TEXT::LocalizeWindow("shell.unsaved", "Unsaved Project Workspace").c_str());
    }
    closePromptWasPending_ = pending;
    if (!pending) {
        return;
    }

    if (ImGui::BeginPopupModal(TEXT::LocalizeWindow("shell.unsaved", "Unsaved Project Workspace").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(TEXT::Localize("shell.saveBeforeLeaving").c_str(),
                           std::string(registry_.GetAwaitingWorkspaceName()).c_str());
        ImGui::Separator();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.common.save", "Save").c_str())) {
            registry_.ResolvePendingClose(GameProjectWorkspaceCloseResolution::Save);
            if (!registry_.IsAwaitingCloseResolution()) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.sceneOpen.discard", "Discard").c_str())) {
            registry_.ResolvePendingClose(GameProjectWorkspaceCloseResolution::Discard);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.common.cancel", "Cancel").c_str())) {
            registry_.ResolvePendingClose(GameProjectWorkspaceCloseResolution::Cancel);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace HIKARI::EDITOR
