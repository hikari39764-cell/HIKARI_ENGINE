#pragma once
#include "Editor/Export/HIKARI_GameExporter.h"
#include <memory>
#include <json.hpp>

namespace HIKARI::EDITOR {
class EditorOperationRegistry;
class GameExportJob final {
public:
    GameExportJob();
    ~GameExportJob();
    bool Start(const GameExportOptions& options, std::string& message);
    void Poll();
    void Cancel();
    bool IsRunning() const;
    const nlohmann::json& Status() const;
    // 書き出し要求を Library/Build/Exports/<時刻>/request.json に保存する。編集器とコマンドラインの共通入口で、
    // 出力先が空なら既定の出力先を options へ書き戻す。失敗時は空のパスを返す。
    static std::filesystem::path WriteRequest(GameExportOptions& options, std::string& message);
    // 要求を実行する（Shipping 実行環境の構築、識別情報の検証、書き出し）。
    static int RunRequest(const std::filesystem::path& requestPath);

private:
    struct State;
    std::unique_ptr<State> state_;
};
void RegisterGameExportOperations(EditorOperationRegistry& registry, GameExportJob& job,
                                  std::function<bool()> canStart);
} // namespace HIKARI::EDITOR
