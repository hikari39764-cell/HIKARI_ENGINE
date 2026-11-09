#pragma once

#include <memory>
#include <string>

#include "HIKARI_TerrainKitEditorCommon.h"

// 地形の作業区の頁。頁ごとに左の列（選ぶ物の一覧）と右の列（選んだ物の設定）を描く。
namespace HIKARI::TERRAINKIT {

class TerrainPage {
public:
    explicit TerrainPage(std::shared_ptr<EditorShared> shared) : shared_(std::move(shared)) {}
    virtual ~TerrainPage() = default;

    virtual const char* Name() const = 0;
    virtual const char* Help() const = 0;
    // この頁を開いた時に視口で有効にする工具。無ければ空。
    virtual const char* ToolId() const = 0;
    virtual void OnOpen(TerrainStore&) {}
    virtual void DrawList(EDITOR_PROJECT::IEditorWorkspaceUi& ui, TerrainStore& store) = 0;
    virtual void DrawProperties(EDITOR_PROJECT::IEditorWorkspaceUi& ui, TerrainStore& store) = 0;

protected:
    void Report(const std::string& error) const {
        if (shared_->commands != nullptr && !error.empty()) {
            shared_->commands->PublishStatus(EDITOR_PROJECT::EditorWorkspaceStatusSeverity::Error, error);
        }
    }

    void ActivateTool(const char* toolId) const {
        if (shared_->services != nullptr) {
            shared_->services->ActivateViewportTool(toolId);
        }
    }

    std::shared_ptr<EditorShared> shared_;
};

std::unique_ptr<TerrainPage> MakeGeneratePage(std::shared_ptr<EditorShared> shared);
std::unique_ptr<TerrainPage> MakeShapePage(std::shared_ptr<EditorShared> shared);
std::unique_ptr<TerrainPage> MakeSurfacePage(std::shared_ptr<EditorShared> shared);
std::unique_ptr<TerrainPage> MakeGrassPage(std::shared_ptr<EditorShared> shared);
std::unique_ptr<TerrainPage> MakePlacementPage(std::shared_ptr<EditorShared> shared);
std::unique_ptr<TerrainPage> MakeRoadsPage(std::shared_ptr<EditorShared> shared);

// 筆の形の一覧（絵つき）。
void DrawBrushShapes(EDITOR_PROJECT::IEditorWorkspaceUi& ui, const TerrainStore& store, EditorShared& shared);

// 材質の枠を名前で選ぶ下拉。変わったら true。
bool DrawSurfaceChoice(EDITOR_PROJECT::IEditorWorkspaceUi& ui, const std::string& id, const std::string& label,
                       const TerrainStore& store, uint32_t& slot);

} // namespace HIKARI::TERRAINKIT
