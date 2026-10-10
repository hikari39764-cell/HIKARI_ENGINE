#include "Editor/Agent/HIKARI_AgentProjectFiles.h"

#include "Core/Text/HIKARI_UtfPath.h"
#include "Platform/Paths/HIKARI_ExecutablePath.h"

#include <fstream>
#include <iterator>
#include <system_error>

#include <json.hpp>

namespace HIKARI::EDITOR::AGENT {
namespace {

constexpr std::string_view kConventionsHeading = "## 项目约定";

std::string GenericUtf8(const std::filesystem::path& path) { return TEXT::PathToUtf8(path.generic_wstring()); }

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return input ? std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()) : std::string{};
}

bool WriteText(const std::filesystem::path& path, const std::string& text, std::string& outError) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    if (!output) {
        outError = "无法写入 " + TEXT::PathToUtf8(path);
        return false;
    }
    return true;
}

std::string MakeAgentsGuide(const AgentProjectFileInputs& inputs, const std::string& conventions) {
    const std::string hikari = inputs.hikariExecutable.empty() ? std::string("hikari.exe（未找到，请确认引擎安装完整）")
                                                               : GenericUtf8(inputs.hikariExecutable);
    const std::string sdk = GenericUtf8(inputs.sdkRoot);
    std::string text = "# " + inputs.projectName + "：智能体工作说明\n\n";
    text += "本项目使用 HIKARI 引擎。本文件由编辑器生成（设置 › AI 本地桥 › 写入智能体说明）。重新生成时只保留“" +
            std::string(kConventionsHeading.substr(3)) + "”一节，其余内容会更新。\n\n";
    text += R"(## 职责分工

- **内容**（场景、资产、材质、VFX、镜头、UI）：只通过编辑器操作修改，不要直接编辑 `Assets/`、`ProjectSettings/` 里的文件。可以读这些文件来理解结构。
- **规则**（判定、伤害、流程、游戏状态）：写在 `Source/` 的 C++ 里，只使用已安装 SDK 的公开头文件和文档，不读、不改引擎源码。
- **验证**：内容改动后截图确认；汇报时说明做了什么、证据是什么、哪些还没验证。

## 连接编辑器

编辑器打开本项目时本地桥默认开启，智能体就能连接（“设置 › AI 本地桥”可以关闭）。

- **MCP**：`.mcp.json` 已注册 `hikari` 服务器，Claude Code 会自动读取。工具：`hikari_state`、`hikari_search`、`hikari_describe`、`hikari_run`、`hikari_batch`、`hikari_capture`、`hikari_play`、`hikari_logs`。
- **命令行**（在项目目录里运行）：
)";
    text += "  - `\"" + hikari + "\" state`：编辑器现场（场景、选中对象、相机、Play、待审批）\n";
    text += "  - `\"" + hikari + "\" search 关键词`、`describe 操作`：找操作、看参数\n";
    text += "  - `\"" + hikari + "\" run 操作 '{\"参数\": 值}'`：执行一个操作\n";
    text += "  - `\"" + hikari +
            "\" batch steps.json`：一次执行一组操作（场景改动合成一个撤销步，失败时自动撤回），"
            "每步是 `{\"operation\": 操作, \"parameters\": {...}}`\n";
    text += "  - `\"" + hikari + "\" capture --out shot.png`：截取编辑器显示的画面\n";
    text += "  - `\"" + hikari +
            "\" play run 300 --track walk.json --capture 60,240`：开始 Play、回放输入轨道、"
            "按固定步长前进 300 tick 并截图，返回截图和警告\n";
    text += "  - `\"" + hikari + "\" logs --level warning`：读取编辑器和游戏日志\n";
    text += "  - `\"" + hikari + "\" headless --input-replay walk.json --frames 600`：不连接编辑器，无头运行一次\n";
    text += "- **Codex 使用 MCP**：在 `~/.codex/config.toml` 加入下面三行；不注册也可以直接用上面的命令行。\n\n";
    text += "  ```toml\n  [mcp_servers.hikari]\n  command = \"" + hikari + "\"\n  args = [\"mcp\"]\n  ```\n\n";
    text += R"(## 工作方式

1. 先读编辑器现场（`hikari_state`），确认打开的场景、选中对象和工作区。
2. 用搜索找操作，不要猜操作名；执行前用 describe 看参数。
3. 相关的多步修改用 batch 一次提交，减少往返。
4. 保存、导入、构建等会修改项目文件或启动外部程序的操作，要等用户在编辑器里批准后才返回。Play 前要先保存。
5. 内容改动截图确认；玩法改动用 `play run` 验证，再读日志，然后汇报。输入轨道的格式：
   `{"assetType": "InputTrack", "formatVersion": 1, "length": tick 数, "events": [{"tick": 0, "action": "Gameplay.Move", "value": [0, 1]}]}`，
   动作名来自项目的输入设置，值保持到同一动作的下一次变化。

## 构建项目代码

需要编辑器的“开发项目”访问模式。

1. 直接修改 `Source/` 下的代码。
2. `project.build`：编辑器在后台构建当前源码的副本（不影响正在用的模块），用 `project.build.status` 看编译和
   `Tests/HikariVerification.json` 行为检查的结果。
3. 通过后 `project.reload`：重新构建并重启编辑器，加载新的模块。每次都要用户批准，要求文档已保存、不在 Play；
   编辑器重启后 hikari 会自动重连。

## 参考资料（已安装 SDK）

)";
    text += "- 制作游戏指南：`" + sdk + "/docs/game-project/MakingAGame.md`\n";
    text += "- SDK API 参考：`" + sdk + "/docs/game-project/GameProjectSdkApiGuide.md`\n";
    text += "- 职责与工作流：`" + sdk + "/docs/game-project/GameDevelopmentResponsibilities.md`\n";
    text += "- 公开头文件：`" + sdk + "/include/HIKARI/`\n\n";
    text += conventions.empty()
                ? std::string(kConventionsHeading) + "\n\n（在这里写画风、命名、常用资产等项目约定。）\n"
                : conventions;
    return text;
}

} // namespace

AgentProjectFileInputs DetectAgentProjectFileInputs(std::string projectName) {
    AgentProjectFileInputs inputs{};
    inputs.projectName = std::move(projectName);
    const std::filesystem::path executableDirectory = PLATFORM::GetExecutableDirectory();
    inputs.sdkRoot = executableDirectory / "Sdk";
    // 构建树と安装先のどちらでも、hikari.exe は编辑器（Runtime/）と同じ場所か一つ上にある。
    std::error_code error;
    for (const std::filesystem::path& candidate :
         {executableDirectory / "hikari.exe", executableDirectory.parent_path() / "hikari.exe"}) {
        if (std::filesystem::is_regular_file(candidate, error)) {
            inputs.hikariExecutable = candidate;
            break;
        }
    }
    return inputs;
}

bool WriteAgentProjectFiles(const std::filesystem::path& projectRoot, const AgentProjectFileInputs& inputs,
                            std::vector<std::filesystem::path>& outWritten, std::string& outError) {
    outWritten.clear();
    const std::filesystem::path agentsPath = projectRoot / "AGENTS.md";
    const std::string previous = ReadText(agentsPath);
    const size_t conventions = previous.find(kConventionsHeading);
    const std::string kept = conventions == std::string::npos ? std::string{} : previous.substr(conventions);
    if (!WriteText(agentsPath, MakeAgentsGuide(inputs, kept), outError))
        return false;
    outWritten.push_back(agentsPath);

    const std::filesystem::path claudePath = projectRoot / "CLAUDE.md";
    if (!WriteText(claudePath, "@AGENTS.md\n", outError))
        return false;
    outWritten.push_back(claudePath);

    if (!inputs.hikariExecutable.empty()) {
        const nlohmann::json mcp{
            {"mcpServers",
             {{"hikari",
               {{"command", GenericUtf8(inputs.hikariExecutable)}, {"args", nlohmann::json::array({"mcp"})}}}}}};
        const std::filesystem::path mcpPath = projectRoot / ".mcp.json";
        if (!WriteText(mcpPath, mcp.dump(2) + "\n", outError))
            return false;
        outWritten.push_back(mcpPath);
    }
    return true;
}

} // namespace HIKARI::EDITOR::AGENT
