#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace HIKARI::EDITOR {

// 書き出しの最初に確かめる：項目のモジュールが全部見つかり、SDK の版が合い、Shipping の DLL が揃っているか。
// Shipping の構築や資産の焼きを待たせる前に、足りない物を理由つきで知らせる。挿件包は編集器と同じ場所
// （項目の Plugins、編集器の隣の Modules）から探す。
bool ValidateProjectModulesForExport(const std::filesystem::path& projectRoot, std::string& outMessage);

// 項目に入れたモジュールを書き出しの作業フォルダへ配る（同じ確認を通ったものだけ）。モジュールごとに：
//   Modules/<id>/ へ Shipping の DLL・清単・清単の runtimeDirectories を置き、
//   編集器側の DLL があれば CollectGameExportFiles を呼んで、申し出たファイルを足す。
// 最後に、焼いた Project.hikari の参照を id だけにする（ゲームは実行ファイルの隣の Modules から読む）。
// scenePaths は書き出す場面（項目の根からの相対）。焼いた内容（stagingDirectory/Content）ができた後に呼ぶ。
bool PackageProjectModules(const std::filesystem::path& projectRoot, const std::filesystem::path& stagingDirectory,
                           const std::vector<std::filesystem::path>& scenePaths, std::string& outMessage);

} // namespace HIKARI::EDITOR
