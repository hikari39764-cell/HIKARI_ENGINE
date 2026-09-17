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

        registry.Register({"vfx.layer.time.edit","Move, trim or retime a layer without mixing placement and animation phase","vfx",
            {Parameter("layerId",EditorOperationParameterType::String,true,"Layer ID"),
             Parameter("mode",EditorOperationParameterType::String,true,"Move, TrimStart, TrimEnd, Retime"),
             Parameter("deltaSeconds",EditorOperationParameterType::Float,true,"Time change in seconds")},
            [&controller](const nlohmann::json& p){
                const std::string mode=p.at("mode").get<std::string>();std::string message;
                if(mode!="Move" && mode!="TrimStart" && mode!="TrimEnd" && mode!="Retime")return Failure("VFX_TIME_MODE","Unknown time edit mode");
                const auto operation=mode=="Move"?VfxLayerTimeEdit::Move:mode=="TrimStart"?VfxLayerTimeEdit::TrimStart:mode=="TrimEnd"?VfxLayerTimeEdit::TrimEnd:VfxLayerTimeEdit::Retime;
                return controller.EditLayerTiming(p.at("layerId").get<std::string>(),operation,p.at("deltaSeconds").get<float>(),message)
                    ?Success(message,ContextData(controller)):Failure("VFX_TIME_EDIT_REJECTED",message);
            }});
        registry.Register({"vfx.keys.move","Move a key selection atomically in composition seconds; preserves spacing","vfx",
            {Parameter("keyIds",EditorOperationParameterType::Json,true,"Array of stable key IDs"),Parameter("deltaSeconds",EditorOperationParameterType::Float,true,"Time offset")},
            [&controller](const nlohmann::json& p){
                const auto& values=p.at("keyIds");std::vector<std::string> ids;std::string message;
                if(!values.is_array())return Failure("VFX_KEY_SELECTION","Expected key ID array");
                for(const auto& id:values){if(!id.is_string())return Failure("VFX_KEY_SELECTION","Expected string IDs");ids.push_back(id.get<std::string>());}
                return controller.MoveLayerKeys(ids,p.at("deltaSeconds").get<float>(),message)?Success(message,ContextData(controller)):Failure("VFX_KEY_MOVE_REJECTED",message);
            }});
        registry.Register({"vfx.layer.parameter.key.set", "Animate a layer render/effect scalar at composition time; updates existing keys atomically", "vfx",
            {Parameter("layerId",EditorOperationParameterType::String,true,"Layer ID"),
             Parameter("targetId",EditorOperationParameterType::String,true,"Effect or RenderShape module ID"),
             Parameter("propertyId",EditorOperationParameterType::String,true,"progress, parameters.x/y, ringInnerRadius/ringOuterRadius/ringStartAngleDegrees/ringEndAngleDegrees/ribbonWidth"),
             Parameter("seconds",EditorOperationParameterType::Float,true,"Composition time, seconds"),
             Parameter("value",EditorOperationParameterType::Float,true,"Finite scalar")},
            [&controller](const nlohmann::json& p) {
                std::string message;
                return controller.SetLayerParameterKey(p.at("layerId").get<std::string>(),p.at("targetId").get<std::string>(),
                    p.at("propertyId").get<std::string>(),p.at("seconds").get<float>(),p.at("value").get<float>(),message)
                    ? Success(message,ContextData(controller)) : Failure("VFX_PARAMETER_KEY_REJECTED",message);
            }});
        registry.Register({"vfx.layer.color.key.set", "Animate layer RGBA at composition time; alpha controls opacity", "vfx",
            {Parameter("layerId",EditorOperationParameterType::String,true,"Shape/group ID"),
             Parameter("seconds",EditorOperationParameterType::Float,true,"Composition time"),
             Parameter("value",EditorOperationParameterType::Json,true,"RGBA [r,g,b,a]")},
            [&controller](const nlohmann::json& p) {
                const auto& v=p.at("value");std::string message;
                if (!v.is_array() || v.size()!=4 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number() || !v[3].is_number()) return Failure("VFX_INVALID_KEY","Expected RGBA");
                return controller.SetLayerColorKey(p.at("layerId").get<std::string>(),p.at("seconds").get<float>(),
                    {v[0].get<float>(),v[1].get<float>(),v[2].get<float>(),v[3].get<float>()},message)
                    ? Success(message,ContextData(controller)) : Failure("VFX_COLOR_KEY_REJECTED",message);
            }});
        registry.Register({"vfx.layer.parameter.track.remove", "Remove parameter animation and restore its static setting", "vfx",
            {Parameter("layerId",EditorOperationParameterType::String,true,"Layer ID"),Parameter("trackId",EditorOperationParameterType::String,true,"Parameter track ID")},
            [&controller](const nlohmann::json& p) {std::string message;
                return controller.RemoveLayerParameterTrack(p.at("layerId").get<std::string>(),p.at("trackId").get<std::string>(),message)
                    ? Success(message,ContextData(controller)) : Failure("VFX_TRACK_REJECTED",message);}});

        registry.Register({"vfx.effect.add", "Append a reusable material effect to a layer", "vfx",
            {Parameter("layerId", EditorOperationParameterType::String, true, "Stable layer ID"),
             Parameter("kind", EditorOperationParameterType::String, true,
                "UvFlow, UvDistortion, Gradient, Fade, Wipe, Dissolve, Emission, Fresnel")},
            [&controller](const nlohmann::json& p) {
                MaterialEffectKind kind; std::string message;
                if (!ParseMaterialEffectKind(p.at("kind").get<std::string>(), kind))
                    return Failure("VFX_UNKNOWN_EFFECT", "Unknown effect kind");
                return controller.AddLayerEffect(p.at("layerId").get<std::string>(), kind, message)
                    ? Success(message, ContextData(controller)) : Failure("VFX_EFFECT_REJECTED", message);
            }});
        registry.Register({"vfx.effect.remove", "Remove a layer material effect", "vfx",
            {Parameter("layerId", EditorOperationParameterType::String, true, "Stable layer ID"),
             Parameter("effectId", EditorOperationParameterType::String, true, "Stable effect ID")},
            [&controller](const nlohmann::json& p) {
                std::string message;
                return controller.RemoveLayerEffect(p.at("layerId").get<std::string>(), p.at("effectId").get<std::string>(), message)
                    ? Success(message, ContextData(controller)) : Failure("VFX_EFFECT_REJECTED", message);
            }});
        registry.Register({"vfx.effect.move", "Move an effect within its layer stack", "vfx",
            {Parameter("layerId", EditorOperationParameterType::String, true, "Stable layer ID"),
             Parameter("effectId", EditorOperationParameterType::String, true, "Stable effect ID"),
             Parameter("direction", EditorOperationParameterType::Int, true, "-1 moves up; 1 moves down")},
            [&controller](const nlohmann::json& p) {
                std::string message;
                return controller.MoveLayerEffect(p.at("layerId").get<std::string>(), p.at("effectId").get<std::string>(), p.at("direction").get<int>(), message)
                    ? Success(message, ContextData(controller)) : Failure("VFX_EFFECT_REJECTED", message);
            }});

        registry.Register({
            "vfx.layer.key.set", "Set a shape/group transform key at composition time (seconds)", "vfx",
            { Parameter("layerId", EditorOperationParameterType::String, true, "Stable layer ID"),
              Parameter("property", EditorOperationParameterType::String, true, "position, rotation (degrees), scale"),
              Parameter("seconds", EditorOperationParameterType::Float, true, "Composition time"),
              Parameter("value", EditorOperationParameterType::Json, true, "Three finite numbers [x,y,z]") },
            [&controller](const nlohmann::json& p) {
                const auto& v = p.at("value");
                if (!v.is_array() || v.size() != 3 || !v[0].is_number() ||
                    !v[1].is_number() || !v[2].is_number())
                    return Failure("VFX_INVALID_KEY_VALUE", "Expected [x,y,z]");
                std::string message;
                if (!controller.SetLayerTransformKey(p.at("layerId").get<std::string>(),
                        p.at("property").get<std::string>(), p.at("seconds").get<float>(),
                        {v[0].get<float>(), v[1].get<float>(), v[2].get<float>()}, message))
                    return Failure("VFX_KEY_REJECTED", message);
                return Success(message, ContextData(controller));
            }
        });

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
