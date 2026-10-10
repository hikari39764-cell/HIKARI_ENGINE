#pragma once

// ランタイム起動設定 ── コマンドライン引数や JSON コンフィグから読み取った
// 実行時パラメータを統一的に扱う構造体とロード関数群。
// Editor / Game / ExportedGame など全ホストモードで共有される。

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "HIKARI_RuntimeHost.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Runtime/Host/HIKARI_HostBootstrapConfig.h"

namespace HIKARI {

// ランタイム起動設定
// コマンドライン由来の値は JSON コンフィグより優先される。
// 各メンバは std::optional で管理され、未設定の項目は
// ApplyRuntimeLaunchConfig 側でフォールバックされる。
struct RuntimeLaunchConfig {
    // 設定が正常に読み込まれたかどうか
    bool loaded = false;

    // 設定ファイルの絶対パス（読み込み元）
    std::filesystem::path sourcePath{};

    // プロジェクトルートディレクトリの絶対パス
    std::filesystem::path projectRoot{};

    // コマンドラインで明示的に指定されたプロジェクトパス（存在する場合のみ）
    std::optional<std::filesystem::path> requestedProjectPath{};

    // ── ホスト動作モード ──
    // ランタイムホストの実行モード（Game / Editor / GamePreview / ExportedGame）
    std::optional<RuntimeHostMode> hostMode{};

    // ── UI・デバッグ関連 ──
    // ImGui デバッグオーバーレイの有効/無効
    std::optional<bool> enableImGui{};
    // エディタ UI（パネル・インスペクタ等）の有効/無効
    std::optional<bool> enableEditorUI{};
    // D3D12 デバッグレイヤーの有効/無効
    std::optional<bool> enableDebugLayer{};
    // GPU pipeline statistics query の有効/無効。未指定なら GfxDebugConfig の既定値を維持する。
    std::optional<bool> enableGpuPipelineStats{};
    // デバッグカメラ（自由飛行カメラ）の有効/無効
    std::optional<bool> enableDebugCamera{};

    // ── ウィンドウ設定 ──
    // メインウィンドウの UTF-8 タイトル
    std::optional<std::string> windowTitle{};
    // ウィンドウのリサイズを許可するか
    std::optional<bool> resizableWindow{};
    // ウィンドウ幅（ピクセル単位）
    std::optional<int> windowWidth{};
    // ウィンドウ高さ（ピクセル単位）
    std::optional<int> windowHeight{};

    // ── 描画品質設定 ──
    // アンチエイリアス、DLSS、フレーム生成などの描画品質設定を集約する
    std::optional<RENDER3D::RenderQualitySettings> renderQuality{};

    // ── シーン起動設定 ──
    // 起動時に読み込むシーンの GUID
    std::optional<std::string> startupSceneGuid{};
    // startupSceneGuid の場面を、資産の文件ではなくこの文件（絶対パス）の内容で開く。编辑器が保存前の場面を
    // 後ろの無頭の引擎で遊ばせる時に使う。
    std::optional<std::string> startupSceneSnapshot{};
    // 出力対象シーンの GUID リスト
    std::optional<std::vector<std::string>> exportedSceneGuids{};
};

// コマンドラインとデフォルト JSON コンフィグから起動設定を読み込む
// 検索順: カレントディレクトリ → EXE 隣接 → プロジェクトルート
RuntimeLaunchConfig LoadRuntimeLaunchConfig();

// コマンドライン引数から指定オプションのパス値を読み取る
// 例: -project "C:\MyProject"  → option=L"project"
std::optional<std::filesystem::path> ReadRuntimeCommandLinePath(std::wstring_view option);

// コマンドラインから uint32 値を厳密に読み取る。
// 無効な文字列、範囲外、値欠落はいずれも nullopt を返す。
std::optional<uint32_t> ReadRuntimeCommandLineUint32(std::wstring_view option);

// 値を伴わない内部起動フラグが指定されているかを確認する。
bool HasRuntimeCommandLineFlag(std::wstring_view option);

// 指定された JSON ファイルから起動設定を読み込む
RuntimeLaunchConfig LoadRuntimeLaunchConfigFromFile(const std::filesystem::path& path);

// RuntimeLaunchConfig の内容を HostBootstrapConfig に反映する
// 未設定の項目は HostBootstrapConfig 側のデフォルト値を維持する
void ApplyRuntimeLaunchConfig(const RuntimeLaunchConfig& runtimeCfg, RUNTIME::HostBootstrapConfig& servicesCfg);

} // namespace HIKARI
