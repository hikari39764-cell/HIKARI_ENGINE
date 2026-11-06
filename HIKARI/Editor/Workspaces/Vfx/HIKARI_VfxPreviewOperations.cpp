#include "Editor/Preview/HIKARI_EditorPreviewCapture.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSections.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Views/HIKARI_EditorViewInstance.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxEditorValue.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSupport.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"

namespace HIKARI::EDITOR {

void RegisterVfxPreviewOperations(EditorOperationRegistry& registry, VfxWorkspaceController& controller) {
    using VFXOPERATIONS::ContextData;

    // 引数なしの再生制御。message が空のまま成功した操作は既定文言を返す。
    using PreviewControl = bool (*)(VfxWorkspaceController&, std::string&);
    struct PreviewControlOperation {
        const char* id;
        const char* summary;
        const char* doneMessage;
        PreviewControl run;
    };
    static constexpr PreviewControlOperation kPreviewControls[] = {
        {"vfx.preview.play", "Play the VFX preview", "",
         [](VfxWorkspaceController& c, std::string& m) { return c.Preview().PlayPreview(m); }},
        {"vfx.preview.pause", "Pause the VFX preview", "VFX preview paused",
         [](VfxWorkspaceController& c, std::string&) { return c.Preview().PausePreview(true), true; }},
        {"vfx.preview.resume", "Resume the VFX preview", "VFX preview resumed",
         [](VfxWorkspaceController& c, std::string&) { return c.Preview().PausePreview(false), true; }},
        {"vfx.preview.stop", "Stop the VFX preview", "VFX preview stopped",
         [](VfxWorkspaceController& c, std::string&) { return c.Preview().StopPreview(), true; }},
        {"vfx.preview.restart", "Restart the VFX preview", "",
         [](VfxWorkspaceController& c, std::string& m) { return c.Preview().RestartPreview(m); }},
        {"vfx.preview.reset_inputs", "Reset all session-only VFX preview inputs", "VFX preview inputs reset",
         [](VfxWorkspaceController& c, std::string&) { return c.Preview().ResetPreviewInputs(), true; }},
        {"vfx.preview.focus_selected", "Focus the preview camera on the selected emitter", "",
         [](VfxWorkspaceController& c, std::string& m) { return c.FocusSelectedEmitter(m); }},
    };
    for (const PreviewControlOperation& operation : kPreviewControls) {
        registry.Register(
            {
                operation.id,
                operation.summary,
                "vfx",
                {},
                [&controller, operation](const nlohmann::json&) {
                    std::string message = operation.doneMessage;
                    return operation.run(controller, message) ? OperationOk(message, ContextData(controller))
                                                              : OperationFailure("VFX_PREVIEW_FAILED", message);
                },
            },
            EditorOperationKind::Session);
    }

    registry.Register(
        {
            "vfx.preview.set_speed",
            "Set the session-only VFX preview playback speed",
            "vfx",
            {
                {"speed", EditorOperationParameterType::Float, true, "Playback speed from 0.1x to 4.0x"},
            },
            [&controller](const nlohmann::json& parameters) {
                controller.Preview().SetPreviewPlaybackSpeed(parameters.at("speed").get<float>());
                nlohmann::json data = ContextData(controller);
                data["speed"] = controller.Preview().GetPreviewPlaybackSpeed();
                return OperationOk("VFX preview playback speed updated", std::move(data));
            },
        },
        EditorOperationKind::Session);

    registry.Register(
        {
            "vfx.preview.step",
            "Pause the preview and advance a fixed number of frames",
            "vfx",
            {
                {"frameCount", EditorOperationParameterType::Int, false, "Frames to advance (1-120, default 1)"},
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                return controller.Preview().StepPreviewFrame(parameters.value("frameCount", 1), message)
                           ? OperationOk(message, ContextData(controller))
                           : OperationFailure("VFX_STEP_FAILED", message);
            },
        },
        EditorOperationKind::Session);

    registry.Register(
        {
            "vfx.preview.seek",
            "Seek the session-only VFX preview playhead",
            "vfx",
            {
                {"seconds", EditorOperationParameterType::Float, true, "Preview time in seconds"},
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                if (!controller.Preview().SeekPreview(parameters.at("seconds").get<float>(), message)) {
                    return OperationFailure("VFX_PREVIEW_SEEK_FAILED", message);
                }
                nlohmann::json data = ContextData(controller);
                data["seconds"] = controller.Preview().GetPreviewElapsedSeconds();
                return OperationOk("VFX preview playhead moved", std::move(data));
            },
        },
        EditorOperationKind::Session);

    registry.Register(
        {
            "vfx.preview.set_loop",
            "Enable or disable session-only VFX preview looping",
            "vfx",
            {
                {"enabled", EditorOperationParameterType::Bool, true, "Whether preview looping is enabled"},
            },
            [&controller](const nlohmann::json& parameters) {
                controller.Preview().SetPreviewLoopEnabled(parameters.at("enabled").get<bool>());
                nlohmann::json data = ContextData(controller);
                data["enabled"] = controller.Preview().IsPreviewLoopEnabled();
                return OperationOk("VFX preview loop updated", std::move(data));
            },
        },
        EditorOperationKind::Session);

    registry.Register(
        {
            "vfx.preview.set_loop_range",
            "Set the session-only VFX preview loop range",
            "vfx",
            {
                {"startSeconds", EditorOperationParameterType::Float, true, "Loop start time in seconds"},
                {"endSeconds", EditorOperationParameterType::Float, true, "Loop end time in seconds"},
            },
            [&controller](const nlohmann::json& parameters) {
                controller.Preview().SetPreviewLoopRange(parameters.at("startSeconds").get<float>(),
                                                         parameters.at("endSeconds").get<float>());
                nlohmann::json data = ContextData(controller);
                data["startSeconds"] = controller.Preview().GetPreviewLoopStartSeconds();
                data["endSeconds"] = controller.Preview().GetPreviewLoopEndSeconds();
                return OperationOk("VFX preview loop range updated", std::move(data));
            },
        },
        EditorOperationKind::Session);

    registry.Register(
        {
            "vfx.preview.capture",
            "Capture the VFX preview render view to PNG files so the result "
            "can be inspected instead of assumed",
            "vfx",
            {
                {"path", EditorOperationParameterType::String, false,
                 "Output PNG path; defaults to the temp directory"},
                {"frameCount", EditorOperationParameterType::Int, false,
                 "Single frame; use authoring.preview with time for sequences"},
                {"intervalSeconds", EditorOperationParameterType::Float, false, "Seconds between captures (0..2)"},
            },
            [&controller](const nlohmann::json& parameters) {
                if (parameters.value("frameCount", 1) != 1)
                    return OperationFailure("USE_TIMED_PREVIEWS",
                                            "Use authoring.preview at explicit time values to capture a sequence.");
                return StartEditorPreviewCapture(controller.Preview().GetActivePreviewViewId(), {},
                                                 parameters.value("path", std::string{}));
            },
        },
        EditorOperationKind::Session, true, HIKARI::EDITOR::EditorOperationSessionDispatch::Preview);

    registry.Register(
        {
            "vfx.fluid.stats",
            "Read the fluid smoke simulation of the VFX preview: grid, GPU memory, substeps, occupied bricks, "
            "divergence and density total (diagnostics arrive 1-3 frames after the first read) and GPU time",
            "vfx",
            {},
            [&controller](const nlohmann::json&) {
                MESHRENDERER::RequestFluidDiagnostics();
                return OperationOk("VFX fluid simulation statistics", controller.DescribePreviewFluidStats());
            },
        },
        EditorOperationKind::Query);

    registry.Register(
        {
            "vfx.fluid.debug_slice",
            "Select a fluid domain layer and show or hide its debug slice in the VFX inspector",
            "vfx",
            {
                {"layerId", EditorOperationParameterType::String, true, "Stable fluid domain layer ID"},
                {"show", EditorOperationParameterType::Bool, false, "Show the slice (default true)"},
                {"field", EditorOperationParameterType::String, false, "Density, Velocity, Divergence or Pressure"},
                {"axis", EditorOperationParameterType::Int, false, "0 X, 1 Y, 2 Z (default 2)"},
                {"position", EditorOperationParameterType::Float, false, "Slice position along the axis, 0..1"},
            },
            [&controller](const nlohmann::json& parameters) {
                constexpr std::array<std::string_view, 4> kFields{"Density", "Velocity", "Divergence", "Pressure"};
                const std::string field = parameters.value("field", "Density");
                const auto found = std::find(kFields.begin(), kFields.end(), field);
                if (found == kFields.end())
                    return OperationFailure("VFX_INVALID_FLUID_FIELD",
                                            "Expected Density, Velocity, Divergence or Pressure");
                std::string message{};
                if (!controller.ShowFluidDebugSlice(parameters.at("layerId").get<std::string>(),
                                                    parameters.value("show", true),
                                                    static_cast<uint32_t>(found - kFields.begin()),
                                                    static_cast<uint32_t>(parameters.value("axis", 2)),
                                                    parameters.value("position", 0.5f), message))
                    return OperationFailure("VFX_FLUID_SLICE_FAILED", message);
                return OperationOk(message, ContextData(controller));
            },
        },
        EditorOperationKind::Session);

    registry.Register(
        {
            "vfx.preview.set_input",
            "Set a session-only exposed input value for VFX preview",
            "vfx",
            {
                {"inputId", EditorOperationParameterType::String, true, "Stable exposed input ID"},
                {"value", EditorOperationParameterType::Json, true, "Typed preview value"},
            },
            [&controller](const nlohmann::json& parameters) {
                const std::string inputId = parameters.at("inputId").get<std::string>();
                const VFX::VfxExposedInputData* input = controller.FindInput(inputId);
                if (input == nullptr) {
                    return OperationFailure("VFX_INPUT_NOT_FOUND", "VFX exposed input was not found");
                }
                VFX::VfxValue value{};
                std::string message{};
                if (!TryReadVfxEditorValue(parameters.at("value"), input->type, value, message) ||
                    !controller.Preview().SetPreviewInput(inputId, value, message)) {
                    return OperationFailure("VFX_PREVIEW_INPUT_REJECTED", message);
                }
                nlohmann::json data = ContextData(controller, inputId);
                data["value"] = WriteVfxEditorValue(value);
                return OperationOk(message, std::move(data));
            },
        },
        EditorOperationKind::Session);
}

} // namespace HIKARI::EDITOR
