#include "HIKARI_RuntimeLaunchConfig.h"

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cwchar>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Core/HIKARI_JsonRead.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Platform/Paths/HIKARI_ExecutablePath.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Render3D/Settings/HIKARI_RenderQualityProfileStore.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"

namespace HIKARI {

namespace {
// JSON コンフィグファイル内の"runtime"ノードを抽出する補助関数群

// "runtime"キーを持つノードを取得、見つからなければ root 自体を返す
const nlohmann::json& RuntimeNode(const nlohmann::json& root) {
    if (root.contains("runtime") && root["runtime"].is_object()) {
        return root["runtime"];
    }
    return root;
}

// JSON ノードから指定キーの型付き値を安全に読み取る
std::optional<bool> ReadBool(const nlohmann::json& node, const char* key) {
    if (!node.contains(key) || !node[key].is_boolean()) {
        return std::nullopt;
    }
    return node[key].get<bool>();
}

std::optional<int> ReadInt(const nlohmann::json& node, const char* key) {
    if (!node.contains(key) || !node[key].is_number_integer()) {
        return std::nullopt;
    }
    return node[key].get<int>();
}

std::optional<std::string> ReadString(const nlohmann::json& node, const char* key) {
    if (!node.contains(key) || !node[key].is_string()) {
        return std::nullopt;
    }
    return node[key].get<std::string>();
}

std::optional<std::vector<std::string>> ReadStringArray(const nlohmann::json& node, const char* key) {
    if (!node.contains(key) || !node[key].is_array()) {
        return std::nullopt;
    }

    std::vector<std::string> values{};
    for (const nlohmann::json& entry : node[key]) {
        if (!entry.is_string()) {
            continue;
        }
        std::string value = entry.get<std::string>();
        if (!value.empty()) {
            values.push_back(std::move(value));
        }
    }
    return values;
}

// コンフィグファイルの検索候補パスを生成
void AddCandidate(std::vector<std::filesystem::path>& candidates, const std::filesystem::path& root) {
    if (root.empty()) {
        return;
    }
    candidates.push_back(root / "HIKARI" / "runtime_config.json");
    candidates.push_back(root / "runtime_config.json");
}

// コンフィグファイルの位置からプロジェクトルートディレクトリを決定
std::filesystem::path ResolveConfigProjectRoot(const std::filesystem::path& path) {
    std::error_code ec{};
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if (ec || normalized.empty()) {
        ec.clear();
        normalized = std::filesystem::absolute(path, ec);
    }
    if (ec || normalized.empty()) {
        normalized = path;
    }

    std::filesystem::path configDirectory = normalized.lexically_normal().parent_path();
    if (configDirectory.filename() == "HIKARI") {
        configDirectory = configDirectory.parent_path();
    }
    return configDirectory.lexically_normal();
}

// コンフィグで明示的に指定されたプロジェクトルートを解決（相対パス対応）
std::filesystem::path ResolveConfiguredProjectRoot(const std::filesystem::path& configPath,
                                                   const std::string& configuredRoot) {
    std::filesystem::path root = configuredRoot;
    if (root.is_relative()) {
        root = configPath.parent_path() / root;
    }

    std::error_code ec{};
    std::filesystem::path normalized = std::filesystem::weakly_canonical(root, ec);
    if (ec || normalized.empty()) {
        ec.clear();
        normalized = std::filesystem::absolute(root, ec);
    }
    return ec || normalized.empty() ? root.lexically_normal() : normalized.lexically_normal();
}

std::vector<std::wstring> ReadCommandLineArguments() {
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr) {
        return {};
    }

    std::vector<std::wstring> values{};
    values.reserve(static_cast<size_t>(argumentCount));
    for (int index = 0; index < argumentCount; ++index) {
        values.emplace_back(arguments[index]);
    }
    LocalFree(arguments);
    return values;
}

std::optional<std::filesystem::path> ExplicitConfigPath() {
    std::array<wchar_t, 32768> buffer{};
    const DWORD length =
        GetEnvironmentVariableW(L"HIKARI_RUNTIME_CONFIG", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return std::nullopt;
    }
    return std::filesystem::path(buffer.data());
}

} // namespace

// コマンドライン引数から指定されたオプション値を抽出
std::optional<std::filesystem::path> ReadRuntimeCommandLinePath(std::wstring_view option) {
    const std::vector<std::wstring> arguments = ReadCommandLineArguments();
    for (size_t index = 1u; index + 1u < arguments.size(); ++index) {
        if (arguments[index] == option) {
            return std::filesystem::path(arguments[index + 1u]);
        }
    }
    return std::nullopt;
}

std::optional<uint32_t> ReadRuntimeCommandLineUint32(std::wstring_view option) {
    const std::vector<std::wstring> arguments = ReadCommandLineArguments();
    for (size_t index = 1u; index + 1u < arguments.size(); ++index) {
        if (arguments[index] != option) {
            continue;
        }

        const std::wstring& valueText = arguments[index + 1u];
        if (valueText.empty() || valueText.front() == L'-') {
            return std::nullopt;
        }
        errno = 0;
        wchar_t* end = nullptr;
        const unsigned long long value = std::wcstoull(valueText.c_str(), &end, 10);
        if (errno == ERANGE || end == valueText.c_str() || end == nullptr || *end != L'\0' ||
            value > (std::numeric_limits<uint32_t>::max)()) {
            return std::nullopt;
        }
        return static_cast<uint32_t>(value);
    }
    return std::nullopt;
}

bool HasRuntimeCommandLineFlag(std::wstring_view option) {
    const std::vector<std::wstring> arguments = ReadCommandLineArguments();
    for (size_t index = 1u; index < arguments.size(); ++index) {
        if (arguments[index] == option) {
            return true;
        }
    }
    return false;
}

// JSON ファイルからランタイムコンフィグを読み込む
RuntimeLaunchConfig LoadRuntimeLaunchConfigFromFile(const std::filesystem::path& path) {
    RuntimeLaunchConfig cfg{};
    nlohmann::json root{};
    if (!SERIALIZATION::JSON::ReadJsonFile(path, root) || !root.is_object()) {
        return cfg;
    }

    const nlohmann::json& runtime = RuntimeNode(root);
    if (!runtime.is_object()) {
        return cfg;
    }

    // ホストモード、UI フラグ、デバッグ設定を読み込む
    if (runtime.contains("hostMode") && runtime["hostMode"].is_string()) {
        RuntimeHostMode mode{};
        if (TEXT::TryParseEnum(kRuntimeHostModeNames, runtime["hostMode"].get<std::string>(), mode)) {
            cfg.hostMode = mode;
        }
    }

    // UI・デバッグ・ウィンドウ設定を読み込む
    cfg.enableImGui = ReadBool(runtime, "enableImGui");
    cfg.enableEditorUI = ReadBool(runtime, "enableEditorUI");
    cfg.enableDebugLayer = ReadBool(runtime, "enableDebugLayer");
    cfg.enableDebugCamera = ReadBool(runtime, "enableDebugCamera");
    // gfxDebug から起動時に取るのはこの 1 項目だけ（調試層は上の enableDebugLayer）。
    if (const nlohmann::json* gfxDebug = JSONREAD::FindMember(runtime, "gfxDebug");
        gfxDebug != nullptr && gfxDebug->is_object()) {
        cfg.enableGpuPipelineStats = ReadBool(*gfxDebug, "enableGpuPipelineStats");
    }
    cfg.windowTitle = ReadString(runtime, "windowTitle");
    cfg.resizableWindow = ReadBool(runtime, "resizableWindow");
    cfg.allowTraditionalGeometryFallback = ReadBool(runtime, "allowTraditionalGeometryFallback");
    cfg.windowWidth = ReadInt(runtime, "windowWidth");
    cfg.windowHeight = ReadInt(runtime, "windowHeight");
    // レンダリング品質プロファイルと関連設定を読み込む
    if (runtime.contains("renderQuality") && runtime["renderQuality"].is_object()) {
        RENDER3D::RenderQualitySettings renderQuality{};
        std::string renderQualityError{};
        if (RENDER3D::DeserializeRenderQualitySettings(runtime["renderQuality"], renderQuality, &renderQualityError)) {
            cfg.renderQuality = renderQuality;
        } else {
            HIKARI_LOG_WARN("[RuntimeConfig] " + renderQualityError + " Path: " + path.string());
        }
    }
    // シーン情報
    cfg.startupSceneGuid = ReadString(runtime, "startupSceneGuid");
#if defined(HIKARI_WITH_EDITOR)
    // 编辑器の後台 Play が保存前の場面を渡す。発行版のゲームは任意の文件から開始場面を読まない。
    cfg.startupSceneSnapshot = ReadString(runtime, "startupSceneSnapshot");
#else
    if (runtime.contains("startupSceneSnapshot")) {
        HIKARI_LOG_WARN("[RuntimeConfig] startupSceneSnapshot is read only by editor builds; ignored. Path: " +
                        path.string());
    }
#endif
    cfg.exportedSceneGuids = ReadStringArray(runtime, "exportedSceneGuids");
    cfg.loaded = true;
    cfg.sourcePath = path;
    cfg.projectRoot = ResolveConfigProjectRoot(path);
    // 明示的に指定されたプロジェクトルートで上書き
    if (const std::optional<std::string> projectRoot = ReadString(runtime, "projectRoot")) {
        cfg.projectRoot = ResolveConfiguredProjectRoot(path, *projectRoot);
    }
    return cfg;
}

// ランタイムコンフィグを探索して読み込む（優先順序: コマンドライン > 環境変数 > 候補パス）
RuntimeLaunchConfig LoadRuntimeLaunchConfig() {
    const std::optional<std::filesystem::path> requestedProjectPath = ReadRuntimeCommandLinePath(L"--project");
    const auto finalize = [&requestedProjectPath](RuntimeLaunchConfig cfg) {
        cfg.requestedProjectPath = requestedProjectPath;
        return cfg;
    };

    // 1. コマンドライン引数 --runtime-config で明示的に指定されたパス
    if (const std::optional<std::filesystem::path> commandLinePath = ReadRuntimeCommandLinePath(L"--runtime-config")) {
        RuntimeLaunchConfig cfg = LoadRuntimeLaunchConfigFromFile(*commandLinePath);
        if (cfg.loaded) {
            return finalize(std::move(cfg));
        }
    }
    // 2. 環境変数 HIKARI_RUNTIME_CONFIG で指定されたパス
    if (const std::optional<std::filesystem::path> explicitPath = ExplicitConfigPath()) {
        RuntimeLaunchConfig cfg = LoadRuntimeLaunchConfigFromFile(*explicitPath);
        if (cfg.loaded) {
            return finalize(std::move(cfg));
        }
    }

    // 3. 標準的な検索パス（カレントディレクトリ、実行ファイルディレクトリ）を走査
    std::vector<std::filesystem::path> candidates{};
    AddCandidate(candidates, std::filesystem::current_path());
    AddCandidate(candidates, PLATFORM::GetExecutableDirectory());

    std::error_code ec{};
    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::exists(candidate, ec) && !ec) {
            RuntimeLaunchConfig cfg = LoadRuntimeLaunchConfigFromFile(candidate);
            if (cfg.loaded) {
                return finalize(std::move(cfg));
            }
        }
        ec.clear();
    }
    return finalize({});
}

// ランタイムコンフィグをホストサービス初期化設定に反映させる
void ApplyRuntimeLaunchConfig(const RuntimeLaunchConfig& runtimeCfg, RUNTIME::HostBootstrapConfig& servicesCfg) {
    // ランタイムコンフィグが設定した値をホストブートストラップ設定に適用
    if (runtimeCfg.hostMode) {
        servicesCfg.hostMode = *runtimeCfg.hostMode;
    }
    if (runtimeCfg.enableImGui) {
        servicesCfg.enableImGui = *runtimeCfg.enableImGui;
    }
    if (runtimeCfg.enableEditorUI) {
        servicesCfg.enableEditorUI = *runtimeCfg.enableEditorUI;
    }
    if (runtimeCfg.enableDebugLayer) {
        servicesCfg.enableDebugLayer = *runtimeCfg.enableDebugLayer;
    }
    if (runtimeCfg.enableGpuPipelineStats) {
        GFX::GfxDebugConfig debugConfig = GFX::GetGfxDebugConfig();
        debugConfig.enableGpuPipelineStats = *runtimeCfg.enableGpuPipelineStats;
        GFX::SetGfxDebugConfig(debugConfig);
    }
    if (runtimeCfg.enableDebugCamera) {
        servicesCfg.enableDebugCamera = *runtimeCfg.enableDebugCamera;
    }
    if (runtimeCfg.startupSceneGuid) {
        servicesCfg.startupSceneGuid = *runtimeCfg.startupSceneGuid;
    }
    if (runtimeCfg.startupSceneSnapshot) {
        servicesCfg.startupSceneSnapshot = *runtimeCfg.startupSceneSnapshot;
    }
    if (runtimeCfg.exportedSceneGuids) {
        servicesCfg.exportedSceneGuids = *runtimeCfg.exportedSceneGuids;
    }

    // レンダリング品質設定を適用（プロジェクト設定またはランタイムコンフィグから）
    RENDER3D::RenderQualitySettings quality{};
    const ProjectContext& projectContext = GetProjectContext();
    const std::filesystem::path projectRoot =
        projectContext.IsOpen() ? projectContext.GetRoot() : runtimeCfg.projectRoot;
    std::string profileError{};
    const bool profileLoaded =
        !projectRoot.empty() && RENDER3D::LoadRenderQualityProfile(projectRoot, quality, &profileError);
    if (!profileLoaded && !profileError.empty()) {
        HIKARI_LOG_WARN("[RenderQualityProfile] " + profileError);
    }

    // ランタイムコンフィグの設定で上書き
    if (runtimeCfg.renderQuality) {
        quality = *runtimeCfg.renderQuality;
    }
    RENDER3D::SetRenderQualitySettings(quality);

    // エディタモード以外では、決定されたウィンドウサイズを適用
    if (!IsEditorHostMode(servicesCfg.hostMode)) {
        const RENDER3D::RenderQualitySettings& resolvedQuality = RENDER3D::GetRenderQualitySettings();
        const RENDER3D::RenderResolution windowResolution =
            RENDER3D::ResolveFixedRenderResolution(resolvedQuality.windowSize);
        servicesCfg.resizableWindow = resolvedQuality.windowMode == RENDER3D::WindowPresentationMode::Windowed;
        if (windowResolution.width > 0) {
            servicesCfg.windowWidth = windowResolution.width;
        }
        if (windowResolution.height > 0) {
            servicesCfg.windowHeight = windowResolution.height;
        }
    }

    if (runtimeCfg.allowTraditionalGeometryFallback) {
        servicesCfg.allowTraditionalGeometryFallback = *runtimeCfg.allowTraditionalGeometryFallback;
    }
    // ウィンドウ設定の最終上書き（ユーザー設定を優先）
    if (runtimeCfg.resizableWindow) {
        servicesCfg.resizableWindow = *runtimeCfg.resizableWindow;
    }
    if (runtimeCfg.windowWidth && *runtimeCfg.windowWidth > 0) {
        servicesCfg.windowWidth = *runtimeCfg.windowWidth;
    }
    if (runtimeCfg.windowHeight && *runtimeCfg.windowHeight > 0) {
        servicesCfg.windowHeight = *runtimeCfg.windowHeight;
    }
}

} // namespace HIKARI
