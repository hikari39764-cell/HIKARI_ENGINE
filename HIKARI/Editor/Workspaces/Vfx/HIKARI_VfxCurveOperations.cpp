#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSections.h"

#include <string>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSupport.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

namespace HIKARI::EDITOR {

void RegisterVfxCurveOperations(EditorOperationRegistry& registry, VfxWorkspaceController& controller) {
    using VFXOPERATIONS::ContextData;

    registry.Register(
        {"vfx.layer.path.set",
         "Set a cubic Bezier motion path using existing XYZ animation curves",
         "vfx",
         {{"layerId", EditorOperationParameterType::String, true, "Shape/group ID"},
          {"knots", EditorOperationParameterType::Json, true,
           "Ordered knots: seconds (composition time), position [x,y,z], optional incoming/outgoing control points [x,y,z]"}},
         [&controller](const nlohmann::json& p) {
             const auto& input = p.at("knots");
             if (!input.is_array() || input.size() < 2 || input.size() > VFX::kMaxVfxLayerCurveKeys)
                 return OperationFailure("VFX_PATH_INVALID", "Expected 2..256 knots");
             const auto read = [](const nlohmann::json& value, MATH::Vec3& result) {
                 if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() ||
                     !value[2].is_number())
                     return false;
                 result = {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
                 return true;
             };
             std::vector<VfxPathKnot> knots;
             for (const auto& item : input) {
                 VfxPathKnot knot;
                 if (!item.is_object() || !item.contains("seconds") || !item["seconds"].is_number() ||
                     !item.contains("position") || !read(item["position"], knot.position))
                     return OperationFailure("VFX_PATH_INVALID", "Invalid knot position/time");
                 knot.seconds = item["seconds"].get<float>();
                 knot.incoming = knot.outgoing = knot.position;
                 if ((item.contains("incoming") && !read(item["incoming"], knot.incoming)) ||
                     (item.contains("outgoing") && !read(item["outgoing"], knot.outgoing)))
                     return OperationFailure("VFX_PATH_INVALID", "Invalid path control point");
                 knots.push_back(knot);
             }
             std::string message;
             return controller.SetLayerBezierPath(p.at("layerId").get<std::string>(), knots, message)
                        ? OperationOk("Bezier path updated", ContextData(controller))
                        : OperationFailure("VFX_PATH_REJECTED", message);
         }},
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register(
        {"vfx.layer.time.edit",
         "Move, trim or retime in composition seconds. Group move/retime includes descendants; trimming clips visibility",
         "vfx",
         {{"layerId", EditorOperationParameterType::String, true, "Layer ID"},
          {"mode", EditorOperationParameterType::String, true, "Move, TrimStart, TrimEnd, Retime"},
          {"deltaSeconds", EditorOperationParameterType::Float, true, "Time change in seconds"}},
         [&controller](const nlohmann::json& p) {
             const std::string mode = p.at("mode").get<std::string>();
             std::string message;
             if (mode != "Move" && mode != "TrimStart" && mode != "TrimEnd" && mode != "Retime")
                 return OperationFailure("VFX_TIME_MODE", "Unknown time edit mode");
             const auto operation = mode == "Move"        ? VfxLayerTimeEdit::Move
                                    : mode == "TrimStart" ? VfxLayerTimeEdit::TrimStart
                                    : mode == "TrimEnd"   ? VfxLayerTimeEdit::TrimEnd
                                                          : VfxLayerTimeEdit::Retime;
             return controller.EditLayerTiming(p.at("layerId").get<std::string>(), operation,
                                               p.at("deltaSeconds").get<float>(), message)
                        ? OperationOk(message, ContextData(controller))
                        : OperationFailure("VFX_TIME_EDIT_REJECTED", message);
         }},
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register({"vfx.keys.move",
                       "Move a key selection atomically in composition seconds; preserves spacing",
                       "vfx",
                       {{"keyIds", EditorOperationParameterType::Json, true, "Array of stable key IDs"},
                        {"deltaSeconds", EditorOperationParameterType::Float, true, "Time offset"}},
                       [&controller](const nlohmann::json& p) {
                           const auto& values = p.at("keyIds");
                           std::vector<std::string> ids;
                           std::string message;
                           if (!values.is_array())
                               return OperationFailure("VFX_KEY_SELECTION", "Expected key ID array");
                           for (const auto& id : values) {
                               if (!id.is_string())
                                   return OperationFailure("VFX_KEY_SELECTION", "Expected string IDs");
                               ids.push_back(id.get<std::string>());
                           }
                           return controller.MoveLayerKeys(ids, p.at("deltaSeconds").get<float>(), message)
                                      ? OperationOk(message, ContextData(controller))
                                      : OperationFailure("VFX_KEY_MOVE_REJECTED", message);
                       }},
                      HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register(
        {"vfx.layer.parameter.key.set",
         "Animate a layer render/effect scalar at composition time; updates existing keys atomically",
         "vfx",
         {{"layerId", EditorOperationParameterType::String, true, "Layer ID"},
          {"targetId", EditorOperationParameterType::String, true, "Effect or RenderShape module ID"},
          {"propertyId", EditorOperationParameterType::String, true,
           "progress, parameters.x/y, ringInnerRadius/ringOuterRadius/ringStartAngleDegrees/ringEndAngleDegrees/ribbonWidth"},
          {"seconds", EditorOperationParameterType::Float, true, "Composition time, seconds"},
          {"value", EditorOperationParameterType::Float, true, "Finite scalar"}},
         [&controller](const nlohmann::json& p) {
             std::string message;
             return controller.SetLayerParameterKey(p.at("layerId").get<std::string>(),
                                                    p.at("targetId").get<std::string>(),
                                                    p.at("propertyId").get<std::string>(), p.at("seconds").get<float>(),
                                                    p.at("value").get<float>(), message)
                        ? OperationOk(message, ContextData(controller))
                        : OperationFailure("VFX_PARAMETER_KEY_REJECTED", message);
         }},
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register({"vfx.layer.color.key.set",
                       "Animate layer RGBA at composition time; alpha controls opacity",
                       "vfx",
                       {{"layerId", EditorOperationParameterType::String, true, "Shape/group ID"},
                        {"seconds", EditorOperationParameterType::Float, true, "Composition time"},
                        {"value", EditorOperationParameterType::Json, true, "RGBA [r,g,b,a]"}},
                       [&controller](const nlohmann::json& p) {
                           const auto& v = p.at("value");
                           std::string message;
                           if (!v.is_array() || v.size() != 4 || !v[0].is_number() || !v[1].is_number() ||
                               !v[2].is_number() || !v[3].is_number())
                               return OperationFailure("VFX_INVALID_KEY", "Expected RGBA");
                           return controller.SetLayerColorKey(
                                      p.at("layerId").get<std::string>(), p.at("seconds").get<float>(),
                                      {v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), v[3].get<float>()},
                                      message)
                                      ? OperationOk(message, ContextData(controller))
                                      : OperationFailure("VFX_COLOR_KEY_REJECTED", message);
                       }},
                      HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register({"vfx.layer.parameter.track.remove",
                       "Remove parameter animation and restore its static setting",
                       "vfx",
                       {{"layerId", EditorOperationParameterType::String, true, "Layer ID"},
                        {"trackId", EditorOperationParameterType::String, true, "Parameter track ID"}},
                       [&controller](const nlohmann::json& p) {
                           std::string message;
                           return controller.RemoveLayerParameterTrack(p.at("layerId").get<std::string>(),
                                                                       p.at("trackId").get<std::string>(), message)
                                      ? OperationOk(message, ContextData(controller))
                                      : OperationFailure("VFX_TRACK_REJECTED", message);
                       }},
                      HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register({"vfx.effect.add",
                       "Append a reusable material effect to a layer",
                       "vfx",
                       {{"layerId", EditorOperationParameterType::String, true, "Stable layer ID"},
                        {"kind", EditorOperationParameterType::String, true, MaterialEffectKindList()}},
                       [&controller](const nlohmann::json& p) {
                           MaterialEffectKind kind;
                           std::string message;
                           if (!ParseMaterialEffectKind(p.at("kind").get<std::string>(), kind))
                               return OperationFailure("VFX_UNKNOWN_EFFECT", "Unknown effect kind");
                           return controller.AddLayerEffect(p.at("layerId").get<std::string>(), kind, message)
                                      ? OperationOk(message, ContextData(controller))
                                      : OperationFailure("VFX_EFFECT_REJECTED", message);
                       }},
                      HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register({"vfx.effect.color_stop.add",
                       "Add a stop to a Gradient effect's multi-stop ramp",
                       "vfx",
                       {{"layerId", EditorOperationParameterType::String, true, "Stable layer ID"},
                        {"effectId", EditorOperationParameterType::String, true, "Stable Gradient effect ID"}},
                       [&controller](const nlohmann::json& p) {
                           std::string id;
                           std::string message;
                           return controller.AddLayerEffectColorStop(p.at("layerId").get<std::string>(),
                                                                     p.at("effectId").get<std::string>(), id, message)
                                      ? OperationOk(message, ContextData(controller, id))
                                      : OperationFailure("VFX_EFFECT_COLOR_STOP_REJECTED", message);
                       }},
                      HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register({"vfx.effect.color_stop.set",
                       "Set a Gradient effect stop's time and color",
                       "vfx",
                       {{"layerId", EditorOperationParameterType::String, true, "Stable layer ID"},
                        {"effectId", EditorOperationParameterType::String, true, "Stable Gradient effect ID"},
                        {"stopId", EditorOperationParameterType::String, true, "Stable gradient stop ID"},
                        {"time", EditorOperationParameterType::Float, true, "Normalized time 0..1"},
                        {"color", EditorOperationParameterType::Json, true, "Stop color as [r,g,b,a]"}},
                       [&controller](const nlohmann::json& p) {
                           std::string message;
                           return controller.SetLayerEffectColorStop(
                                      p.at("layerId").get<std::string>(), p.at("effectId").get<std::string>(),
                                      p.at("stopId").get<std::string>(), p.at("time").get<float>(),
                                      MATH::Vec4{p.at("color")[0].get<float>(), p.at("color")[1].get<float>(),
                                                 p.at("color")[2].get<float>(), p.at("color")[3].get<float>()},
                                      message)
                                      ? OperationOk(message, ContextData(controller))
                                      : OperationFailure("VFX_EFFECT_COLOR_STOP_REJECTED", message);
                       }},
                      HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register({"vfx.effect.color_stop.remove",
                       "Remove a stop from a Gradient effect's multi-stop ramp",
                       "vfx",
                       {{"layerId", EditorOperationParameterType::String, true, "Stable layer ID"},
                        {"effectId", EditorOperationParameterType::String, true, "Stable Gradient effect ID"},
                        {"stopId", EditorOperationParameterType::String, true, "Stable gradient stop ID"}},
                       [&controller](const nlohmann::json& p) {
                           std::string message;
                           return controller.RemoveLayerEffectColorStop(p.at("layerId").get<std::string>(),
                                                                        p.at("effectId").get<std::string>(),
                                                                        p.at("stopId").get<std::string>(), message)
                                      ? OperationOk(message, ContextData(controller))
                                      : OperationFailure("VFX_EFFECT_COLOR_STOP_REJECTED", message);
                       }},
                      HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register({"vfx.effect.remove",
                       "Remove a layer material effect",
                       "vfx",
                       {{"layerId", EditorOperationParameterType::String, true, "Stable layer ID"},
                        {"effectId", EditorOperationParameterType::String, true, "Stable effect ID"}},
                       [&controller](const nlohmann::json& p) {
                           std::string message;
                           return controller.RemoveLayerEffect(p.at("layerId").get<std::string>(),
                                                               p.at("effectId").get<std::string>(), message)
                                      ? OperationOk(message, ContextData(controller))
                                      : OperationFailure("VFX_EFFECT_REJECTED", message);
                       }},
                      HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register({"vfx.effect.move",
                       "Move an effect within its layer stack",
                       "vfx",
                       {{"layerId", EditorOperationParameterType::String, true, "Stable layer ID"},
                        {"effectId", EditorOperationParameterType::String, true, "Stable effect ID"},
                        {"direction", EditorOperationParameterType::Int, true, "-1 moves up; 1 moves down"}},
                       [&controller](const nlohmann::json& p) {
                           std::string message;
                           return controller.MoveLayerEffect(p.at("layerId").get<std::string>(),
                                                             p.at("effectId").get<std::string>(),
                                                             p.at("direction").get<int>(), message)
                                      ? OperationOk(message, ContextData(controller))
                                      : OperationFailure("VFX_EFFECT_REJECTED", message);
                       }},
                      HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {"vfx.layer.key.set",
         "Set a shape/group transform key at composition time (seconds)",
         "vfx",
         {{"layerId", EditorOperationParameterType::String, true, "Stable layer ID"},
          {"property", EditorOperationParameterType::String, true, "position, rotation (degrees), scale"},
          {"seconds", EditorOperationParameterType::Float, true, "Composition time"},
          {"value", EditorOperationParameterType::Json, true, "Three finite numbers [x,y,z]"}},
         [&controller](const nlohmann::json& p) {
             const auto& v = p.at("value");
             if (!v.is_array() || v.size() != 3 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number())
                 return OperationFailure("VFX_INVALID_KEY_VALUE", "Expected [x,y,z]");
             std::string message;
             if (!controller.SetLayerTransformKey(p.at("layerId").get<std::string>(),
                                                  p.at("property").get<std::string>(), p.at("seconds").get<float>(),
                                                  {v[0].get<float>(), v[1].get<float>(), v[2].get<float>()}, message))
                 return OperationFailure("VFX_KEY_REJECTED", message);
             return OperationOk(message, ContextData(controller));
         }},
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.curve_key.add",
            "Add a key to an addressed VFX curve channel",
            "vfx",
            {
                {"emitterId", EditorOperationParameterType::String, true, "Stable emitter ID"},
                {"moduleId", EditorOperationParameterType::String, true,
                 "Stable Position, Rotation, Scale, or Color module ID"},
                {"propertyId", EditorOperationParameterType::String, true,
                 "Animated property ID: value or eulerDegrees"},
                {"channel", EditorOperationParameterType::String, true, "Curve channel: x, y, z, r, g, b, or a"},
            },
            [&controller](const nlohmann::json& parameters) {
                std::string id{};
                std::string message{};
                if (!controller.AddCurveKey(parameters.at("emitterId").get<std::string>(),
                                            parameters.at("moduleId").get<std::string>(),
                                            parameters.at("propertyId").get<std::string>(),
                                            parameters.at("channel").get<std::string>(), id, message)) {
                    return OperationFailure("VFX_CURVE_KEY_EDIT_FAILED", message);
                }
                return OperationOk(message, ContextData(controller, id));
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.curve_key.remove",
            "Remove a key from an addressed VFX curve channel",
            "vfx",
            {
                {"emitterId", EditorOperationParameterType::String, true, "Stable emitter ID"},
                {"moduleId", EditorOperationParameterType::String, true, "Stable curve-owning module ID"},
                {"propertyId", EditorOperationParameterType::String, true, "Animated property ID"},
                {"channel", EditorOperationParameterType::String, true, "Curve channel"},
                {"keyId", EditorOperationParameterType::String, true, "Stable curve key ID"},
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                if (!controller.RemoveCurveKey(
                        parameters.at("emitterId").get<std::string>(), parameters.at("moduleId").get<std::string>(),
                        parameters.at("propertyId").get<std::string>(), parameters.at("channel").get<std::string>(),
                        parameters.at("keyId").get<std::string>(), message)) {
                    return OperationFailure("VFX_CURVE_KEY_EDIT_FAILED", message);
                }
                return OperationOk(message, ContextData(controller));
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.color_stop.add",
            "Add a stop to an addressed VFX color gradient",
            "vfx",
            {
                {"emitterId", EditorOperationParameterType::String, true, "Stable emitter ID"},
                {"moduleId", EditorOperationParameterType::String, true, "Stable Color module ID"},
                {"propertyId", EditorOperationParameterType::String, true, "Animated color property ID"},
            },
            [&controller](const nlohmann::json& parameters) {
                std::string id{};
                std::string message{};
                if (!controller.AddColorStop(parameters.at("emitterId").get<std::string>(),
                                             parameters.at("moduleId").get<std::string>(),
                                             parameters.at("propertyId").get<std::string>(), id, message)) {
                    return OperationFailure("VFX_COLOR_STOP_EDIT_FAILED", message);
                }
                return OperationOk(message, ContextData(controller, id));
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);

    registry.Register(
        {
            "vfx.color_stop.remove",
            "Remove a stop from an addressed VFX color gradient",
            "vfx",
            {
                {"emitterId", EditorOperationParameterType::String, true, "Stable emitter ID"},
                {"moduleId", EditorOperationParameterType::String, true, "Stable Color module ID"},
                {"propertyId", EditorOperationParameterType::String, true, "Animated color property ID"},
                {"stopId", EditorOperationParameterType::String, true, "Stable gradient stop ID"},
            },
            [&controller](const nlohmann::json& parameters) {
                std::string message{};
                if (!controller.RemoveColorStop(parameters.at("emitterId").get<std::string>(),
                                                parameters.at("moduleId").get<std::string>(),
                                                parameters.at("propertyId").get<std::string>(),
                                                parameters.at("stopId").get<std::string>(), message)) {
                    return OperationFailure("VFX_COLOR_STOP_EDIT_FAILED", message);
                }
                return OperationOk(message, ContextData(controller));
            },
        },
        HIKARI::EDITOR::EditorOperationKind::Mutation);
}

} // namespace HIKARI::EDITOR
