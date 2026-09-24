#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

namespace HIKARI {
class IScene;
}

namespace HIKARI::RUNTIME {

// --benchmark-output 指定時のみ生成し、既存の計測と保存トランザクションを接続する。
class RuntimeBenchmark final {
public:
    static std::unique_ptr<RuntimeBenchmark> FromCommandLine(std::chrono::steady_clock::time_point startupStarted);
    ~RuntimeBenchmark();
    void OnPresentedFrame(uint64_t frameId, bool applicationFrame, const IScene* scene);
    void Finish();
    bool ShouldExit() const noexcept;
    int ExitCode() const noexcept;

private:
    struct State;
    explicit RuntimeBenchmark(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};

} // namespace HIKARI::RUNTIME
