#include "Input/Assets/HIKARI_InputActionMapJson.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_set>

#include <json.hpp>

namespace HIKARI::INPUT {
namespace {

using nlohmann::json;

void SetError(std::string* errorMessage, std::string message) {
    if (errorMessage != nullptr) {
        *errorMessage = std::move(message);
    }
}

std::string ContextFileStem(std::string_view contextId) {
    std::string stem;
    stem.reserve(contextId.size());
    for (unsigned char c : contextId) {
        if (std::isalnum(c) != 0) {
            stem.push_back(TEXT::ToLowerAscii(static_cast<char>(c)));
        } else if (!stem.empty() && stem.back() != '_') {
            stem.push_back('_');
        }
    }
    return stem.empty() ? "context" : stem;
}

json SerializeBinding(const InputBinding& binding) {
    json node{
        { "action", binding.actionId },
        { "source", ToString(binding.source) },
        { "control", binding.control },
        { "scale", binding.scale },
        { "component", binding.component },
    };
    if (!binding.modifierControl.empty()) {
        node["modifier"] = binding.modifierControl;
    }
    if (binding.deadZone > 0.0f) {
        node["deadZone"] = binding.deadZone;
    }
    return node;
}

bool DeserializeBinding(
    const json& node,
    InputBinding& out,
    std::string* errorMessage) {
    if (!node.is_object()) {
        SetError(errorMessage, "Input binding must be an object.");
        return false;
    }
    out.actionId = node.value("action", std::string{});
    out.control = node.value("control", std::string{});
    out.scale = node.value("scale", 1.0f);
    out.component = static_cast<uint8_t>(node.value("component", 0u));
    out.modifierControl = node.value("modifier", std::string{});
    out.deadZone = (std::clamp)(node.value("deadZone", 0.0f), 0.0f, 0.99f);
    const std::string sourceName = node.value("source", std::string{});
    if (out.actionId.empty() || out.control.empty() ||
        !TryParseInputBindingSource(sourceName, out.source)) {
        SetError(errorMessage, "Input binding has an invalid action, source, or control.");
        return false;
    }
    return true;
}

InputBinding Binding(
    std::string action,
    InputBindingSource source,
    std::string control,
    float scale = 1.0f,
    uint8_t component = 0,
    std::string modifier = {},
    float deadZone = 0.0f) {
    return InputBinding{
        std::move(action), source, std::move(control), scale,
        component, std::move(modifier), deadZone };
}

} // namespace

InputActionMap CreateDefaultInputActionMap() {
    InputActionMap map{};
    map.actions = {
        { "Global.ToggleEditorUI", "Toggle Editor UI", InputActionValueType::Button },
        { "Global.StopPlay", "Stop Play", InputActionValueType::Button },
        { "Global.CloseProgram", "Close Program", InputActionValueType::Button },
        { "Gameplay.Move", "Move", InputActionValueType::Axis2D },
        { "Gameplay.Look", "Look", InputActionValueType::Axis2D, false },
        { "Gameplay.CameraZoom", "Camera Zoom", InputActionValueType::Axis1D, false },
        { "Gameplay.CameraRecenter", "Camera Recenter", InputActionValueType::Button },
        { "Gameplay.Jump", "Jump", InputActionValueType::Button },
        { "Gameplay.Sprint", "Sprint", InputActionValueType::Button },
        { "Gameplay.Interact", "Interact", InputActionValueType::Button },
        { "Editor.CameraMove", "Camera Move", InputActionValueType::Axis2D, true },
        { "Editor.CameraMoveVertical", "Camera Move Vertical", InputActionValueType::Axis1D, true },
        { "Editor.CameraLook", "Camera Look", InputActionValueType::Axis2D, false },
        { "Editor.CameraLookHeld", "Camera Look Held", InputActionValueType::Button },
        { "Editor.CameraPan", "Camera Pan", InputActionValueType::Axis2D, false },
        { "Editor.CameraPanHeld", "Camera Pan Held", InputActionValueType::Button },
        { "Editor.CameraZoom", "Camera Zoom", InputActionValueType::Axis1D },
        { "Editor.CameraBoost", "Camera Boost", InputActionValueType::Button },
        { "Editor.CameraReset", "Camera Reset", InputActionValueType::Button },
    };

    InputContextDefinition global{};
    global.contextId = "Global";
    global.displayName = "Global";
    global.priority = 1000;
    global.bindings = {
        Binding("Global.ToggleEditorUI", InputBindingSource::Keyboard, "F1"),
        Binding("Global.StopPlay", InputBindingSource::Keyboard, "Escape"),
    };

    InputContextDefinition editor{};
    editor.contextId = "Editor";
    editor.displayName = "Editor Camera";
    editor.priority = 500;
    editor.consumeInput = true;
    editor.bindings = {
        Binding("Editor.CameraMove", InputBindingSource::Keyboard, "A", -1.0f, 0, "Mouse:Right"),
        Binding("Editor.CameraMove", InputBindingSource::Keyboard, "D", 1.0f, 0, "Mouse:Right"),
        Binding("Editor.CameraMove", InputBindingSource::Keyboard, "W", 1.0f, 1, "Mouse:Right"),
        Binding("Editor.CameraMove", InputBindingSource::Keyboard, "S", -1.0f, 1, "Mouse:Right"),
        Binding("Editor.CameraMoveVertical", InputBindingSource::Keyboard, "E", 1.0f, 0, "Mouse:Right"),
        Binding("Editor.CameraMoveVertical", InputBindingSource::Keyboard, "Q", -1.0f, 0, "Mouse:Right"),
        Binding("Editor.CameraLook", InputBindingSource::MouseDeltaX, "DeltaX", 1.0f, 0, "Mouse:Right"),
        Binding("Editor.CameraLook", InputBindingSource::MouseDeltaY, "DeltaY", 1.0f, 1, "Mouse:Right"),
        Binding("Editor.CameraLookHeld", InputBindingSource::MouseButton, "Right"),
        Binding("Editor.CameraPan", InputBindingSource::MouseDeltaX, "DeltaX", -1.0f, 0, "Mouse:Middle"),
        Binding("Editor.CameraPan", InputBindingSource::MouseDeltaY, "DeltaY", -1.0f, 1, "Mouse:Middle"),
        Binding("Editor.CameraPanHeld", InputBindingSource::MouseButton, "Middle"),
        Binding("Editor.CameraZoom", InputBindingSource::MouseWheel, "Wheel", 1.0f),
        Binding("Editor.CameraBoost", InputBindingSource::Keyboard, "Shift"),
        Binding("Editor.CameraReset", InputBindingSource::Keyboard, "Home"),
    };

    InputContextDefinition gameplay{};
    gameplay.contextId = "Gameplay";
    gameplay.displayName = "Gameplay";
    gameplay.priority = 100;
    gameplay.bindings = {
        Binding("Gameplay.Move", InputBindingSource::Keyboard, "A", -1.0f, 0),
        Binding("Gameplay.Move", InputBindingSource::Keyboard, "Left", -1.0f, 0),
        Binding("Gameplay.Move", InputBindingSource::Keyboard, "D", 1.0f, 0),
        Binding("Gameplay.Move", InputBindingSource::Keyboard, "Right", 1.0f, 0),
        Binding("Gameplay.Move", InputBindingSource::Keyboard, "W", 1.0f, 1),
        Binding("Gameplay.Move", InputBindingSource::Keyboard, "Up", 1.0f, 1),
        Binding("Gameplay.Move", InputBindingSource::Keyboard, "S", -1.0f, 1),
        Binding("Gameplay.Move", InputBindingSource::Keyboard, "Down", -1.0f, 1),
        Binding("Gameplay.Move", InputBindingSource::GamepadAxis, "LeftX", 1.0f, 0, {}, 0.15f),
        Binding("Gameplay.Move", InputBindingSource::GamepadAxis, "LeftY", 1.0f, 1, {}, 0.15f),
        Binding("Gameplay.Look", InputBindingSource::MouseDeltaX, "DeltaX", 1.0f, 0),
        Binding("Gameplay.Look", InputBindingSource::MouseDeltaY, "DeltaY", 1.0f, 1),
        Binding("Gameplay.Look", InputBindingSource::GamepadAxis, "RightX", 1.0f, 0, {}, 0.15f),
        Binding("Gameplay.Look", InputBindingSource::GamepadAxis, "RightY", -1.0f, 1, {}, 0.15f),
        Binding("Gameplay.CameraZoom", InputBindingSource::MouseWheel, "Wheel"),
        Binding("Gameplay.CameraRecenter", InputBindingSource::Keyboard, "R"),
        Binding("Gameplay.CameraRecenter", InputBindingSource::GamepadButton, "RightStick"),
        Binding("Gameplay.Jump", InputBindingSource::Keyboard, "Space"),
        Binding("Gameplay.Jump", InputBindingSource::GamepadButton, "A"),
        Binding("Gameplay.Sprint", InputBindingSource::Keyboard, "Shift"),
        Binding("Gameplay.Sprint", InputBindingSource::GamepadButton, "LeftStick"),
        Binding("Gameplay.Interact", InputBindingSource::Keyboard, "E"),
        Binding("Gameplay.Interact", InputBindingSource::GamepadButton, "X"),
    };

    map.contexts = {
        std::move(global), std::move(editor), std::move(gameplay) };
    return map;
}

bool LoadInputActionMapProject(
    const std::filesystem::path& inputDirectory,
    InputActionMap& out,
    std::string* errorMessage) {
    out = {};
    const std::filesystem::path actionsPath = inputDirectory / "actions.json";
    std::ifstream actionsFile(actionsPath);
    if (!actionsFile) {
        SetError(errorMessage, "Input actions file is missing: " + actionsPath.string());
        return false;
    }
    const json actionsRoot = json::parse(actionsFile, nullptr, false);
    if (!actionsRoot.is_object() || !actionsRoot.contains("actions") ||
        !actionsRoot["actions"].is_array()) {
        SetError(errorMessage, "Input actions file is invalid: " + actionsPath.string());
        return false;
    }
    out.version = actionsRoot.value("version", 1u);
    for (const json& node : actionsRoot["actions"]) {
        InputActionDefinition action{};
        action.actionId = node.value("id", std::string{});
        action.displayName = node.value("displayName", action.actionId);
        action.clampValue = node.value("clamp", true);
        const std::string typeName = node.value("valueType", std::string{});
        if (action.actionId.empty() ||
            !TryParseInputActionValueType(typeName, action.valueType)) {
            SetError(errorMessage, "Input action definition is invalid.");
            return false;
        }
        out.actions.push_back(std::move(action));
    }

    std::error_code ec{};
    for (const std::filesystem::directory_entry& entry :
            std::filesystem::directory_iterator(inputDirectory, ec)) {
        if (ec || !entry.is_regular_file() ||
            entry.path().extension() != ".json" ||
            entry.path().filename() == "actions.json" ||
            entry.path().stem().extension() != ".inputmap") {
            continue;
        }
        std::ifstream contextFile(entry.path());
        const json root = json::parse(contextFile, nullptr, false);
        if (!root.is_object()) {
            SetError(errorMessage, "Input context file is invalid: " + entry.path().string());
            return false;
        }
        InputContextDefinition context{};
        context.contextId = root.value("id", std::string{});
        context.displayName = root.value("displayName", context.contextId);
        context.priority = root.value("priority", 0);
        context.consumeInput = root.value("consumeInput", false);
        context.enabledByDefault = root.value("enabledByDefault", true);
        if (context.contextId.empty() || !root.contains("bindings") ||
            !root["bindings"].is_array()) {
            SetError(errorMessage, "Input context is missing its ID or bindings.");
            return false;
        }
        for (const json& bindingNode : root["bindings"]) {
            InputBinding binding{};
            if (!DeserializeBinding(bindingNode, binding, errorMessage)) {
                return false;
            }
            context.bindings.push_back(std::move(binding));
        }
        out.contexts.push_back(std::move(context));
    }
    if (ec) {
        SetError(errorMessage, "Could not enumerate input contexts: " + ec.message());
        return false;
    }
    const std::vector<std::string> issues = out.Validate();
    if (!issues.empty()) {
        SetError(errorMessage, issues.front());
        return false;
    }
    return true;
}

bool SaveInputActionMapProject(
    const std::filesystem::path& inputDirectory,
    const InputActionMap& map,
    std::string* errorMessage) {
    const std::vector<std::string> issues = map.Validate();
    if (!issues.empty()) {
        SetError(errorMessage, issues.front());
        return false;
    }
    std::error_code ec{};
    std::filesystem::create_directories(inputDirectory, ec);
    if (ec) {
        SetError(errorMessage, "Could not create input settings directory: " + ec.message());
        return false;
    }

    json actionsRoot{
        { "version", map.version },
        { "actions", json::array() },
    };
    for (const InputActionDefinition& action : map.actions) {
        actionsRoot["actions"].push_back({
            { "id", action.actionId },
            { "displayName", action.displayName },
            { "valueType", ToString(action.valueType) },
            { "clamp", action.clampValue },
        });
    }
    std::ofstream actionsFile(inputDirectory / "actions.json");
    if (!actionsFile) {
        SetError(errorMessage, "Could not write input actions.");
        return false;
    }
    actionsFile << actionsRoot.dump(2) << '\n';

    std::unordered_set<std::string> expectedFiles;
    for (const InputContextDefinition& context : map.contexts) {
        const std::string fileName =
            ContextFileStem(context.contextId) + ".inputmap.json";
        expectedFiles.insert(fileName);
        json root{
            { "version", map.version },
            { "id", context.contextId },
            { "displayName", context.displayName },
            { "priority", context.priority },
            { "consumeInput", context.consumeInput },
            { "enabledByDefault", context.enabledByDefault },
            { "bindings", json::array() },
        };
        for (const InputBinding& binding : context.bindings) {
            root["bindings"].push_back(SerializeBinding(binding));
        }
        std::ofstream contextFile(inputDirectory / fileName);
        if (!contextFile) {
            SetError(errorMessage, "Could not write input context: " + context.contextId);
            return false;
        }
        contextFile << root.dump(2) << '\n';
    }

    for (const std::filesystem::directory_entry& entry :
            std::filesystem::directory_iterator(inputDirectory, ec)) {
        const std::string fileName = entry.path().filename().string();
        if (!entry.is_regular_file() ||
            entry.path().stem().extension() != ".inputmap" ||
            expectedFiles.contains(fileName)) {
            continue;
        }
        std::filesystem::remove(entry.path(), ec);
        if (ec) {
            SetError(errorMessage, "Could not remove stale input context: " + ec.message());
            return false;
        }
    }
    return true;
}

} // namespace HIKARI::INPUT
