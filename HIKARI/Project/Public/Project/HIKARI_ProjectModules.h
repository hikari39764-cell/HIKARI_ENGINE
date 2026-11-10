#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Project/HIKARI_ProjectDescriptor.h"

namespace HIKARI {

inline constexpr char kModuleManifestExtension[] = ".hmodule";
inline constexpr uint32_t kModuleManifestFormatVersion = 1u;
// 項目に置いた挿件包を探すフォルダ：<項目>/Plugins/<id>/<id>.hmodule。
inline constexpr char kProjectPluginsDirectory[] = "Plugins";

// モジュールの資産の種類。JSON のファイルが資産として走査・取り込み・書き出しされる。
struct ModuleManifestAssetType {
    std::string typeId{};
    // 小文字、先頭の点を含む（".hscatter"）。
    std::string extension{};
    std::string displayName{};
};

// 一つのモジュールの清単（<id>.hmodule）。各路径は清単のあるフォルダからの相対。
struct ModuleManifest {
    std::string id{};
    std::string name{};
    std::string version{};
    // DLL を構築した SDK の版（"major.minor.patch"）。構築が書く。major と minor が引擎と同じ時だけ読み込む。
    std::string sdkVersion{};
    // 編集器と開発用の実行。ゲームの書き出しは shippingRuntimeLibrary を配る。
    std::filesystem::path runtimeLibrary{};
    std::filesystem::path shippingRuntimeLibrary{};
    std::filesystem::path editorLibrary{};
    // 書き出したゲームにも配るフォルダ（実行側の DLL が自分の隣から読む物：着色器など）。
    // 編集器だけが使う内容は清単の editorDirectories に書く：挿件包には入るがゲームには入らず、引擎は読まない。
    std::vector<std::filesystem::path> runtimeDirectories{};
    std::vector<std::string> dependencies{};
    // DLL を読まない工程（書き出し、取り込みの命令）も同じ種類を知るよう、清単で宣言する。
    std::vector<ModuleManifestAssetType> assetTypes{};
    // 清単のあるフォルダ（読み込み時に埋める絶対路径）。
    std::filesystem::path directory{};
};

// 項目に入れたモジュールの読み込み結果（項目設定の一覧と本地橋の問い合わせが読む）。
struct InstalledModuleStatus {
    ModuleManifest manifest{};
    bool runtimeLoaded = false;
    bool editorLoaded = false;
    std::string error{};
};

bool LoadModuleManifest(const std::filesystem::path& manifestPath, ModuleManifest& outManifest,
                        std::string* outMessage = nullptr);

bool SaveModuleManifest(const std::filesystem::path& manifestPath, const ModuleManifest& manifest,
                        std::string* outMessage = nullptr);

// モジュールのフォルダ <root>/<id> にある清単：<root>/<id>/<id>.hmodule。
std::filesystem::path MakeModuleManifestPath(const std::filesystem::path& root, const std::string& id);

// 引擎に同梱のモジュールの置き場所：実行ファイルの隣の Modules（書き出したゲームも同じ名前のフォルダから読む）。
inline constexpr char kEngineModulesDirectory[] = "Modules";
std::filesystem::path GetEngineModulesDirectory();

// 清単の sdkVersion が今の引擎の SDK と合うか（major と minor が同じ）。合わなければ、どの版向けの包かを書く。
bool IsModuleSdkCompatible(const ModuleManifest& manifest, std::string* outMessage = nullptr);

// Project.hikari の一覧を清単へ解決し、依存が先に来る順に並べる。路径の無い参照と一覧に無い依存は、
// 項目の Plugins、引擎に同梱のフォルダの順に探す。
// 清単が読めない・id が食い違う・依存が見つからない・循環している時は false と理由。
bool ResolveProjectModules(const std::vector<ProjectModuleReference>& references,
                           const std::filesystem::path& projectRoot, const std::filesystem::path& builtinRoot,
                           std::vector<ModuleManifest>& outModules, std::string* outMessage = nullptr);

enum class ModuleSource : uint8_t {
    ProjectPlugins,
    Engine,
    // Project.hikari が清単の路径を直接書いている。
    ManifestPath,
};

// 項目から選べるモジュール一つ（項目設定の一覧と操作が読む）。
struct ProjectModuleOption {
    // 清単が読めない時は id と directory だけ。
    ModuleManifest manifest{};
    ModuleSource source = ModuleSource::ProjectPlugins;
    // Project.hikari が参照している。
    bool enabled = false;
    // 有効なモジュールのうち、これに依存しているものの id（空でなければ参照が無くても読み込まれ、外せない）。
    std::vector<std::string> requiredBy{};
    // 使えない理由：清単が読めない、id が食い違う、SDK の版が合わない。空なら使える。
    std::string problem{};
};

// Project.hikari が参照するモジュールと、項目の Plugins・引擎に同梱のフォルダにある包を id の昇順で並べる。
std::vector<ProjectModuleOption> ListProjectModuleOptions(const ProjectDescriptor& descriptor,
                                                          const std::filesystem::path& projectRoot,
                                                          const std::filesystem::path& builtinRoot);

// Project.hikari の参照へモジュールを足す・外す（読み込みへの反映は次に項目を開いた時）。
// 知らない id、使えない包、有効なモジュールが依存している包を外す指定は false と理由。既にその状態なら何も書かない。
bool SetProjectModuleEnabled(const std::filesystem::path& projectRoot, const std::filesystem::path& builtinRoot,
                             const std::string& id, bool enabled, std::string* outMessage = nullptr);

} // namespace HIKARI
