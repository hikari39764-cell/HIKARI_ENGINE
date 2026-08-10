#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJsonInternal.h"

#include <utility>

namespace HIKARI::VFX::SERIALIZATION::INTERNAL {
    namespace {
        using Json = nlohmann::json;
    }

    bool ReadVfxSourceEmitter(const Json& node, VfxEmitterData& outEmitter) {
        if (!node.is_object() || !node.contains("modules") ||
            !node["modules"].is_object() ||
            !node.contains("inputLinks") ||
            !node["inputLinks"].is_array()) {
            return false;
        }
        VfxEmitterData emitter{};
        if (!node.contains("id") || !node["id"].is_string() ||
            !node.contains("name") || !node["name"].is_string() ||
            !node.contains("parentEmitterId") ||
            !node["parentEmitterId"].is_string() ||
            !node.contains("order") ||
            !node["order"].is_number_unsigned()) {
            return false;
        }
        emitter.id = node["id"].get<std::string>();
        emitter.name = node["name"].get<std::string>();
        emitter.parentEmitterId =
            node["parentEmitterId"].get<std::string>();
        emitter.order = node["order"].get<uint32_t>();

        const Json& modules = node["modules"];
        constexpr const char* kModuleNames[] = {
            "basic", "emitterTransform", "spawn", "shape", "position",
            "rotation", "scale", "forces", "color", "renderCommon",
            "renderShape",
        };
        for (const char* name : kModuleNames) {
            if (!modules.contains(name) || !modules[name].is_object() ||
                !modules[name].contains("id") ||
                !modules[name]["id"].is_string()) {
                return false;
            }
        }
        const Json& basic = modules["basic"];
        const Json& transform = modules["emitterTransform"];
        const Json& spawn = modules["spawn"];
        const Json& shape = modules["shape"];
        const Json& position = modules["position"];
        const Json& rotation = modules["rotation"];
        const Json& scale = modules["scale"];
        const Json& forces = modules["forces"];
        const Json& color = modules["color"];
        const Json& renderCommon = modules["renderCommon"];
        const Json& renderShape = modules["renderShape"];

        emitter.basic.id = basic["id"].get<std::string>();
        if (!basic.contains("enabled") ||
            !basic["enabled"].is_boolean() ||
            !basic.contains("lifetime") ||
            !ReadDistribution(
                basic["lifetime"], emitter.basic.lifetime)) {
            return false;
        }
        emitter.basic.enabled = basic["enabled"].get<bool>();

        emitter.emitterTransform.id =
            transform["id"].get<std::string>();
        if (!transform.contains("transform") ||
            !ReadTransform(
                transform["transform"],
                emitter.emitterTransform.transform)) {
            return false;
        }

        emitter.spawn.id = spawn["id"].get<std::string>();
        if (!spawn.contains("ratePerSecond") ||
            !spawn["ratePerSecond"].is_number() ||
            !spawn.contains("burstCount") ||
            !spawn["burstCount"].is_number_unsigned() ||
            !spawn.contains("maximumParticles") ||
            !spawn["maximumParticles"].is_number_unsigned()) {
            return false;
        }
        emitter.spawn.ratePerSecond =
            spawn["ratePerSecond"].get<float>();
        emitter.spawn.burstCount = spawn["burstCount"].get<uint32_t>();
        emitter.spawn.maximumParticles =
            spawn["maximumParticles"].get<uint32_t>();

        emitter.shape.id = shape["id"].get<std::string>();
        if (!TryParseVfxShapeType(
                shape.value("type", std::string{}),
                emitter.shape.type) ||
            !shape.contains("radius") ||
            !ReadDistribution(shape["radius"], emitter.shape.radius) ||
            !shape.contains("coneAngleDegrees") ||
            !shape["coneAngleDegrees"].is_number()) {
            return false;
        }
        emitter.shape.coneAngleDegrees =
            shape["coneAngleDegrees"].get<float>();

        emitter.position.id = position["id"].get<std::string>();
        if (!position.contains("radialSpeed") ||
            !ReadDistribution(
                position["radialSpeed"],
                emitter.position.radialSpeed) ||
            !position.contains("value") ||
            !ReadAnimatedVec3(
                position["value"],
                emitter.position.value)) {
            return false;
        }

        emitter.rotation.id = rotation["id"].get<std::string>();
        if (!rotation.contains("eulerDegrees") ||
            !ReadAnimatedVec3(
                rotation["eulerDegrees"],
                emitter.rotation.eulerDegrees)) {
            return false;
        }

        emitter.scale.id = scale["id"].get<std::string>();
        if (!scale.contains("value") ||
            !ReadAnimatedVec3(scale["value"], emitter.scale.value)) {
            return false;
        }

        emitter.forces.id = forces["id"].get<std::string>();
        if (!forces.contains("gravity") ||
            !ReadVec3(forces["gravity"], emitter.forces.gravity) ||
            !forces.contains("drag") || !forces["drag"].is_number()) {
            return false;
        }
        emitter.forces.drag = forces["drag"].get<float>();

        emitter.color.id = color["id"].get<std::string>();
        if (!color.contains("value") ||
            !ReadAnimatedColor(color["value"], emitter.color.value)) {
            return false;
        }

        emitter.renderCommon.id =
            renderCommon["id"].get<std::string>();
        if (!renderCommon.contains("materialAssetGuid") ||
            !renderCommon["materialAssetGuid"].is_string() ||
            !TryParseVfxBlendMode(
                renderCommon.value("blendMode", std::string{}),
                emitter.renderCommon.blendMode) ||
            !renderCommon.contains("depthTest") ||
            !renderCommon["depthTest"].is_boolean()) {
            return false;
        }
        emitter.renderCommon.materialAssetGuid = AssetGuid{
            renderCommon["materialAssetGuid"].get<std::string>() };
        emitter.renderCommon.depthTest =
            renderCommon["depthTest"].get<bool>();

        emitter.renderShape.id =
            renderShape["id"].get<std::string>();
        if (!TryParseVfxRenderShapeType(
                renderShape.value("type", std::string{}),
                emitter.renderShape.type) ||
            !TryParseVfxBillboardMode(
                renderShape.value("billboardMode", std::string{}),
                emitter.renderShape.billboardMode)) {
            return false;
        }

        for (const Json& item : node["inputLinks"]) {
            if (!item.is_object() ||
                !item.contains("id") || !item["id"].is_string() ||
                !item.contains("inputId") ||
                !item["inputId"].is_string() ||
                !item.contains("target") ||
                !item["target"].is_object() ||
                !item.contains("scale") || !item["scale"].is_number() ||
                !item.contains("bias") || !item["bias"].is_number()) {
                return false;
            }
            const Json& target = item["target"];
            if (!target.contains("emitterId") ||
                !target["emitterId"].is_string() ||
                !target.contains("moduleId") ||
                !target["moduleId"].is_string() ||
                !target.contains("propertyId") ||
                !target["propertyId"].is_string()) {
                return false;
            }
            emitter.inputLinks.push_back({
                item["id"].get<std::string>(),
                item["inputId"].get<std::string>(),
                {
                    target["emitterId"].get<std::string>(),
                    target["moduleId"].get<std::string>(),
                    target["propertyId"].get<std::string>(),
                },
                item["scale"].get<float>(),
                item["bias"].get<float>(),
            });
        }
        outEmitter = std::move(emitter);
        return true;
    }

} // namespace HIKARI::VFX::SERIALIZATION::INTERNAL

