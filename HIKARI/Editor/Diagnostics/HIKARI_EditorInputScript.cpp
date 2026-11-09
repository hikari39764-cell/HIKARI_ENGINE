#include "Editor/Diagnostics/HIKARI_EditorInputScript.h"

#include <fstream>
#include <string_view>

#include <json.hpp>

#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"
#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {
ImGuiKey FindKey(std::string_view name) {
    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key)
        if (name == ImGui::GetKeyName(static_cast<ImGuiKey>(key)))
            return static_cast<ImGuiKey>(key);
    return ImGuiKey_None;
}

// io.KeyShift などは修飾キーの事件から決まるので、左右の修飾キーには修飾の事件も添える（Win32 の後端と同じ）。
ImGuiKey ModifierOf(ImGuiKey key) {
    switch (key) {
    case ImGuiKey_LeftShift:
    case ImGuiKey_RightShift: return ImGuiMod_Shift;
    case ImGuiKey_LeftCtrl:
    case ImGuiKey_RightCtrl: return ImGuiMod_Ctrl;
    case ImGuiKey_LeftAlt:
    case ImGuiKey_RightAlt: return ImGuiMod_Alt;
    default: return ImGuiKey_None;
    }
}
} // namespace

std::optional<EditorInputScript> EditorInputScript::FromCommandLine() {
    const auto path = ReadRuntimeCommandLinePath(L"--editor-input-script");
    if (!path)
        return std::nullopt;
    std::ifstream file(*path);
    const nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.contains("events") || !root["events"].is_array()) {
        HIKARI_LOG_ERROR("[InputScript] cannot read " + TEXT::PathToUtf8(*path));
        return std::nullopt;
    }
    EditorInputScript script;
    for (const nlohmann::json& item : root["events"]) {
        Event event;
        event.frame = item.value("frame", uint64_t{0});
        if (item.contains("mouse"))
            event.mouse = item["mouse"].get<std::array<float, 2>>();
        if (item.contains("to")) {
            event.to = item["to"].get<std::array<float, 2>>();
            event.frames = item.value("frames", uint64_t{1});
        }
        const std::string button = item.value("button", std::string{});
        event.button = button == "left"     ? ImGuiMouseButton_Left
                       : button == "right"  ? ImGuiMouseButton_Right
                       : button == "middle" ? ImGuiMouseButton_Middle
                                            : -1;
        event.key = item.value("key", std::string{});
        event.wheel = item.value("wheel", 0.0f);
        event.text = item.value("text", std::string{});
        event.down = item.value("down", false);
        script.events_.push_back(std::move(event));
    }
    HIKARI_LOG_INFO("[InputScript] " + std::to_string(script.events_.size()) + " events from " +
                    TEXT::PathToUtf8(*path));
    return script;
}

void EditorInputScript::Apply(uint64_t frame) {
    ImGuiIO& io = ImGui::GetIO();
    for (const Event& event : events_) {
        if (event.mouse && frame >= event.frame && frame <= event.frame + event.frames) {
            const float t =
                event.frames > 0 ? static_cast<float>(frame - event.frame) / static_cast<float>(event.frames) : 0.0f;
            const std::array<float, 2>& to = event.frames > 0 ? event.to : *event.mouse;
            io.AddMousePosEvent((*event.mouse)[0] + (to[0] - (*event.mouse)[0]) * t,
                                (*event.mouse)[1] + (to[1] - (*event.mouse)[1]) * t);
        }
        if (frame != event.frame)
            continue;
        if (event.button >= 0)
            io.AddMouseButtonEvent(event.button, event.down);
        if (event.wheel != 0.0f)
            io.AddMouseWheelEvent(0.0f, event.wheel);
        if (!event.text.empty())
            io.AddInputCharactersUTF8(event.text.c_str());
        if (!event.key.empty()) {
            const ImGuiKey key = FindKey(event.key);
            if (key == ImGuiKey_None)
                HIKARI_LOG_ERROR("[InputScript] unknown key " + event.key);
            else {
                if (const ImGuiKey modifier = ModifierOf(key); modifier != ImGuiKey_None)
                    io.AddKeyEvent(modifier, event.down);
                io.AddKeyEvent(key, event.down);
            }
        }
    }
}

} // namespace HIKARI::EDITOR
