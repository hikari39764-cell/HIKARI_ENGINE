#pragma once

#include <cstddef>
#include <string_view>

namespace HIKARI::EDITOR_PROJECT {

// ゲームの書き出しの時に、モジュールが包へ入れるファイルを申し出る窓口。路径は UTF-8。
// 引擎が自分で配るのは、場面とその .cells フォルダ・GUID で参照された資産・清単の runtimeDirectories。
// それ以外に実行側が読む物（焼いた派生データ、路径で参照する項目のファイル）をここで足す。
class IGameExportFiles {
public:
    // 項目の根（絶対路径）。
    virtual std::string_view GetProjectRoot() const = 0;
    // 書き出す場面。路径は項目の根からの相対（"Assets/Scenes/Main.scene.json"）。
    virtual size_t GetSceneCount() const = 0;
    virtual std::string_view GetScenePath(size_t index) const = 0;
    // 自分のモジュールのフォルダへ足す。ゲームでは実行側の DLL の隣の <relativePath> で読める。
    // sourcePath は絶対路径（項目の Library に焼いた物など）。
    virtual bool AddModuleFile(std::string_view sourcePath, std::string_view relativePath) = 0;
    // 項目の Assets の下のファイルを、場面と同じ相対位置のまま足す（場面の路径から辿る根はゲームでも同じ形になる）。
    virtual bool AddProjectFile(std::string_view projectRelativePath) = 0;
    // 書き出しを止める。理由は書き出しの結果にそのまま出る。
    virtual void Fail(std::string_view message) = 0;

protected:
    ~IGameExportFiles() = default;
};

} // namespace HIKARI::EDITOR_PROJECT
