#include "HIKARI_AgentTools.h"

#include <algorithm>
#include <chrono>

namespace HIKARI::AGENT_CLI {
namespace {

// 非同期の操作（截图・書き出し・構築）を待つ上限。超えたら pending の結果と poll をそのまま返す。
constexpr std::chrono::seconds kPollWait{30};

std::filesystem::path FromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

// 截图（data.images）と play.run の截图（data.captures）のパス。
std::vector<std::filesystem::path> CollectImages(const nlohmann::json& result) {
    std::vector<std::filesystem::path> images;
    if (!result.contains("data") || !result["data"].is_object())
        return images;
    for (const char* list : {"images", "captures"}) {
        const auto found = result["data"].find(list);
        if (found == result["data"].end() || !found->is_array())
            continue;
        for (const nlohmann::json& image : *found) {
            if (image.is_object() && image.contains("path") && image["path"].is_string())
                images.push_back(FromUtf8(image["path"].get<std::string>()));
        }
    }
    return images;
}

std::optional<AgentToolOutcome> Finish(std::optional<nlohmann::json> result, bool fullSizeImages = false) {
    if (!result)
        return std::nullopt;
    AgentToolOutcome outcome{};
    outcome.images = CollectImages(*result);
    outcome.result = std::move(*result);
    outcome.fullSizeImages = fullSizeImages;
    return outcome;
}

std::optional<AgentToolOutcome> InvokeState(AgentClient& client, const nlohmann::json&, std::string& outError) {
    return Finish(client.Call("editor.state", nlohmann::json::object(), outError));
}

std::optional<AgentToolOutcome> InvokeSearch(AgentClient& client, const nlohmann::json& arguments,
                                             std::string& outError) {
    nlohmann::json parameters{{"keyword", arguments.value("keyword", std::string{})}};
    for (const char* key : {"workspace", "limit", "offset", "includeParameters"}) {
        if (arguments.contains(key))
            parameters[key] = arguments[key];
    }
    return Finish(client.Call("operations.search", parameters, outError));
}

std::optional<AgentToolOutcome> InvokeDescribe(AgentClient& client, const nlohmann::json& arguments,
                                               std::string& outError) {
    return Finish(
        client.Call("operations.describe", {{"operation", arguments.value("operation", std::string{})}}, outError));
}

std::optional<AgentToolOutcome> InvokeRun(AgentClient& client, const nlohmann::json& arguments, std::string& outError) {
    const nlohmann::json parameters = arguments.value("parameters", nlohmann::json::object());
    if (!parameters.is_object()) {
        outError = "parameters must be a JSON object";
        return std::nullopt;
    }
    return Finish(
        client.CallFollowingPoll(arguments.value("operation", std::string{}), parameters, kPollWait, outError),
        arguments.value("full", false));
}

std::optional<AgentToolOutcome> InvokeBatch(AgentClient& client, const nlohmann::json& arguments,
                                            std::string& outError) {
    nlohmann::json parameters{{"steps", arguments.value("steps", nlohmann::json{})}};
    if (arguments.contains("rollbackOnError"))
        parameters["rollbackOnError"] = arguments["rollbackOnError"];
    return Finish(client.Call("operations.batch", parameters, outError));
}

std::optional<AgentToolOutcome> InvokeCapture(AgentClient& client, const nlohmann::json& arguments,
                                              std::string& outError) {
    nlohmann::json parameters{{"source", arguments.value("source", std::string("auto"))}};
    if (arguments.contains("path"))
        parameters["path"] = arguments["path"];
    return Finish(client.CallFollowingPoll("capture.preview", parameters, kPollWait, outError),
                  arguments.value("full", false));
}

std::optional<AgentToolOutcome> InvokePlay(AgentClient& client, const nlohmann::json& arguments,
                                           std::string& outError) {
    const std::string action = arguments.value("action", std::string{});
    nlohmann::json parameters = nlohmann::json::object();
    for (const char* key : {"ticks", "track", "path", "captureAt", "trackObjects", "pauseAfter"}) {
        if (arguments.contains(key))
            parameters[key] = arguments[key];
    }
    std::string operation = "play." + action;
    if (action == "background") {
        // 後ろの無頭の编辑器で play.run を一回。pauseAfter は使わない。
        parameters.erase("pauseAfter");
        if (arguments.contains("quality"))
            parameters["quality"] = arguments["quality"];
        // 引擎の起動と资产の読み込みに数十秒かかる。
        const auto wait = std::chrono::seconds(90 + arguments.value("ticks", 600) / 20);
        return Finish(client.CallFollowingPoll(operation, parameters, wait, outError), arguments.value("full", false));
    }
    if (action == "pause" || action == "resume") {
        operation = "play.pause";
        parameters = {{"paused", action == "pause"}};
    } else if (action == "input" && arguments.value("stop", false)) {
        parameters = {{"stop", true}};
    } else if (action != "start" && action != "stop" && action != "status" && action != "step" && action != "input" &&
               action != "run") {
        outError = "action must be background, start, stop, status, pause, resume, step, input or run";
        return std::nullopt;
    }
    // play.run は ticks に比例して待つ（锁步は 1 フレーム 1 tick）。
    const auto wait = action == "run" ? std::chrono::seconds(30 + arguments.value("ticks", 0) / 20) : kPollWait;
    return Finish(client.CallFollowingPoll(operation, parameters, wait, outError));
}

std::optional<AgentToolOutcome> InvokeLogs(AgentClient& client, const nlohmann::json& arguments,
                                           std::string& outError) {
    nlohmann::json parameters = nlohmann::json::object();
    for (const char* key : {"since", "minLevel", "contains", "limit"}) {
        if (arguments.contains(key))
            parameters[key] = arguments[key];
    }
    return Finish(client.Call("log.read", parameters, outError));
}

nlohmann::json Schema(nlohmann::json properties, nlohmann::json required = nlohmann::json::array()) {
    return {{"type", "object"}, {"properties", std::move(properties)}, {"required", std::move(required)}};
}

} // namespace

const std::vector<AgentTool>& AgentTools() {
    static const std::vector<AgentTool> tools = {
        {"hikari_state",
         "Live editor state: project, open scene, active workspace document, selection with components, editor "
         "camera, Play state, unsaved documents and pending approvals. Call this first.",
         Schema(nlohmann::json::object()), InvokeState},
        {"hikari_search",
         "Find editor operations by purpose (English or Chinese keywords). Returns operation ids, summaries and the "
         "names of required and optional parameters; call hikari_describe only when you need a parameter's exact "
         "shape. Use it instead of guessing operation names.",
         Schema(
             {{"keyword", {{"type", "string"}, {"description", "Purpose or operation name, e.g. 'light color'"}}},
              {"workspace", {{"type", "string"}, {"description", "Optional workspace id filter"}}},
              {"limit", {{"type", "integer"}, {"description", "1 to 32, default 8"}}},
              {"offset", {{"type", "integer"}, {"description", "nextOffset from the previous page"}}},
              {"includeParameters", {{"type", "boolean"}, {"description", "Full parameter schemas, default false"}}}},
             {"keyword"}),
         InvokeSearch},
        {"hikari_describe", "Full parameter schema of one operation.",
         Schema({{"operation", {{"type", "string"}, {"description", "Operation id from hikari_search"}}}},
                {"operation"}),
         InvokeDescribe},
        {"hikari_run",
         "Run one editor operation. Edits are visible in the editor and can be undone there; while the user is "
         "dragging something in the scene, edits wait until they let go. Asynchronous operations are followed for up "
         "to 30 seconds. Saving, imports and external processes wait for the user's approval in the editor. Images in "
         "the result are scaled to 768 px on the long side unless full is true.",
         Schema({{"operation", {{"type", "string"}, {"description", "Operation id"}}},
                 {"parameters", {{"type", "object"}, {"description", "Parameters of the operation"}}},
                 {"full", {{"type", "boolean"}, {"description", "Return images at their original size"}}}},
                {"operation"}),
         InvokeRun},
        {"hikari_batch",
         "Run up to 64 operations in one call; Scene edits become one undo step and a failure undoes them. Each step "
         "is {\"operation\": operation id, \"parameters\": {...}}. Prefer this over many hikari_run calls.",
         Schema({{"steps", {{"type", "array"}, {"items", {{"type", "object"}}}}},
                 {"rollbackOnError", {{"type", "boolean"}, {"description", "Default true"}}}},
                {"steps"}),
         InvokeBatch},
        {"hikari_capture",
         "Capture what the user currently sees in the editor (their viewport or a workspace preview). Use it only "
         "when asked to look at the user's view. To check your own work, run scene.capture_views with hikari_run: it "
         "renders in the background from any viewpoint without moving the user's view. source: auto (default), game, "
         "space or preview. The image is scaled to 768 px on the long side unless full is true.",
         Schema(
             {{"source", {{"type", "string"}, {"enum", {"auto", "game", "space", "preview"}}}},
              {"path", {{"type", "string"}, {"description", "Optional output .png path; omit to keep it in Library"}}},
              {"full", {{"type", "boolean"}, {"description", "Return the image at its original size"}}}}),
         InvokeCapture},
        {"hikari_play",
         "Run and test the game. action background (use this by default): play the current scene, saved or not, once "
         "in a separate headless editor for ticks fixed ticks (60 per second) with an optional track, capture the game "
         "view at captureAt ticks, record positions of trackObjects every 30 ticks, and return captures, positions and "
         "log warnings and errors; the user's editor is untouched and the user approves starting the process. The "
         "other actions run Play in the user's editor (it takes over the game view and needs the scene saved), so use "
         "them only when the user wants to watch: "
         "start, stop, status, pause, resume, step (exact ticks), input (play an "
         "InputTrack: {formatVersion 1, assetType InputTrack, length, events [{tick, action, value}]}), run (one call: "
         "start if needed, optional input track, advance ticks, capture at captureAt ticks, pause; returns captures and "
         "warnings). Ticks are fixed simulation steps (usually 60 per second).",
         Schema({{"action",
                  {{"type", "string"},
                   {"enum", {"background", "start", "stop", "status", "pause", "resume", "step", "input", "run"}}}},
                 {"ticks", {{"type", "integer"}, {"description", "step: 1-3600, run: 1-36000, background: 1-7200"}}},
                 {"track", {{"type", "object"}, {"description", "InputTrack for input or run"}}},
                 {"path", {{"type", "string"}, {"description", ".hinputtrack file instead of track"}}},
                 {"captureAt", {{"type", "array"}, {"items", {{"type", "integer"}}}}},
                 {"trackObjects",
                  {{"type", "array"},
                   {"items", {{"type", "integer"}}},
                   {"description", "run/background: object ids whose positions are recorded every 30 ticks"}}},
                 {"pauseAfter", {{"type", "boolean"}}},
                 {"stop", {{"type", "boolean"}, {"description", "input: stop the current track"}}},
                 {"quality",
                  {{"type", "string"},
                   {"enum", {"light", "project"}},
                   {"description", "background: light (default, 1280x720) or project (the game's render quality)"}}},
                 {"full", {{"type", "boolean"}, {"description", "Return images at their original size"}}}},
                {"action"}),
         InvokePlay},
        {"hikari_logs",
         "Editor and game log records after a sequence number; use nextSince next time. minLevel: info, warning, error.",
         Schema({{"since", {{"type", "integer"}}},
                 {"minLevel", {{"type", "string"}, {"enum", {"info", "warning", "error"}}}},
                 {"contains", {{"type", "string"}}},
                 {"limit", {{"type", "integer"}}}}),
         InvokeLogs},
    };
    return tools;
}

std::optional<std::string> NormalizeAgentToolArguments(const AgentTool& tool, nlohmann::json& arguments) {
    const nlohmann::json& properties = tool.inputSchema["properties"];
    for (auto& [key, value] : arguments.items()) {
        const auto declared = properties.find(key);
        if (declared == properties.end()) {
            std::string names;
            for (const auto& [name, schema] : properties.items())
                names += (names.empty() ? "" : ", ") + name;
            return "Unknown argument " + key + " for " + tool.name + "; it takes: " + names + ".";
        }
        const std::string type = declared->value("type", std::string{});
        if ((type == "object" || type == "array") && value.is_string()) {
            nlohmann::json parsed = nlohmann::json::parse(value.get<std::string>(), nullptr, false);
            if (parsed.is_discarded() || (type == "object" ? !parsed.is_object() : !parsed.is_array()))
                return key + " must be a JSON " + type + ".";
            value = std::move(parsed);
        }
    }
    return std::nullopt;
}

const AgentTool* FindAgentTool(std::string_view name) {
    const auto& tools = AgentTools();
    const auto found =
        std::find_if(tools.begin(), tools.end(), [name](const AgentTool& tool) { return tool.name == name; });
    return found != tools.end() ? &*found : nullptr;
}

const char* AgentUsageInstructions() {
    return "HIKARI editor tools. The user keeps working in the same editor, so work in the background: do not move "
           "their view, change their selection or switch workspaces unless asked. Start with hikari_state, then "
           "hikari_search to find operations (hikari_describe only when a parameter's shape is unclear). Change scenes "
           "and assets only through operations (hikari_run / hikari_batch), never by editing asset files; game rules "
           "belong in the project's C++ under Source/ using the installed SDK. Group related edits with hikari_batch, "
           "then check them with scene.capture_views (background renders) and hikari_logs before reporting. Use "
           "hikari_capture only to see the user's current view, and hikari_play only when the user wants to watch Play "
           "in the editor. Read AGENTS.md in the project root for the quick reference and project conventions.";
}

} // namespace HIKARI::AGENT_CLI
