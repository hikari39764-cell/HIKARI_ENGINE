#pragma once
#include <functional>

namespace HIKARI::RUNTIME {

    class IHostApplication;

    // エンジンホストの動作モード。
    // Game はスタンドアロン実行、Editor はオーサリングツールとして起動する。
    enum class EngineHostProduct {
        Game,
        Editor,
    };

    // プロジェクトファイル（Project.hikari）の要否。
    // Required の場合はプロジェクトの存在と読み込み成功が起動条件となる。
    // None の場合はプロジェクトなしでランチャーモードとして動作する。
    enum class EngineHostProjectPolicy {
        Required,
        None,
    };

    // RunEngineHost に渡す起動オプション。
    // product         : Game / Editor の切り替え。
    // projectPolicy   : プロジェクトファイルの要否。
    // loadRuntimeConfig : RuntimeConfig.json を読み込むかどうか。
    // windowTitle     : メインウィンドウのタイトル文字列。
    struct EngineHostRunOptions {
        EngineHostProduct product = EngineHostProduct::Game;
        EngineHostProjectPolicy projectPolicy =
            EngineHostProjectPolicy::Required;
        bool loadRuntimeConfig = true;
        const char* windowTitle = "HIKARI_Ver1.3";
        bool enableStreamline = true;
        std::function<void()> onFirstApplicationPresented;
    };

    // エンジンホストのメインエントリポイント。
    // IHostApplication を通じてゲーム固有の初期化・更新・終了処理を注入する。
    // 戻り値はプロセスの終了コード（0 が正常終了）。
    int RunEngineHost(
        IHostApplication& application,
        const EngineHostRunOptions& options = {});

} // namespace HIKARI::RUNTIME
