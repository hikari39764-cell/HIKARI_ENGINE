#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "HIKARI/Project/EditorGameExport.h"
#include "HIKARI/Project/EditorModuleUi.h"
#include "HIKARI/Project/EditorOperations.h"
#include "HIKARI/Project/EditorWorkspace.h"

namespace HIKARI::EDITOR_PROJECT {

class IGameProjectEditorModule {
public:
    virtual ~IGameProjectEditorModule() = default;

    virtual std::string_view GetModuleId() const = 0;
    virtual std::vector<std::unique_ptr<IEditorWorkspace>> CreateEditorWorkspaces() const = 0;
    // Project-owned handlers must remain alive until the Editor unregisters
    // this module's operations before destroying the module instance.
    virtual void RegisterEditorOperations(IEditorOperationRegistrar&) {}
    // 場景視口の工具と場景の作業区の窓。id はモジュールの中で一意。
    virtual std::vector<std::unique_ptr<IEditorViewportTool>> CreateViewportTools() const { return {}; }
    virtual std::vector<std::unique_ptr<IEditorPanel>> CreateEditorPanels() const { return {}; }
    // ゲームの書き出し。編集器の画面も場面も無い工程から呼ばれるので、項目のファイルだけを読んで決める。
    virtual void CollectGameExportFiles(IGameExportFiles&) {}
};

// Defined by an optional project Editor companion. The SDK adapter owns
// the returned module and every workspace until the host unloads the DLL.
std::unique_ptr<IGameProjectEditorModule> CreateGameProjectEditorModule();

} // namespace HIKARI::EDITOR_PROJECT
