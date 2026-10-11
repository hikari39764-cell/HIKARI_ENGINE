#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <json.hpp>

namespace HIKARI::EDITOR {
class EditorChildProcess;
class EditorOperationRegistry;
struct EditorOperationResult;
// 現在の項目源码の写しを隠れた worker で構築し、编译と動作検証の結果を報告する。原本と読み込み済みの模块は変えない。
class ProjectCodeJob final {
public:
    ProjectCodeJob();
    ~ProjectCodeJob();
    bool Start(std::string& message);
    void Poll();
    void Cancel();
    bool Running() const;
    const nlohmann::json& Status() const { return status_; }
    // 最後に成功した構築の模块（無ければ空）。後台試玩は重载せずにこれで新しい规则を試せる。
    std::filesystem::path BuiltModule() const;
    static std::filesystem::path SdkRoot();

private:
    std::unique_ptr<EditorChildProcess> process_;
    std::filesystem::path job_;
    nlohmann::json status_ = {{"running", false}, {"state", "Idle"}};
    bool cancelled_ = false;
    std::chrono::steady_clock::time_point lastPoll_{};
};

// project.build・project.build.status・project.build.cancel・project.reload と、公開 SDK を読む project.sdk.*。
// reload は编辑器を再起動して新しい模块を読み込む（呼ぶ側が未保存の文書と Play を確かめる）。
void RegisterProjectCodeOperations(EditorOperationRegistry& registry, ProjectCodeJob& job,
                                   std::function<EditorOperationResult()> reload);
} // namespace HIKARI::EDITOR
