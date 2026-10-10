#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

namespace HIKARI::EDITOR {
struct EditorOperationDefinition;
}

// 智能体はどの模式でも引擎目录に書き込まない（読むのは構わない）。引擎目录は動いている编辑器から決まるので、
// 编辑器を他人に渡せばその人の安装先になる。引擎の源码仓库があることを前提にしない。
namespace HIKARI::EDITOR::AGENT {

// 安装根（编辑器のある Runtime/ の一つ上、启动器と hikari.exe のある所）と、それが引擎の源码仓库の中にあれば仓库根。
std::vector<std::filesystem::path> FindEngineRoots();

// candidate が root の中か。大文字小文字を区別せず、実在する部分は接合点や短い名前を解いて比べる。
// 相対パスは base から解く（base が空なら現在の目录）。
bool IsInsideAgentRoot(const std::filesystem::path& candidate, const std::filesystem::path& root,
                       const std::filesystem::path& base = {});
// 書き込み先が引擎目录の中か、引擎目录を含むか（D:\ 全体への書き込み許可など）。
bool TouchesEngineRoots(const std::vector<std::filesystem::path>& roots, const std::filesystem::path& candidate,
                        const std::filesystem::path& base = {});
// 命令などの文に引擎目录が出てくるか。\ と /、大文字小文字を区別せず、Git Bash（/d/...）と WSL（/mnt/d/...）の書き方も見る。
bool MentionsEngineRoots(const std::vector<std::filesystem::path>& roots, std::string_view text);
// 操作の出力先の引数（schema の format が kOutputPathFormat）が引擎目录に入るなら、その値を返す。
std::optional<std::string> FindEngineOutputParameter(const EditorOperationDefinition& definition,
                                                     const nlohmann::json& parameters,
                                                     const std::vector<std::filesystem::path>& roots);

} // namespace HIKARI::EDITOR::AGENT
