#include "Editor/Agent/HIKARI_AgentProjectFiles.h"

#include "Core/Text/HIKARI_UtfPath.h"
#include "Core/Paths/HIKARI_ExecutablePath.h"

#include <fstream>
#include <iterator>
#include <system_error>

#include <json.hpp>

namespace HIKARI::EDITOR::AGENT {
namespace {

constexpr std::string_view kConventionsHeading = "## 项目约定";
// Windows PowerShell 5.1 は BOM の無い UTF-8 を ANSI として読み、中文が化ける。智能体が命令で読んでも崩れないように付ける。
constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

std::string GenericUtf8(const std::filesystem::path& path) { return TEXT::PathToUtf8(path.generic_wstring()); }

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return input ? std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()) : std::string{};
}

// 内容が同じなら書かない（更新日時と版本管理の差分を増やさない）。書いたものだけ outWritten に足す。
bool WriteIfChanged(const std::filesystem::path& path, const std::string& text,
                    std::vector<std::filesystem::path>& outWritten, std::string& outError) {
    if (ReadText(path) == text)
        return true;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    if (!output) {
        outError = "无法写入 " + TEXT::PathToUtf8(path);
        return false;
    }
    outWritten.push_back(path);
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
- **引擎目录只读**：任何模式下都不要写入引擎的安装目录（编辑器会直接拒绝），所有文件改动都留在本项目目录里。
  操作要输出文件时，用项目内的绝对路径，或者不给路径（默认存到 `Library`）；相对路径按编辑器的工作目录解析。
- **和用户并行**：用户和你同时使用这个编辑器。不要移动用户的视角、改用户的选中、切换工作区或开始 Play，除非用户要求。
- **验证**：内容改动后截图确认；汇报时说明做了什么、证据是什么、哪些还没验证。

## 连接编辑器

编辑器打开本项目时本地桥默认开启，智能体就能连接（“设置 › AI 本地桥”可以关闭）。

- **MCP**：`.mcp.json` 已注册 `hikari` 服务器，Claude Code 会自动读取。工具：`hikari_state`、`hikari_search`、`hikari_describe`、`hikari_run`、`hikari_batch`、`hikari_capture`、`hikari_play`、`hikari_logs`。
  有 MCP 工具时优先用它们：命令行在 Codex 等沙箱里运行时读不到连接文件。
- **命令行**（在项目目录里运行）：
)";
    text += "  - `\"" + hikari + "\" state`：编辑器现场（场景、选中对象、相机、Play、待审批）\n";
    text += "  - `\"" + hikari + "\" search 关键词`、`describe 操作`：找操作、看参数\n";
    text += "  - `\"" + hikari +
            "\" run 操作 '{\"参数\": 值}'`：执行一个操作（MCP 的 `hikari_run` 把参数放在 `parameters` 里）\n";
    text += "  - `\"" + hikari +
            "\" batch steps.json`：一次执行一组操作（场景改动合成一个撤销步，失败时自动撤回），"
            "每步是 `{\"operation\": 操作, \"parameters\": {...}}`\n";
    text += "  - `\"" + hikari + "\" capture`：截取用户当前看到的画面（图片存在 `Library/EditorAI/Captures`）\n";
    text += "  - `\"" + hikari +
            "\" play background 600 --track walk.json --capture 120,240 --track-objects 11`：在后台另开一个无头"
            "编辑器运行当前场景（不用保存，不占用用户的编辑器），回放输入轨道、截游戏画面、记录物体位置，"
            "返回截图、位置和日志里的警告与错误\n";
    text += "  - `\"" + hikari +
            "\" play run 300 --track walk.json --capture 60,240`：在用户的编辑器里 Play（用户要看时才用）\n";
    text += "  - `\"" + hikari + "\" logs --level warning`：读取编辑器和游戏日志\n";
    text += "  - `\"" + hikari +
            "\" headless 600 --track walk.json --capture 300`：编辑器没开时，用保存的开始场景做同样的运行\n";
    text += "- **Codex 使用 MCP**：在 `~/.codex/config.toml` 加入下面三行；不注册也可以直接用上面的命令行。\n\n";
    text += "  ```toml\n  [mcp_servers.hikari]\n  command = \"" + hikari + "\"\n  args = [\"mcp\"]\n  ```\n\n";
    text += R"(## 工作方式

1. 先读编辑器现场（`hikari_state`），确认打开的场景、选中对象和工作区。
2. 先看下面的速查；不在速查里的用搜索找，不要猜操作名。搜索结果带必填和可选参数名，参数形状不清楚时再 describe。
3. 相关的多步修改用 batch 一次提交，减少往返。
4. 场景里的修改（创建、改属性、布景、灯光）直接生效、可以撤销，不需要批准；截图存到 `Library` 也不需要。
   写入磁盘（保存、新建资产、导入）和启动外部程序（构建、导出）要等用户在编辑器里批准后才返回；`hikari_state` 的
   `agent.autoApprove` 为 true 时用户开了自动模式，这些操作会直接执行。场景本身用户没要求时不要保存；任务本身就需要的
   新资产（镜头配置、材质、序列、对话、特效）直接新建并保存，不用先问。
   操作所属的 workspace 只是分类：调用操作不会切换用户的工作区（名字带 `reveal` 的除外）。
5. 内容改动用 `scene.capture_views` 截图确认（后台渲染，不动用户的视角）；玩法改动用 `hikari_play` 的
   `background`（`play.background`）验证：在后台另开一个无头引擎运行编辑器里当前的场景，不用保存，也不占用用户的
   编辑器；启动外部进程要用户批准（同一轮里批准一次即可）。看返回的截图和 `problems`，然后汇报。输入轨道的格式：
   `{"assetType": "InputTrack", "formatVersion": 1, "length": tick 数, "events": [{"tick": 0, "action": "Gameplay.Move", "value": [0, 1]}]}`，
   动作名来自项目的输入设置，值保持到同一动作的下一次变化。
6. 用户正在拖动场景里的东西时，你的修改会等用户松手再执行，不用重试。
7. 报 `Unknown component type` 或 `FEATURE_DISABLED` 时，多半是项目没开对应的运行时功能（UI、Vfx、Cinematics 等）：
   `project.features` 查看，`project.features.edit {"enable": [...]}` 开启（立即生效，不用重启）。

## 常用操作速查

- 看自己的修改：`scene.capture_views`，一般给 1～2 个视角就够（每张图都会留在对话里）。`views` 可以写：
  - `editor`：从用户当前的编辑相机位置拍，不移动相机。在封闭的房间里，绕物体旋转的视角容易落在墙外，用这个最稳；
  - `front`、`three_quarter`、`right`、`back`、`left`、`top`，或 `{"azimuth": 45, "elevation": 20, "distance": 6}`
    （方位 0 是 +Z 一侧，90 是 +X 一侧），绕 `focusObjectId` 或 `center` 旋转；
  - `{"position": [...], "target": [...]}`：任意机位。
  场景很暗、看不清颜色或形状时加 `"shading": "neutral"`：均匀的灰色照明，形状和底色都清楚，不受场景灯光影响；
  `"unlit"` 只有底色、没有明暗，同色的面会连成一片。确认灯光效果时用默认的 `lit`。
  `hikari_run`（命令行的 `run`）会等截图完成，直接返回图片。
- 查物体：`scene.objects.query`，默认返回 id、名称、组件、世界位置和包围盒；要模型和材质槽时加 `"detail": true`。
- 改组件字段：`component.describe` 看字段 id 和当前值（`propertyIds` 只看需要的字段；资产一类的长选项只给 `choiceCount`），
  `component.property.set` 改一个（返回 `oldValue` / `newValue`，不用再回读），`authoring.transaction` 一次改多个（一个撤销步）。
- 改颜色：物体的颜色来自材质。`material.list_assets` 带 `"nearColor": [r, g, b]`（线性 RGB）按底色远近列出最接近的现成材质，
  选一个后把 `ProceduralMeshComponent` 的 `materialSelection` 设成它的 `guid`。不要直接改共用的材质，否则所有用它的物体都会变。
- 搭布景：`procedural.create_primitive`（方块、墙、地板、柱、坡、台阶）、`procedural.opening`（门、窗、洞，含圆形）、
  `procedural.subtract`（挖空）、`scene.arrange`（对齐、等距、落地、编组、镜像、阵列）。
- 灯光：`scene.create_light`、`lighting.three_point`。给物体加“发光”或点缀用的灯时，放在物体中心附近并关掉投影
  （`settings` 里 `"castsShadows": false`）：灯贴着物体又投影，物体会在地上投出一个放大的、比太阳影子还深的影子。
- 移动、删除、复制：`scene.set_transform`、`scene.delete_object`、`scene.duplicate_object`（传 `objectId`，不会改用户的选中）。
  位置、旋转（度）、缩放和颜色在所有操作里都写成 `[x, y, z]`。
- 游戏镜头区域：`camera_zone.edit`。
- 特效与流体烟雾：改已有的特效一律 `vfx.open_asset` → `vfx.property.set` → `vfx.save`，在原资产上改。不要用脚本整份
  重新生成或覆盖 `.hvfx` 文件，那样会冲掉用户在编辑器里调过的值；`vfx.new_asset` 只用于项目里还没有的特效。
  加层：`vfx.emitter.add`（`kind` 为 `Particles`、`Fluid`（流体域）或 `FluidSource`（流体源：`parentEmitterId` 给流体域
  的 id 就注入那个域；不放在流体域下就是“流体注入”，跟着特效走，注入到形状碰到的场景流体域，如剑气尾迹、烟弹打进
  场地的烟雾域；它的力场、风和阻力只作用在形状附近））→ `vfx.query` 拿各层的模块 id → `vfx.property.set`（`targetId` 是模块 id，`property` 是模块里的字段路径，
  如 `domainSize`、`extinction`、`emissionNoise`、`radius.maximum`）→ `vfx.save`。范围（最小～最大）先设会把范围放宽的
  那一端，否则会被拒绝。解算看 `vfx.fluid.stats` 和 `vfx.fluid.debug_slice`；碎屑、火星跟着烟走，给粒子层的力模块设
  `fluidFollow`。火的颜色：流体域的 `emissionColor`/`emissionTemperature` 是起亮的颜色和温度，`emissionHotColor`/
  `emissionHotTemperature` 是高温端（白黄的芯）。同一个域里要有不同颜色的烟：流体域开 `cellColor`，各流体源层的颜色模块
  决定注入的烟色（发光和散射都乘这个颜色）。火与爆炸：流体域开 `combustion`（`ignitionTemperature`、`burnRate`、
  `fuelPerBurn`、`heatPerBurn`、`smokePerBurn`、`expansionPerBurn`），流体源的 `fuelFraction` 设为 1 注入燃料、
  `temperature` 高于点燃温度；燃料燃烧放热、生出会发光的高温烟灰并膨胀，燃料烧完火自己熄灭。爆炸用 burst 一次注入大量燃料。
- 对话：`dialogue.new_asset` → `dialogue.edit`（传整份定义，`dialogue.query` 返回当前定义）→ `dialogue.save`。
- 界面文字（HUD 等）：`ui.create`。`scene.capture_views` 只画 3D 场景，界面要在 `play.background` 的截图里看。引擎不自带字体，项目里没有字体资产时文字不显示（结果里有 `warning`），
  先用 `asset.import` 导入 .ttf/.otf，或请用户提供字体。
- 撤回自己的修改：`scene.undo`。修改的结果里 `undo.label` 是这次积下的撤销步；撤销栈和用户共用，`hikari_state` 的
  `scene.undoLabel` 不是你的修改时不要撤。
- 测玩法：`play.background`（`ticks`，每秒 60；`track` 或 `path` 输入轨道；`captureAt` 截游戏画面的 tick；
  `trackObjects` 每 30 tick 记录这些物体的世界位置，用来证明东西移动到了哪里——游戏摄像机拍不到的地方也能确认）。
  默认用轻量画质（1280×720，降低 GI 和体积效果），截图要和正式画面一致时加 `"quality": "project"`。后台运行只带上
  当前场景未保存的修改；其他资产（材质、VFX、镜头等）从磁盘读，改过的要先保存（保存要用户批准）。
- 只在用户要求时用：`hikari_capture`（用户当前的画面）、`hikari_play` 的 start / run 等和 `play.run`（在编辑器里 Play，
  会占用游戏视图，要求场景已保存）、`scene.object.focus`（会移动用户的视角）。

本文件是带 BOM 的 UTF-8。在 Windows PowerShell 5.1 里读项目的其他文本文件时，加 `-Encoding UTF8`。

## 构建项目代码

需要编辑器的“开发项目”访问模式。

1. 直接修改 `Source/` 下的代码。
2. `project.build`：编辑器在后台构建当前源码的副本（不影响正在用的模块），用 `project.build.status` 看编译和
   `Tests/HikariVerification.json` 行为检查的结果。
3. 测试规则不用重载：`project.build` 通过后，`play.background` 直接用这次构建的模块（结果里 `module` 为 `projectBuild`）。
   新写的系统只在 `GetDefaultSystems()` 里声明时，已有场景不会运行它：用 `BindComponentSystem` 把系统绑到组件上
   （场景里有该组件就自动导入），日志的 `[SceneSystem] active schedule` 能看到正在运行的系统。
4. 只有要在编辑器里给物体加新写的组件、或让编辑器加载新代码时才 `project.reload`：重新构建并重启编辑器，
   要求文档已保存、不在 Play；编辑器重启后 hikari 会自动重连（访问模式和自动模式保持不变）。

## 参考资料（已安装 SDK）

)";
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
    if (!WriteIfChanged(agentsPath, std::string(kUtf8Bom) + MakeAgentsGuide(inputs, kept), outWritten, outError) ||
        !WriteIfChanged(projectRoot / "CLAUDE.md", "@AGENTS.md\n", outWritten, outError))
        return false;

    if (!inputs.hikariExecutable.empty()) {
        const nlohmann::json mcp{
            {"mcpServers",
             {{"hikari",
               {{"command", GenericUtf8(inputs.hikariExecutable)}, {"args", nlohmann::json::array({"mcp"})}}}}}};
        if (!WriteIfChanged(projectRoot / ".mcp.json", mcp.dump(2) + "\n", outWritten, outError))
            return false;
    }
    return true;
}

} // namespace HIKARI::EDITOR::AGENT
