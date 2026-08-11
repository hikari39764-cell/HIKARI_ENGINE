#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSections.h"

#include <string>
#include <utility>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxEditorValue.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSupport.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

namespace HIKARI::EDITOR {

    void RegisterVfxPreviewOperations(
        EditorOperationRegistry& registry,
        VfxWorkspaceController& controller) {

        using VFXOPERATIONS::ContextData;
        using VFXOPERATIONS::Failure;
        using VFXOPERATIONS::Parameter;
        using VFXOPERATIONS::Success;

        const auto registerPreviewOperation =
            [&registry, &controller](
                const char* id,
                const char* summary,
                int action) {
                registry.Register({
                    id,
                    summary,
                    "vfx",
                    {},
                    [&controller, action](const nlohmann::json&) {
                        std::string message{};
                        bool accepted = true;
                        switch (action) {
                        case 0:
                            accepted = controller.PlayPreview(message);
                            break;
                        case 1:
                            controller.PausePreview(true);
                            message = "VFX preview paused";
                            break;
                        case 2:
                            controller.PausePreview(false);
                            message = "VFX preview resumed";
                            break;
                        case 3:
                            controller.StopPreview();
                            message = "VFX preview stopped";
                            break;
                        case 4:
                            accepted = controller.RestartPreview(message);
                            break;
                        case 5:
                            controller.ResetPreviewInputs();
                            message = "VFX preview inputs reset";
                            break;
                        default:
                            accepted = false;
                            message = "Unknown VFX preview action";
                            break;
                        }
                        return accepted
                            ? Success(message, ContextData(controller))
                            : Failure("VFX_PREVIEW_FAILED", message);
                    },
                });
            };
        registerPreviewOperation(
            "vfx.preview.play", "Play the VFX preview", 0);
        registerPreviewOperation(
            "vfx.preview.pause", "Pause the VFX preview", 1);
        registerPreviewOperation(
            "vfx.preview.resume", "Resume the VFX preview", 2);
        registerPreviewOperation(
            "vfx.preview.stop", "Stop the VFX preview", 3);
        registerPreviewOperation(
            "vfx.preview.restart", "Restart the VFX preview", 4);
        registerPreviewOperation(
            "vfx.preview.reset_inputs",
            "Reset all session-only VFX preview inputs",
            5);

        registry.Register({
            "vfx.preview.set_speed",
            "Set the session-only VFX preview playback speed",
            "vfx",
            {
                Parameter(
                    "speed",
                    EditorOperationParameterType::Float,
                    true,
                    "Playback speed from 0.1x to 4.0x"),
            },
            [&controller](const nlohmann::json& parameters) {
                controller.SetPreviewPlaybackSpeed(
                    parameters.at("speed").get<float>());
                nlohmann::json data = ContextData(controller);
                data["speed"] = controller.GetPreviewPlaybackSpeed();
                return Success(
                    "VFX preview playback speed updated",
                    std::move(data));
            },
        });

        registry.Register({
            "vfx.preview.seek",
            "Seek the session-only VFX preview playhead",
            "vfx",
            {
                Parameter(
                    "seconds",
                    EditorOperationParameterType::Float,
                    true,
                    "Preview time in seconds"),
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                if (!controller.SeekPreview(
                        parameters.at("seconds").get<float>(),
                        message)) {
                    return Failure("VFX_PREVIEW_SEEK_FAILED", message);
                }
                nlohmann::json data = ContextData(controller);
                data["seconds"] = controller.GetPreviewElapsedSeconds();
                return Success("VFX preview playhead moved", std::move(data));
            },
        });

        registry.Register({
            "vfx.preview.set_loop",
            "Enable or disable session-only VFX preview looping",
            "vfx",
            {
                Parameter(
                    "enabled",
                    EditorOperationParameterType::Bool,
                    true,
                    "Whether preview looping is enabled"),
            },
            [&controller](const nlohmann::json& parameters) {
                controller.SetPreviewLoopEnabled(
                    parameters.at("enabled").get<bool>());
                nlohmann::json data = ContextData(controller);
                data["enabled"] = controller.IsPreviewLoopEnabled();
                return Success("VFX preview loop updated", std::move(data));
            },
        });

        registry.Register({
            "vfx.preview.set_loop_range",
            "Set the session-only VFX preview loop range",
            "vfx",
            {
                Parameter(
                    "startSeconds",
                    EditorOperationParameterType::Float,
                    true,
                    "Loop start time in seconds"),
                Parameter(
                    "endSeconds",
                    EditorOperationParameterType::Float,
                    true,
                    "Loop end time in seconds"),
            },
            [&controller](const nlohmann::json& parameters) {
                controller.SetPreviewLoopRange(
                    parameters.at("startSeconds").get<float>(),
                    parameters.at("endSeconds").get<float>());
                nlohmann::json data = ContextData(controller);
                data["startSeconds"] =
                    controller.GetPreviewLoopStartSeconds();
                data["endSeconds"] =
                    controller.GetPreviewLoopEndSeconds();
                return Success(
                    "VFX preview loop range updated", std::move(data));
            },
        });

        registry.Register({
            "vfx.preview.set_input",
            "Set a session-only exposed input value for VFX preview",
            "vfx",
            {
                Parameter(
                    "inputId",
                    EditorOperationParameterType::String,
                    true,
                    "Stable exposed input ID"),
                Parameter(
                    "value",
                    EditorOperationParameterType::Json,
                    true,
                    "Typed preview value"),
            },
            [&controller](const nlohmann::json& parameters) {
                const std::string inputId =
                    parameters.at("inputId").get<std::string>();
                const VFX::VfxExposedInputData* input =
                    controller.FindInput(inputId);
                if (input == nullptr) {
                    return Failure(
                        "VFX_INPUT_NOT_FOUND",
                        "VFX exposed input was not found");
                }
                VFX::VfxValue value{};
                std::string message{};
                if (!TryReadVfxEditorValue(
                        parameters.at("value"),
                        input->type,
                        value,
                        message) ||
                    !controller.SetPreviewInput(
                        inputId, value, message)) {
                    return Failure(
                        "VFX_PREVIEW_INPUT_REJECTED", message);
                }
                nlohmann::json data = ContextData(controller, inputId);
                data["value"] = WriteVfxEditorValue(value);
                return Success(message, std::move(data));
            },
        });
    }

} // namespace HIKARI::EDITOR
