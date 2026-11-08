#include <filesystem>
#include <memory>
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
    std::vector<std::unique_ptr<IEditorWorkspace>> CreateEditorWorkspaces() const override { return {}; }

    std::vector<std::unique_ptr<IEditorViewportTool>> CreateViewportTools() const override {
        std::vector<std::unique_ptr<IEditorViewportTool>> tools{};
        tools.push_back(MakeSculptTool(shared_));
        tools.push_back(MakeMaskTool(shared_));
        tools.push_back(MakeStampTool(shared_));
        tools.push_back(MakePaintTool(shared_));
        return tools;
    }
    std::vector<std::unique_ptr<IEditorPanel>> CreateEditorPanels() const override {
        std::vector<std::unique_ptr<IEditorPanel>> panels{};
        panels.push_back(MakeTerrainPanel(shared_));
        return panels;
    }

    void OnSceneSaved(std::string_view scenePathUtf8) override {
        std::string error{};
        const std::filesystem::path path(
            std::u8string_view(reinterpret_cast<const char8_t*>(scenePathUtf8.data()), scenePathUtf8.size()));
        if (TerrainStore::Get().IsOpen() && !TerrainStore::Get().Save(path, error) && shared_->commands != nullptr) {
            shared_->commands->PublishStatus(EditorWorkspaceStatusSeverity::Error, error);
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
};

} // namespace
} // namespace HIKARI::TERRAINKIT

namespace HIKARI::EDITOR_PROJECT {
std::unique_ptr<IGameProjectEditorModule> CreateGameProjectEditorModule() {
    return std::make_unique<::HIKARI::TERRAINKIT::TerrainKitEditorModule>();
}
} // namespace HIKARI::EDITOR_PROJECT
