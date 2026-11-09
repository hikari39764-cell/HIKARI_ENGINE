#include <algorithm>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "HIKARI_TerrainKitWorkspacePages.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;

// 数据図の読み方。kTerrainOverlayNames と同じ順。
constexpr const char* kOverlayLegends[] = {
    "",
    "Blue is low, red is high. Dark lines every 10 m.",
    "Green is flat, red is 45 degrees or steeper.",
    "Blue is where water collects.",
    "Each surface has its own color.",
    "Green shows how dense the grass is. Gray has no grass.",
};

// 地形の作業区。左に頁と一覧、中央に場景の視口、右に選んだ物の設定、下に数据図と状態。
class TerrainWorkspace final : public IEditorWorkspace {
public:
    explicit TerrainWorkspace(std::shared_ptr<EditorShared> shared) : shared_(std::move(shared)) {
        // 美術の流れの順：全体を生成 → 形を作る → 地面を塗る → 草 → 物を置く → 道と水。
        pages_.push_back(MakeGeneratePage(shared_));
        pages_.push_back(MakeShapePage(shared_));
        pages_.push_back(MakeSurfacePage(shared_));
        pages_.push_back(MakeGrassPage(shared_));
        pages_.push_back(MakePlacementPage(shared_));
        pages_.push_back(MakeRoadsPage(shared_));
    }

    EditorWorkspaceDescriptor GetDescriptor() const override {
        EditorWorkspaceDescriptor descriptor{};
        descriptor.workspaceId = "terrainkit.workspace";
        descriptor.displayName = "Terrain";
        descriptor.options = static_cast<uint32_t>(EditorWorkspaceOption::RequestSceneViewport);
        return descriptor;
    }

    void OnActivate(EditorWorkspaceHostContext&) override { OpenPage(page_); }

    void OnDeactivate(EditorWorkspaceHostContext&) override {
        if (shared_->services != nullptr) {
            shared_->services->ActivateViewportTool("");
        }
        TerrainStore::Get().SetOverlay(TerrainOverlay::None);
    }

    void Draw(EditorWorkspaceHostContext& context) override {
        IEditorWorkspaceUi& ui = *context.ui;
        TerrainStore& store = TerrainStore::Get();
        if (store.IsOpen() && store.ScenePath() != openedScene_) {
            openedScene_ = store.ScenePath();
            OpenPage(IsFlat(store) ? 0u : (std::max)(page_, size_t{1u}));
        }
        if (ui.BeginPanel("terrainPages", "Terrain", nullptr, EditorWorkspaceDockRegion::Left)) {
            if (store.IsOpen()) {
                for (size_t index = 0u; index < pages_.size(); ++index) {
                    const std::string name = pages_[index]->Name();
                    if (ui.Selectable("page" + name, name, page_ == index) && page_ != index) {
                        OpenPage(index);
                    }
                    ui.Tooltip(pages_[index]->Help());
                }
                ui.Separator();
                pages_[page_]->DrawList(ui, store);
            } else {
                ui.Text("This scene has no terrain. Add a Terrain component to an object in the Scene workspace.");
            }
        }
        ui.EndPanel();
        if (ui.BeginPanel("terrainProperties", "Properties", nullptr, EditorWorkspaceDockRegion::Right)) {
            if (store.IsOpen()) {
                pages_[page_]->DrawProperties(ui, store);
            }
        }
        ui.EndPanel();
        if (ui.BeginPanel("terrainView", "View and Status", nullptr, EditorWorkspaceDockRegion::Bottom)) {
            if (store.IsOpen()) {
                DrawViewAndStatus(ui, store);
            }
        }
        ui.EndPanel();
    }

    bool IsDirty() const override {
        const TerrainStore& store = TerrainStore::Get();
        return store.IsOpen() && store.HasUnsavedTiles();
    }

    EditorWorkspaceSaveResult Save(EditorWorkspaceHostContext&) override {
        TerrainStore& store = TerrainStore::Get();
        std::string error{};
        if (store.IsOpen() && !store.Save(store.ScenePath(), error)) {
            if (shared_->commands != nullptr) {
                shared_->commands->PublishStatus(EditorWorkspaceStatusSeverity::Error, error);
            }
            return EditorWorkspaceSaveResult::Failed;
        }
        return EditorWorkspaceSaveResult::Saved;
    }

    // 地形の編集は場面に属する。作業区を移っても残り、場面の保存でも書かれる。
    EditorWorkspaceCloseDisposition QueryClose(EditorWorkspaceCloseReason) const override {
        return EditorWorkspaceCloseDisposition::Allow;
    }

private:
    void OpenPage(size_t index) {
        page_ = index;
        TerrainStore& store = TerrainStore::Get();
        if (store.IsOpen()) {
            pages_[page_]->OnOpen(store);
        }
        if (shared_->services != nullptr) {
            shared_->services->ActivateViewportTool(pages_[page_]->ToolId());
        }
    }

    // まだ形を作っていない地形は生成の頁から始める。
    static bool IsFlat(const TerrainStore& store) {
        return std::all_of(store.Tiles().begin(), store.Tiles().end(), [](const auto& entry) {
            return entry.second.heights.empty() || entry.second.maxHeight - entry.second.minHeight < 0.01f;
        });
    }

    void DrawViewAndStatus(IEditorWorkspaceUi& ui, TerrainStore& store) {
        ui.Text("Show on terrain");
        size_t overlayIndex = 0u;
        for (size_t index = 0u; index < std::size(kTerrainOverlayNames); ++index) {
            const auto& entry = kTerrainOverlayNames[index];
            ui.SameLine();
            if (ui.Choice(std::string("overlay") + entry.name, entry.name, store.Overlay() == entry.value)) {
                store.SetOverlay(entry.value);
            }
            if (store.Overlay() == entry.value) {
                overlayIndex = index;
            }
        }
        ui.SameLine();
        if (ui.Button("frameTerrain", "Frame Whole Terrain")) {
            FrameTerrainView(*shared_);
        }
        const auto& grid = store.Settings().grid;
        std::string status = std::to_string(store.Tiles().size()) + " tiles of " +
                             std::to_string(static_cast<int>(grid.tileSizeMeters)) + " m";
        if (store.RuleBaker().PendingTiles() != 0u) {
            status += "   Updating " + std::to_string(store.RuleBaker().PendingTiles()) + " tiles";
        }
        if (store.Surfaces().Busy()) {
            status +=
                "   Preparing surfaces " + std::to_string(static_cast<int>(store.Surfaces().Progress() * 100.0f)) + "%";
        }
        if (store.HasUnsavedTiles()) {
            status += "   Unsaved changes";
        }
        if (*kOverlayLegends[overlayIndex] != '\0') {
            status += "   |   ";
            status += kOverlayLegends[overlayIndex];
        }
        ui.Text(status);
    }

    std::shared_ptr<EditorShared> shared_;
    std::vector<std::unique_ptr<TerrainPage>> pages_{};
    size_t page_ = 1u;
    std::filesystem::path openedScene_{};
};

} // namespace

void FrameTerrainView(const EditorShared& shared) {
    const TerrainStore& store = TerrainStore::Get();
    if (shared.services == nullptr || store.Tiles().empty()) {
        return;
    }
    const float tileSize = store.Settings().grid.tileSizeMeters;
    float minimumX = 1.0e9f;
    float minimumZ = 1.0e9f;
    float maximumX = -1.0e9f;
    float maximumZ = -1.0e9f;
    float lowest = 1.0e9f;
    float highest = -1.0e9f;
    for (const auto& [tile, runtime] : store.Tiles()) {
        minimumX = (std::min)(minimumX, static_cast<float>(tile.x) * tileSize);
        minimumZ = (std::min)(minimumZ, static_cast<float>(tile.z) * tileSize);
        maximumX = (std::max)(maximumX, static_cast<float>(tile.x + 1) * tileSize);
        maximumZ = (std::max)(maximumZ, static_cast<float>(tile.z + 1) * tileSize);
        if (!runtime.heights.empty()) {
            lowest = (std::min)(lowest, runtime.minHeight);
            highest = (std::max)(highest, runtime.maxHeight);
        }
    }
    const float centerX = (minimumX + maximumX) * 0.5f;
    const float centerZ = (minimumZ + maximumZ) * 0.5f;
    const float span = (std::max)(maximumX - minimumX, maximumZ - minimumZ);
    const float top = highest >= lowest ? highest : 0.0f;
    const float middle = highest >= lowest ? (lowest + highest) * 0.5f : 0.0f;
    shared.services->SetViewportCamera({centerX, top + span * 0.45f, centerZ - span * 0.7f},
                                       {centerX, middle, centerZ});
}

std::unique_ptr<IEditorWorkspace> MakeTerrainWorkspace(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<TerrainWorkspace>(std::move(shared));
}

} // namespace HIKARI::TERRAINKIT
