#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "HIKARI/Project/GameProjectEditorModule.h"
#include "HIKARI_TerrainKitEditorCommon.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;

class TerrainKitEditorModule final : public IGameProjectEditorModule, public IEditorSceneListener {
public:
    std::string_view GetModuleId() const override { return "HIKARI.TerrainKit"; }
    std::vector<std::unique_ptr<IEditorWorkspace>> CreateEditorWorkspaces() const override {
        std::vector<std::unique_ptr<IEditorWorkspace>> workspaces{};
        workspaces.push_back(MakeTerrainWorkspace(shared_));
        return workspaces;
    }

    std::vector<std::unique_ptr<IEditorViewportTool>> CreateViewportTools() const override {
        std::vector<std::unique_ptr<IEditorViewportTool>> tools{};
        tools.push_back(MakeSculptTool(shared_));
        tools.push_back(MakeMaskTool(shared_));
        tools.push_back(MakeStampTool(shared_));
        tools.push_back(MakePaintTool(shared_));
        tools.push_back(MakeSplineTool(shared_));
        tools.push_back(MakeInstanceTool(shared_));
        return tools;
    }
    void OnSceneSaved(std::string_view scenePathUtf8) override {
        std::string error{};
        const std::filesystem::path path(
            std::u8string_view(reinterpret_cast<const char8_t*>(scenePathUtf8.data()), scenePathUtf8.size()));
        if (TerrainStore::Get().IsOpen() && !TerrainStore::Get().Save(path, error) && shared_->commands != nullptr) {
            shared_->commands->PublishStatus(EditorWorkspaceStatusSeverity::Error, error);
        }
    }

    // 場面を開いた直後に相機が山の中へ埋まっていたら、地形全体が見える所へ出す。
    void OnViewportCamera(const GameProjectFloat3& position) override {
        const TerrainStore& store = TerrainStore::Get();
        if (!store.IsOpen() || store.ScenePath() == checkedScene_) {
            return;
        }
        const std::optional<float> ground = store.HeightAt(position.x, position.z);
        const bool baked = std::any_of(store.Tiles().begin(), store.Tiles().end(),
                                       [](const auto& tile) { return !tile.second.heights.empty(); });
        if (!baked) {
            return;
        }
        checkedScene_ = store.ScenePath();
        if (ground.has_value() && position.y < *ground + 1.0f) {
            FrameTerrainView(*shared_);
        }
    }

    void RegisterEditorOperations(IEditorOperationRegistrar& registrar) override {
        shared_->services = registrar.SceneServices();
        shared_->commands = registrar.Commands();
        if (shared_->services != nullptr) {
            shared_->services->SetSceneListener(this);
        }
        RegisterTerrainOperations(registrar, shared_, handlers_);
    }

private:
    std::shared_ptr<EditorShared> shared_ = std::make_shared<EditorShared>();
    std::vector<std::unique_ptr<IEditorOperationHandler>> handlers_{};
    std::filesystem::path checkedScene_{};
};

} // namespace
} // namespace HIKARI::TERRAINKIT

namespace HIKARI::EDITOR_PROJECT {
std::unique_ptr<IGameProjectEditorModule> CreateGameProjectEditorModule() {
    return std::make_unique<::HIKARI::TERRAINKIT::TerrainKitEditorModule>();
}
} // namespace HIKARI::EDITOR_PROJECT
