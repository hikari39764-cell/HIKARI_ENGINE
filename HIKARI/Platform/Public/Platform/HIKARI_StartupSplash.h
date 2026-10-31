#pragma once

#include <memory>

namespace HIKARI::PLATFORM {

// D3D 初期化より前に表示する、CPU 描画だけの起動画面。
// native window とメッセージ処理は専用スレッドが所有する。
class StartupSplash final {
public:
    StartupSplash();
    ~StartupSplash();
    StartupSplash(const StartupSplash&) = delete;
    StartupSplash& operator=(const StartupSplash&) = delete;

    bool Start();
    void Stop();

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace HIKARI::PLATFORM
