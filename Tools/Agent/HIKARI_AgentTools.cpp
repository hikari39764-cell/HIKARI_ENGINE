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

std::optional<AgentToolOutcome> Finish(std::optional<nlohmann::json> result) {
    if (!result)
        return std::nullopt;
    AgentToolOutcome outcome{};
    outcome.images = CollectImages(*result);
    outcome.result = std::move(*result);
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
    const nlohmann::json parameters = arguments.value("arguments", nlohmann::json::object());
    if (!parameters.is_object()) {
        outError = "arguments must be a JSON object";
        return std::nullopt;
    }
    return Finish(
        client.CallFollowingPoll(arguments.value("operation", std::string{}), parameters, kPollWait, outError));
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
    return Finish(client.CallFollowingPoll("capture.preview", parameters, kPollWait, outError));
}

std::optional<AgentToolOutcome> InvokePlay(AgentClient& client, const nlohmann::json& arguments,
                                           std::string& outError) {
    const std::string action = arguments.value("action", std::string{});
    nlohmann::json parameters = nlohmann::json::object();
    for (const char* key : {"ticks", "track", "path", "captureAt", "pauseAfter"}) {
        if (arguments.contains(key))
            parameters[key] = arguments[key];
    }
    std::string operation = "play." + action;
    if (action == "pause" || action == "resume") {
        operation = "play.pause";
        parameters = {{"paused", action == "pause"}};
    } else if (action == "input" && arguments.value("stop", false)) {
        parameters = {{"stop", true}};
    } else if (action != "start" && action != "stop" && action != "status" && action != "step" && action != "input" &&
               action != "run") {
        outError = "action must be start, stop, status, pause, resume, step, input or run";
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
         "Find editor operations by purpose (English or Chinese keywords). Returns operation ids, summaries and "
         "parameters. Use it instead of guessing operation names.",
         Schema({{"keyword", {{"type", "string"}, {"description", "Purpose or operation name, e.g. 'light color'"}}},
                 {"workspace", {{"type", "string"}, {"description", "Optional workspace id filter"}}},
                 {"limit", {{"type", "integer"}, {"description", "1 to 32, default 12"}}},
                 {"offset", {{"type", "integer"}, {"description", "nextOffset from the previous page"}}}},
                {"keyword"}),
         InvokeSearch},
        {"hikari_describe", "Full parameter schema of one operation.",
         Schema({{"operation", {{"type", "string"}, {"description", "Operation id from hikari_search"}}}},
                {"operation"}),
         InvokeDescribe},
        {"hikari_run",
         "Run one editor operation. Edits are visible in the editor and can be undone there. Asynchronous "
         "operations are followed for up to 30 seconds. Saving, imports and external processes wait for the user's "
         "approval in the editor.",
         Schema({{"operation", {{"type", "string"}, {"description", "Operation id"}}},
                 {"arguments", {{"type", "object"}, {"description", "Parameters of the operation"}}}},
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
         "Capture the editor's displayed output as an image to check your work. source: auto (default), game, "
         "space or preview.",
         Schema({{"source", {{"type", "string"}, {"enum", {"auto", "game", "space", "preview"}}}},
                 {"path", {{"type", "string"}, {"description", "Optional output .png path"}}}}),
         InvokeCapture},
        {"hikari_play",
         "Run and test the game. action: start, stop, status, pause, resume, step (exact ticks), input (play an "
         "InputTrack: {formatVersion 1, assetType InputTrack, length, events [{tick, action, value}]}), run (one call: "
         "start if needed, optional input track, advance ticks, capture at captureAt ticks, pause; returns captures and "
         "warnings). Ticks are fixed simulation steps (usually 60 per second).",
         Schema(
             {{"action",
               {{"type", "string"}, {"enum", {"start", "stop", "status", "pause", "resume", "step", "input", "run"}}}},
              {"ticks", {{"type", "integer"}, {"description", "step: 1-3600, run: 1-36000"}}},
              {"track", {{"type", "object"}, {"description", "InputTrack for input or run"}}},
              {"path", {{"type", "string"}, {"description", ".hinputtrack file instead of track"}}},
              {"captureAt", {{"type", "array"}, {"items", {{"type", "integer"}}}}},
              {"pauseAfter", {{"type", "boolean"}}},
              {"stop", {{"type", "boolean"}, {"description", "input: stop the current track"}}}},
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

const AgentTool* FindAgentTool(std::string_view name) {
    const auto& tools = AgentTools();
    const auto found =
        std::find_if(tools.begin(), tools.end(), [name](const AgentTool& tool) { return tool.name == name; });
    return found != tools.end() ? &*found : nullptr;
}

const char* AgentUsageInstructions() {
    return "HIKARI editor tools. Start with hikari_state, then hikari_search to find operations and hikari_describe "
           "for their parameters. Change scenes and assets only through operations (hikari_run / hikari_batch), "
           "never by editing asset files directly; game rules belong in the project's C++ under Source/ using the "
           "installed SDK. Group related edits with hikari_batch, then verify with hikari_capture (content) or hikari_play run "
           "(gameplay) and hikari_logs before reporting. "
           "Read AGENTS.md in the project root for project conventions.";
}

} // namespace HIKARI::AGENT_CLI
