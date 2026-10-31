#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace HIKARI::PLATFORM {

// 単体ゲームの起動段階。起動の順に進む。
enum class StartupStage {
    Boot,
    Graphics,
    Data,
    Shaders,
};

// 単体ゲームの起動中、ゲームの窓と同じ大きさで出す読み込み画面（HIKARI の標志と進捗）。
// CPU 描画だけで、窓とメッセージ処理は専用スレッドが持つので、本体のスレッドが重い初期化で塞がっていても
// 止まって見えない。進捗は本体が段階を知らせ、各段階の割合は前回の起動で測った時間から決める。
// 最初の描画で黒へ溶けてからゲームの窓に切り替わる。
class LoadingScreen final {
public:
    LoadingScreen();
    ~LoadingScreen();
    LoadingScreen(const LoadingScreen&) = delete;
    LoadingScreen& operator=(const LoadingScreen&) = delete;

    bool Start();
    // すぐ閉じる（ゲームの窓を出した後、または起動の失敗時）。
    void Stop();

    // 中身は実装側だけが知る（下の知らせる関数と窓のスレッドが共有する）。
    struct State;

private:
    std::unique_ptr<State> state_;
};

// 以下は起動中の読み込み画面へ知らせる。読み込み画面が無ければ何もしない。本体のスレッドから呼ぶ。
// ゲームの窓の出し方（画面全体か、窓の内側の大きさ）とタイトル。
void ReportStartupWindowPlan(bool coverMonitor, int clientWidth, int clientHeight, const std::wstring& title);
// 起動時間の記録を置く利用者フォルダー。
void ReportStartupUserRoot(const std::filesystem::path& root);
void ReportStartupStage(StartupStage stage);
// 最初の描画の直前に呼ぶ。読み込み画面が黒へ溶け終えるまで（最長 timeoutMs）待つ。
void WaitForStartupReveal(unsigned timeoutMs);

} // namespace HIKARI::PLATFORM
