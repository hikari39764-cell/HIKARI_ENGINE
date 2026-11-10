#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <imgui.h>

#include "Editor/NodeGraph/HIKARI_NodeGraphView.h"
#include "Editor/Views/HIKARI_GameUiPreviewTarget.h"
#include "HIKARI/Project/EditorWorkspace.h"
#include "RenderContracts/Asset/HIKARI_AssetTypeEnum.h"
#include "RenderCore/Texture/HIKARI_DxTexture.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"

namespace HIKARI {
class AssetDatabase;
class AssetRegistry;
} // namespace HIKARI

namespace HIKARI::EDITOR {

// モジュールの作業区と窓が描くための界面（IEditorWorkspaceUi）の ImGui 実装。
// 窓と基本の控件・表は HIKARI_GameProjectEditorWorkspaceUi.cpp、画布は ...UiCanvas.cpp、
// 資産の選択・項目の面板・節点図は ...UiAuthoring.cpp。
class GameProjectEditorWorkspaceUi final : public EDITOR_PROJECT::IEditorWorkspaceUi {
public:
    ~GameProjectEditorWorkspaceUi() override;

    // ---- 宿主（GameProjectEditorWorkspaceHost）が使う ----
    void SetAssets(AssetDatabase& database, AssetRegistry& registry);
    void BeginFrame(std::string_view workspaceId);
    void SetWorkspaceId(std::string_view workspaceId);
    // 場景の作業区の窓はプロジェクト作業区のドックに入れない。
    void ClearDockRegions() noexcept;
    void DrawDockSpace(bool reset, bool wideCenter);
    void PrepareSceneViewportWindow();
    void EndFrame() noexcept;
    // 編集器の「元に戻す・やり直し」の命令が今のフレームに来たか。作業区は GetInteraction で受け取る。
    void SetHistoryRequest(bool undo, bool redo) noexcept {
        undoRequested_ = undo;
        redoRequested_ = redo;
    }

    // ---- IEditorWorkspaceUi ----
    bool BeginPanel(std::string_view stablePanelId, std::string_view displayName, bool* open,
                    EDITOR_PROJECT::EditorWorkspaceDockRegion region) override;
    EDITOR_PROJECT::EditorWorkspaceInteraction GetInteraction() const override;
    bool InputTextMultiline(std::string_view id, std::string_view label, char* buffer, size_t capacity,
                            float height) override;
    void EndPanel() override;
    void Text(std::string_view utf8Text) override;
    void Separator() override;
    void SameLine() override;
    bool Button(std::string_view stableControlId, std::string_view label) override;
    bool InputText(std::string_view stableControlId, std::string_view label, char* utf8Buffer,
                   size_t bufferCapacity) override;
    bool Checkbox(std::string_view stableControlId, std::string_view label, bool* value) override;
    bool InputInt(std::string_view stableControlId, std::string_view label, int32_t* value,
                  EDITOR_PROJECT::EditorWorkspaceIntInputOptions options) override;
    bool InputFloat(std::string_view stableControlId, std::string_view label, float* value,
                    EDITOR_PROJECT::EditorWorkspaceFloatInputOptions options) override;
    bool InputFloat2(std::string_view stableControlId, std::string_view label,
                     EDITOR_PROJECT::EditorWorkspacePoint* value,
                     EDITOR_PROJECT::EditorWorkspaceFloatInputOptions options) override;
    bool Color4(std::string_view stableControlId, std::string_view label,
                EDITOR_PROJECT::EditorWorkspaceColor* value) override;
    bool Selectable(std::string_view stableControlId, std::string_view label, bool selected) override;
    bool BeginTable(std::string_view stableTableId, uint32_t columnCount) override;
    void TableNextRow() override;
    bool TableNextColumn() override;
    void EndTable() override;
    EDITOR_PROJECT::EditorWorkspaceCanvasInput BeginCanvas(std::string_view stableCanvasId,
                                                           float requestedHeight) override;
    void EndCanvas() override;
    void CanvasLine(EDITOR_PROJECT::EditorWorkspacePoint from, EDITOR_PROJECT::EditorWorkspacePoint to,
                    EDITOR_PROJECT::EditorWorkspaceColor color, float thickness) override;
    void CanvasRect(EDITOR_PROJECT::EditorWorkspacePoint minimum, EDITOR_PROJECT::EditorWorkspacePoint maximum,
                    EDITOR_PROJECT::EditorWorkspaceColor color, float thickness) override;
    void CanvasCircle(EDITOR_PROJECT::EditorWorkspacePoint center, float radius,
                      EDITOR_PROJECT::EditorWorkspaceColor color, float thickness) override;
    void CanvasText(EDITOR_PROJECT::EditorWorkspacePoint position, EDITOR_PROJECT::EditorWorkspaceColor color,
                    std::string_view utf8Text) override;
    bool AssetField(std::string_view stableControlId, std::string_view label, GameProjectAssetKind kind,
                    char* assetGuidUtf8, size_t bufferCapacity) override;
    bool CanvasImage(std::string_view guid, EDITOR_PROJECT::EditorWorkspacePoint minimum,
                     EDITOR_PROJECT::EditorWorkspacePoint maximum, EDITOR_PROJECT::EditorWorkspaceColor tint) override;
    void CanvasFill(EDITOR_PROJECT::EditorWorkspacePoint a, EDITOR_PROJECT::EditorWorkspacePoint b,
                    EDITOR_PROJECT::EditorWorkspaceColor color) override;
    void CanvasStyledText(EDITOR_PROJECT::EditorWorkspacePoint p, float height, float wrapWidth,
                          EDITOR_PROJECT::EditorWorkspaceColor color, std::string_view text) override;
    bool BeginCanvasPreview(uint32_t width, uint32_t height, GameProjectUiColor clear) override;
    bool CanvasPreviewSprite(std::string_view guid, const GameProjectUiSpriteRequest& request) override;
    bool CanvasPreviewText(std::string_view font, std::string_view text,
                           const GameProjectUiTextRequest& request) override;
    bool EndCanvasPreview(EDITOR_PROJECT::EditorWorkspacePoint minimum,
                          EDITOR_PROJECT::EditorWorkspacePoint maximum) override;
    bool Choice(std::string_view stableControlId, std::string_view label, bool selected) override;
    bool SliderFloat(std::string_view stableControlId, std::string_view label, float* value, float minimum,
                     float maximum) override;
    bool Section(std::string_view stableControlId, std::string_view label, bool defaultOpen) override;
    void Tooltip(std::string_view utf8Text) override;
    bool Combo(std::string_view stableControlId, std::string_view label, std::span<const std::string_view> items,
               int32_t* selected) override;
    bool ModuleAssetField(std::string_view stableControlId, std::string_view label, std::string_view moduleAssetTypeId,
                          char* assetGuidUtf8, size_t bufferCapacity) override;
    bool ImageSelectable(std::string_view stableControlId, std::string_view label, const uint8_t* pixels,
                         uint32_t width, uint32_t height, uint64_t revision, bool selected, float rowHeight) override;
    void Fields(std::string_view stableControlId, IPropertyFieldDeclaration& declaration) override;
    void NodeGraph(std::string_view stableGraphId, const EDITOR_PROJECT::EditorNodeGraphDesc& desc,
                   EDITOR_PROJECT::EditorNodeGraphResult& outResult) override;
    void SetClipboardText(std::string_view utf8Text) override;
    size_t GetClipboardText(char* utf8Buffer, size_t bufferCapacity) override;

private:
    struct Thumbnail {
        int handle = -1;
        uint64_t revision = 0u;
    };

    // モジュールが渡す縮図。行ごとに一枚持ち、版が変わった時だけ作り直す。
    D3D12_GPU_DESCRIPTOR_HANDLE ThumbnailTexture(const std::string& key, const uint8_t* pixels, uint32_t width,
                                                 uint32_t height, uint64_t revision);
    static void PushSelectionColor(bool selected);
    static void PopSelectionColor(bool selected);
    ImVec2 ToScreen(EDITOR_PROJECT::EditorWorkspacePoint point) const noexcept;
    // 資産の選択欄（引擎の種類、またはモジュールが登記した種類）。
    bool DrawAssetGuidField(std::string_view stableControlId, std::string_view label, AssetType type,
                            std::string_view moduleAssetTypeId, char* assetGuidUtf8, size_t bufferCapacity);

    std::unordered_map<std::string, Thumbnail> thumbnails_{};
    // 節点図ごとの視点と選択。鍵は作業区と控件の ID。
    std::unordered_map<std::string, NodeGraphView> nodeGraphs_{};
    GameUiPreviewTarget previewTarget_;
    GAME_UI::GameUiPresentationService previewContent_;
    std::filesystem::path previewRoot_;
    GameProjectUiColor previewClear_{};
    uint32_t previewWidth_{}, previewHeight_{};
    uint64_t previewSerial_{}, previewRenderedFence_ = ~uint64_t(0);
    bool previewOpen_{}, previewFailed_{};

    std::string workspaceId_{};
    bool undoRequested_ = false;
    bool redoRequested_ = false;
    AssetDatabase* database_{};
    AssetRegistry* assets_{};
    size_t panelDepth_ = 0u;
    size_t tableDepth_ = 0u;
    bool canvasActive_ = false;
    std::array<ImGuiID, 5> dockRegions_{};
    bool forceDock_ = false;
    int floatingPanelCount_ = 0;
    ImVec2 canvasMinimum_{};
    ImVec2 canvasSize_{};
    ImDrawList* canvasDrawList_ = nullptr;
};

// 控件の ImGui ラベル。表示名が変わっても状態が続くよう、ID は作業区と控件の安定 ID から作る。
std::string MakeWorkspaceControlLabel(std::string_view label, std::string_view workspaceId, std::string_view controlId);

} // namespace HIKARI::EDITOR
