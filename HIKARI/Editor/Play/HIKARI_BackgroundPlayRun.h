#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <json.hpp>

#include "Editor/Agent/HIKARI_HeadlessEngineRun.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

namespace HIKARI {
struct SceneDocument;
}

namespace HIKARI::EDITOR {
class EditorChildProcess;

// 編集中の場面を保存せずに、後ろの無頭の引擎で一回遊ばせる。人の编辑器の視点・選択・Play は使わない。
// 場面と入力轨道は Library/EditorAI/BackgroundPlay/<id>/ へ写し、引擎は本地桥を開かない。一度に一回だけ。
class BackgroundPlayRun {
public:
    struct Request {
        std::filesystem::path engine{};
        std::filesystem::path descriptor{};
        std::filesystem::path libraryRoot{};
        // 编辑器が読み込んでいる项目模块（無ければ模块なしで動かす）。
        std::filesystem::path projectModule{};
        std::string sceneGuid{};
        const SceneDocument* scene = nullptr;
        uint32_t frames = 600u;
        // InputTrack の JSON か、.hinputtrack 文件（どちらも無ければ入力なし）。
        nlohmann::json track{};
        std::filesystem::path trackFile{};
        // 撮るフレーム。2 つ以上は等間隔。
        std::vector<uint32_t> captureFrames{};
        // false なら軽い画質で走らせる（AGENT::HeadlessRuntimeSettings）。
        bool projectQuality = false;
    };

    BackgroundPlayRun();
    ~BackgroundPlayRun();
    BackgroundPlayRun(const BackgroundPlayRun&) = delete;
    BackgroundPlayRun& operator=(const BackgroundPlayRun&) = delete;

    EditorOperationResult Start(const Request& request);
    // 走っている間は pending、終われば截图・終了コード・日志の警告と错误。
    EditorOperationResult Status();

private:
    // 撮れた截图と、その回の日志の警告・错误。
    nlohmann::json Report(uint32_t exitCode) const;

    std::unique_ptr<EditorChildProcess> process_;
    AGENT::HeadlessEngineRun run_{};
    std::filesystem::path folder_{};
    std::filesystem::path logs_{};
    std::set<std::filesystem::path> logsBefore_{};
    std::chrono::steady_clock::time_point started_{};
    std::chrono::seconds timeout_{};
    uint64_t nextId_ = 1u;
    uint64_t runId_ = 0u;
    std::optional<EditorOperationResult> finished_{};
};

} // namespace HIKARI::EDITOR
