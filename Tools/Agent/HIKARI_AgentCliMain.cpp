// hikari.exe：編集器の本地桥を命令行と MCP から呼ぶ。Claude Code・Codex などの智能体が使う入口。
#include "HIKARI_AgentClient.h"
#include "HIKARI_AgentHeadless.h"
#include "HIKARI_AgentMcpServer.h"
#include "HIKARI_AgentTools.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

using namespace HIKARI::AGENT_CLI;

constexpr int kExitOk = 0;
constexpr int kExitOperationFailed = 1;
constexpr int kExitUsage = 2;
constexpr int kExitConnection = 3;

constexpr const char* kUsage =
    "用法：hikari <命令> [选项]\n"
    "\n"
    "命令：\n"
    "  state                        编辑器现场（项目、场景、选中、相机、Play、待审批）\n"
    "  search <关键词...>            按用途查找编辑器操作\n"
    "  describe <操作>               查看一个操作的参数\n"
    "  run <操作> [JSON|-]           执行一个操作；参数是 JSON 对象，- 表示从标准输入读取\n"
    "  batch <文件|->                一次执行一组操作（JSON 数组，或 {\"steps\": [...]}）\n"
    "  capture [--source 来源] [--out 文件.png]   截取编辑器显示的画面（来源：auto、game、space、preview）\n"
    "  play start|stop|status|pause|resume\n"
    "  play step <ticks>            按固定步长精确前进\n"
    "  play input <轨道文件|-|stop>  回放或停止输入轨道（InputTrack JSON）\n"
    "  play run <ticks> [--track 文件] [--capture 30,60] [--no-pause]\n"
    "                               一次完成：需要时开始 Play、回放输入、前进、截图，返回截图与警告\n"
    "  play background <ticks> [--track 文件] [--capture 120,240] [--track-objects 11,21] [--quality project]\n"
    "                               后台无头运行当前场景（不用保存，不占用编辑器），结果同 play run，\n"
    "                               --track-objects 每 30 tick 记录这些物体的位置\n"
    "  logs [--since N] [--level info|warning|error] [--contains 文本] [--limit N]\n"
    "  headless <ticks> [--track 文件] [--capture 120,240] [--track-objects 11,21] [--quality project]\n"
    "           [--allow-missing-module]\n"
    "                               编辑器没开时，用保存的开始场景无头运行一次（同 play background）\n"
    "  mcp                          作为 MCP 服务器运行（标准输入输出）\n"
    "\n"
    "选项：\n"
    "  --project <目录>   项目目录；默认从当前目录向上查找 Project.hikari\n"
    "  --timeout <秒>     单个请求最长等待时间，默认 600（包括等待用户审批）\n"
    "  --pretty           缩进输出 JSON\n"
    "\n"
    "退出码：0 成功，1 操作失败，2 用法错误，3 无法连接编辑器。\n";

// 値を取る選択肢と、値を取らない旗。
const std::set<std::string> kValueOptions = {"--project",  "--timeout", "--source",        "--out",
                                             "--track",    "--capture", "--since",         "--level",
                                             "--contains", "--limit",   "--track-objects", "--quality"};
const std::set<std::string> kFlags = {"--pretty", "--no-pause", "--allow-missing-module"};

std::string ToUtf8(const wchar_t* text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string result(size > 0 ? static_cast<size_t>(size - 1) : 0u, '\0');
    if (size > 1)
        WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::filesystem::path FromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string AbsoluteUtf8(const std::string& path) {
    // 編集器は自分の作業フォルダーで相対パスを解くので、送る前に絶対パスにする。
    const std::u8string absolute = std::filesystem::absolute(FromUtf8(path)).u8string();
    return {reinterpret_cast<const char*>(absolute.data()), absolute.size()};
}

bool ReadJsonArgument(const std::string& text, nlohmann::json& outValue, std::string& outError) {
    std::string source = text;
    if (text == "-") {
        source.assign(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    }
    outValue = nlohmann::json::parse(source, nullptr, false);
    if (outValue.is_discarded()) {
        outError = "JSON 解析失败：" + text;
        return false;
    }
    return true;
}

bool ReadJsonFile(const std::string& path, nlohmann::json& outValue, std::string& outError) {
    if (path == "-")
        return ReadJsonArgument(path, outValue, outError);
    std::ifstream input(FromUtf8(path), std::ios::binary);
    if (!input) {
        outError = "无法读取文件：" + path;
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return ReadJsonArgument(text, outValue, outError);
}

std::optional<uint64_t> ParseCount(const std::string& text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        return std::nullopt;
    return std::strtoull(text.c_str(), nullptr, 10);
}

void PrintUtf8(const std::string& text, FILE* stream) {
    std::fwrite(text.data(), 1u, text.size(), stream);
    std::fputc('\n', stream);
}

struct CommandLine {
    std::vector<std::string> positional{};
    std::map<std::string, std::string> values{};
    std::set<std::string> flags{};

    std::string Value(const std::string& key) const {
        const auto found = values.find(key);
        return found != values.end() ? found->second : std::string{};
    }
};

// 逗号で区切った数の列（--capture、--track-objects）。
bool ParseCountList(const std::string& text, const char* option, nlohmann::json& outList, std::string& outError) {
    outList = nlohmann::json::array();
    std::stringstream list(text);
    for (std::string item; std::getline(list, item, ',');) {
        const std::optional<uint64_t> count = ParseCount(item);
        if (!count) {
            outError = std::string(option) + " 需要用逗号分隔的数";
            return false;
        }
        outList.push_back(*count);
    }
    return true;
}

// play run / play background / headless の共通の引数（ticks は位置 index の単語）。
bool BuildRunArguments(const CommandLine& line, size_t index, nlohmann::json& outArguments, std::string& outError) {
    const std::vector<std::string>& words = line.positional;
    const std::optional<uint64_t> ticks = words.size() > index ? ParseCount(words[index]) : std::nullopt;
    if (!ticks) {
        outError = "需要 tick 数";
        return false;
    }
    outArguments["ticks"] = *ticks;
    if (!line.Value("--track").empty() && !ReadJsonFile(line.Value("--track"), outArguments["track"], outError))
        return false;
    if (!line.Value("--capture").empty() &&
        !ParseCountList(line.Value("--capture"), "--capture", outArguments["captureAt"], outError))
        return false;
    if (!line.Value("--track-objects").empty() &&
        !ParseCountList(line.Value("--track-objects"), "--track-objects", outArguments["trackObjects"], outError))
        return false;
    return true;
}

// play の命令を道具の引数にする。失敗したら理由。
bool BuildPlayArguments(const CommandLine& line, nlohmann::json& outArguments, std::string& outError) {
    const std::vector<std::string>& words = line.positional;
    if (words.size() < 2u) {
        outError = "play 需要子命令：background、start、stop、status、pause、resume、step、input、run";
        return false;
    }
    const std::string& action = words[1];
    outArguments = {{"action", action}};
    if (action == "step") {
        const std::optional<uint64_t> count = words.size() >= 3u ? ParseCount(words[2]) : std::nullopt;
        if (!count) {
            outError = "play step 需要 tick 数";
            return false;
        }
        outArguments["ticks"] = *count;
    }
    if (action == "input") {
        if (words.size() != 3u) {
            outError = "play input 需要轨道文件、- 或 stop";
            return false;
        }
        if (words[2] == "stop") {
            outArguments["stop"] = true;
        } else if (!ReadJsonFile(words[2], outArguments["track"], outError)) {
            return false;
        }
    }
    if (action == "run" || action == "background") {
        if (!BuildRunArguments(line, 2u, outArguments, outError))
            return false;
        if (action == "run" && line.flags.contains("--no-pause"))
            outArguments["pauseAfter"] = false;
        if (action == "background" && !line.Value("--quality").empty())
            outArguments["quality"] = line.Value("--quality");
    }
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    CommandLine line;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = ToUtf8(argv[index]);
        if (argument == "--help" || argument == "-h") {
            PrintUtf8(kUsage, stdout);
            return kExitOk;
        }
        if (kValueOptions.contains(argument) && index + 1 < argc) {
            line.values[argument] = ToUtf8(argv[++index]);
        } else if (kFlags.contains(argument)) {
            line.flags.insert(argument);
        } else {
            line.positional.push_back(argument);
        }
    }
    if (line.positional.empty()) {
        PrintUtf8(kUsage, stderr);
        return kExitUsage;
    }
    const bool pretty = line.flags.contains("--pretty");
    const auto print = [pretty](const nlohmann::json& result) {
        PrintUtf8(result.dump(pretty ? 2 : -1, ' ', false, nlohmann::json::error_handler_t::replace), stdout);
        return result.value("ok", false) ? kExitOk : kExitOperationFailed;
    };

    std::filesystem::path project = FromUtf8(line.Value("--project"));
    if (project.empty()) {
        const std::optional<std::filesystem::path> found = FindProjectRoot(std::filesystem::current_path());
        if (!found) {
            PrintUtf8("找不到项目：请在项目目录里运行，或使用 --project <目录>。", stderr);
            return kExitUsage;
        }
        project = *found;
    }
    const std::string& command = line.positional[0];
    if (command == "headless") {
        HeadlessRunOptions headless{};
        headless.projectRoot = project;
        headless.allowMissingModule = line.flags.contains("--allow-missing-module");
        headless.projectQuality = line.Value("--quality") == "project";
        std::string error;
        if (!BuildRunArguments(line, 1u, headless.run, error)) {
            PrintUtf8("headless " + error, stderr);
            return kExitUsage;
        }
        return print(RunHeadless(headless));
    }

    AgentClientOptions options{};
    options.projectRoot = project;
    if (const std::optional<uint64_t> timeout = ParseCount(line.Value("--timeout")))
        options.requestTimeout = std::chrono::seconds((std::max)(*timeout, uint64_t{1}));
    if (command == "mcp") {
        // MCP は智能体のセッションの間ずっと動くので、編集器の再起動を長めに待つ。
        options.reconnectWait = std::chrono::seconds(60);
        AgentClient mcpClient(options);
        return RunMcpServer(mcpClient);
    }
    AgentClient client(options);

    const std::vector<std::string>& words = line.positional;
    nlohmann::json arguments = nlohmann::json::object();
    std::string toolName;
    std::string error;
    if (command == "state") {
        toolName = "hikari_state";
    } else if (command == "search" && words.size() >= 2u) {
        std::string keyword;
        for (size_t index = 1u; index < words.size(); ++index)
            keyword += (index > 1u ? " " : "") + words[index];
        toolName = "hikari_search";
        arguments["keyword"] = keyword;
    } else if (command == "describe" && words.size() == 2u) {
        toolName = "hikari_describe";
        arguments["operation"] = words[1];
    } else if (command == "run" && (words.size() == 2u || words.size() == 3u)) {
        toolName = "hikari_run";
        arguments["operation"] = words[1];
        if (words.size() == 3u && !ReadJsonArgument(words[2], arguments["parameters"], error)) {
            PrintUtf8(error, stderr);
            return kExitUsage;
        }
    } else if (command == "batch" && words.size() == 2u) {
        toolName = "hikari_batch";
        nlohmann::json steps;
        if (!ReadJsonFile(words[1], steps, error)) {
            PrintUtf8(error, stderr);
            return kExitUsage;
        }
        arguments = steps.is_array() ? nlohmann::json{{"steps", steps}} : steps;
    } else if (command == "capture" && words.size() == 1u) {
        toolName = "hikari_capture";
        if (!line.Value("--source").empty())
            arguments["source"] = line.Value("--source");
        if (!line.Value("--out").empty())
            arguments["path"] = AbsoluteUtf8(line.Value("--out"));
    } else if (command == "play") {
        toolName = "hikari_play";
        if (!BuildPlayArguments(line, arguments, error)) {
            PrintUtf8(error, stderr);
            return kExitUsage;
        }
    } else if (command == "logs" && words.size() == 1u) {
        toolName = "hikari_logs";
        if (const std::optional<uint64_t> since = ParseCount(line.Value("--since")))
            arguments["since"] = *since;
        if (const std::optional<uint64_t> limit = ParseCount(line.Value("--limit")))
            arguments["limit"] = *limit;
        if (!line.Value("--level").empty())
            arguments["minLevel"] = line.Value("--level");
        if (!line.Value("--contains").empty())
            arguments["contains"] = line.Value("--contains");
    } else {
        PrintUtf8(kUsage, stderr);
        return kExitUsage;
    }

    const AgentTool* tool = FindAgentTool(toolName);
    if (const std::optional<std::string> invalid = NormalizeAgentToolArguments(*tool, arguments)) {
        PrintUtf8(*invalid, stderr);
        return kExitUsage;
    }
    const std::optional<AgentToolOutcome> outcome = tool->invoke(client, arguments, error);
    if (!outcome) {
        PrintUtf8(error, stderr);
        return kExitConnection;
    }
    return print(outcome->result);
}
