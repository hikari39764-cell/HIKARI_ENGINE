#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Project/HIKARI_ProjectDescriptor.h"

namespace HIKARI {

inline constexpr char kModuleManifestExtension[] = ".hmodule";
inline constexpr uint32_t kModuleManifestFormatVersion = 1u;

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
    // 編集器と開発用の実行。ゲームの書き出しは shippingRuntimeLibrary を配る。
    std::filesystem::path runtimeLibrary{};
    std::filesystem::path shippingRuntimeLibrary{};
    std::filesystem::path editorLibrary{};
    std::filesystem::path contentDirectory{};
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

// 引擎に同梱のモジュールの清単：<builtinRoot>/<id>/<id>.hmodule。
std::filesystem::path MakeBuiltinModuleManifestPath(const std::filesystem::path& builtinRoot, const std::string& id);

// Project.hikari の一覧を清単へ解決し、依存が先に来る順に並べる。一覧に無い依存は同梱のモジュールから補う。
// 清単が読めない・id が食い違う・依存が見つからない・循環している時は false と理由。
bool ResolveProjectModules(const std::vector<ProjectModuleReference>& references,
                           const std::filesystem::path& projectRoot, const std::filesystem::path& builtinRoot,
                           std::vector<ModuleManifest>& outModules, std::string* outMessage = nullptr);

} // namespace HIKARI
