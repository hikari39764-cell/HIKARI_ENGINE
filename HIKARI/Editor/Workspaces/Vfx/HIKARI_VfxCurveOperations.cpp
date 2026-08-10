#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSections.h"

#include <string>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSupport.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

namespace HIKARI::EDITOR {

    void RegisterVfxCurveOperations(
        EditorOperationRegistry& registry,
        VfxWorkspaceController& controller) {

        using VFXOPERATIONS::ContextData;
        using VFXOPERATIONS::Failure;
        using VFXOPERATIONS::Parameter;
        using VFXOPERATIONS::Success;

        registry.Register({
            "vfx.curve_key.add",
            "Add a key to an addressed VFX curve channel",
            "vfx",
            {
                Parameter("emitterId", EditorOperationParameterType::String,
                    true, "Stable emitter ID"),
                Parameter("moduleId", EditorOperationParameterType::String,
                    true, "Stable Position, Rotation, Scale, or Color module ID"),
                Parameter("propertyId", EditorOperationParameterType::String,
                    true, "Animated property ID: value or eulerDegrees"),
                Parameter("channel", EditorOperationParameterType::String,
                    true, "Curve channel: x, y, z, r, g, b, or a"),
            },
            [&controller](const nlohmann::json& parameters) {
                std::string id{};
                std::string message{};
                if (!controller.AddCurveKey(
                        parameters.at("emitterId").get<std::string>(),
                        parameters.at("moduleId").get<std::string>(),
                        parameters.at("propertyId").get<std::string>(),
                        parameters.at("channel").get<std::string>(),
                        id,
                        message)) {
                    return Failure("VFX_CURVE_KEY_EDIT_FAILED", message);
                }
                return Success(message, ContextData(controller, id));
            },
        });

        registry.Register({
            "vfx.curve_key.remove",
            "Remove a key from an addressed VFX curve channel",
            "vfx",
            {
                Parameter("emitterId", EditorOperationParameterType::String,
                    true, "Stable emitter ID"),
                Parameter("moduleId", EditorOperationParameterType::String,
                    true, "Stable curve-owning module ID"),
                Parameter("propertyId", EditorOperationParameterType::String,
                    true, "Animated property ID"),
                Parameter("channel", EditorOperationParameterType::String,
                    true, "Curve channel"),
                Parameter("keyId", EditorOperationParameterType::String,
                    true, "Stable curve key ID"),
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                if (!controller.RemoveCurveKey(
                        parameters.at("emitterId").get<std::string>(),
                        parameters.at("moduleId").get<std::string>(),
                        parameters.at("propertyId").get<std::string>(),
                        parameters.at("channel").get<std::string>(),
                        parameters.at("keyId").get<std::string>(),
                        message)) {
                    return Failure("VFX_CURVE_KEY_EDIT_FAILED", message);
                }
                return Success(message, ContextData(controller));
            },
        });

        registry.Register({
            "vfx.color_stop.add",
            "Add a stop to an addressed VFX color gradient",
            "vfx",
            {
                Parameter("emitterId", EditorOperationParameterType::String,
                    true, "Stable emitter ID"),
                Parameter("moduleId", EditorOperationParameterType::String,
                    true, "Stable Color module ID"),
                Parameter("propertyId", EditorOperationParameterType::String,
                    true, "Animated color property ID"),
            },
            [&controller](const nlohmann::json& parameters) {
                std::string id{};
                std::string message{};
                if (!controller.AddColorStop(
                        parameters.at("emitterId").get<std::string>(),
                        parameters.at("moduleId").get<std::string>(),
                        parameters.at("propertyId").get<std::string>(),
                        id,
                        message)) {
                    return Failure("VFX_COLOR_STOP_EDIT_FAILED", message);
                }
                return Success(message, ContextData(controller, id));
            },
        });

        registry.Register({
            "vfx.color_stop.remove",
            "Remove a stop from an addressed VFX color gradient",
            "vfx",
            {
                Parameter("emitterId", EditorOperationParameterType::String,
                    true, "Stable emitter ID"),
                Parameter("moduleId", EditorOperationParameterType::String,
                    true, "Stable Color module ID"),
                Parameter("propertyId", EditorOperationParameterType::String,
                    true, "Animated color property ID"),
                Parameter("stopId", EditorOperationParameterType::String,
                    true, "Stable gradient stop ID"),
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                if (!controller.RemoveColorStop(
                        parameters.at("emitterId").get<std::string>(),
                        parameters.at("moduleId").get<std::string>(),
                        parameters.at("propertyId").get<std::string>(),
                        parameters.at("stopId").get<std::string>(),
                        message)) {
                    return Failure("VFX_COLOR_STOP_EDIT_FAILED", message);
                }
                return Success(message, ContextData(controller));
            },
        });
    }

} // namespace HIKARI::EDITOR
