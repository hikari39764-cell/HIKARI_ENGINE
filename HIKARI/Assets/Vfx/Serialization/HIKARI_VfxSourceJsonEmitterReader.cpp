#include "Assets/Vfx/HIKARI_VfxBudgets.h"
#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJsonInternal.h"

#include <algorithm>
#include <utility>
#include <limits>

#include "Assets/Vfx/HIKARI_VfxProgram.h"

namespace HIKARI::VFX::SERIALIZATION::INTERNAL {
    namespace {
        using Json = nlohmann::json;

        bool ReadVec4(const Json& node, MATH::Vec4& outValue) {
            if (!node.is_array() || node.size() != 4u ||
                !node[0].is_number() || !node[1].is_number() ||
                !node[2].is_number() || !node[3].is_number()) {
                return false;
            }
            outValue = {
                node[0].get<float>(),
                node[1].get<float>(),
                node[2].get<float>(),
                node[3].get<float>(),
            };
            return true;
        }

        bool ReadUint32(
            const Json& node,
            uint32_t& outValue) {

            if (node.is_number_unsigned()) {
                const uint64_t value = node.get<uint64_t>();
                if (value > (std::numeric_limits<uint32_t>::max)()) {
                    return false;
                }
                outValue = static_cast<uint32_t>(value);
                return true;
            }
            if (!node.is_number_integer()) return false;
            const int64_t value = node.get<int64_t>();
            if (value < 0 || static_cast<uint64_t>(value) >
                (std::numeric_limits<uint32_t>::max)()) {
                return false;
            }
            outValue = static_cast<uint32_t>(value);
            return true;
        }

        bool ReadAttachment(
            const Json& node,
            VfxAttachmentData& outAttachment) {

            if (!node.is_object()) return false;
            VfxAttachmentData value = outAttachment;
            if (!TryParseVfxAttachmentMode(
                    node.value("mode", std::string{}), value.mode) ||
                !TryParseVfxFollowMode(
                    node.value("follow", std::string{}), value.follow) ||
                (node.contains("socketName") &&
                 !node["socketName"].is_string()) ||
                (node.contains("offset") &&
                 !ReadTransform(node["offset"], value.offset))) {
                return false;
            }
            if (node.contains("socketName")) {
                value.socketName = node["socketName"].get<std::string>();
            }
            outAttachment = std::move(value);
            return true;
        }

        bool ReadSpecialOutput(
            const Json& node,
            VfxSpecialOutputData& outOutput) {

            if (!node.is_object() ||
                !node.contains("id") || !node["id"].is_string() ||
                !TryParseVfxSpecialOutputType(
                    node.value("type", std::string{}), outOutput.type) ||
                (node.contains("enabled") &&
                 !node["enabled"].is_boolean())) {
                return false;
            }
            VfxSpecialOutputData value = outOutput;
            value.id = node["id"].get<std::string>();
            value.enabled = node.value("enabled", true);

            const auto readFloat = [](const Json& object,
                                      const char* key,
                                      float& destination) {
                if (!object.contains(key)) return true;
                if (!object[key].is_number()) return false;
                destination = object[key].get<float>();
                return true;
            };
            const auto readUnsigned = [](const Json& object,
                                         const char* key,
                                         uint32_t& destination) {
                if (!object.contains(key)) return true;
                return ReadUint32(object[key], destination);
            };

            if (node.contains("weaponPathDistortion")) {
                const Json& profile = node["weaponPathDistortion"];
                if (!profile.is_object() ||
                    !readFloat(profile, "distortion",
                        value.weaponPathDistortion.distortion) ||
                    !readFloat(profile, "blur",
                        value.weaponPathDistortion.blur) ||
                    !readFloat(profile, "opacity",
                        value.weaponPathDistortion.opacity) ||
                    !readFloat(profile, "emissive",
                        value.weaponPathDistortion.emissive) ||
                    !readFloat(profile, "flow",
                        value.weaponPathDistortion.flow) ||
                    !readFloat(profile, "width",
                        value.weaponPathDistortion.width) ||
                    !readFloat(profile, "lifetimeSeconds",
                        value.weaponPathDistortion.lifetimeSeconds) ||
                    (profile.contains("bodyColor") &&
                     !ReadVec4(profile["bodyColor"],
                        value.weaponPathDistortion.bodyColor)) ||
                    (profile.contains("edgeColor") &&
                     !ReadVec4(profile["edgeColor"],
                        value.weaponPathDistortion.edgeColor))) {
                    return false;
                }
            }
            if (node.contains("screenImpulse")) {
                const Json& profile = node["screenImpulse"];
                if (!profile.is_object() ||
                    !readFloat(profile, "durationSeconds",
                        value.screenImpulse.durationSeconds) ||
                    !readFloat(profile, "maxRadius",
                        value.screenImpulse.maxRadius) ||
                    !readFloat(profile, "ringThickness",
                        value.screenImpulse.ringThickness) ||
                    !readFloat(profile, "distortion",
                        value.screenImpulse.distortion) ||
                    !readFloat(profile, "chromaticAberration",
                        value.screenImpulse.chromaticAberration) ||
                    !readFloat(profile, "flash",
                        value.screenImpulse.flash) ||
                    !readFloat(profile, "intensity",
                        value.screenImpulse.intensity) ||
                    (profile.contains("color") &&
                     !ReadVec4(profile["color"], value.screenImpulse.color)) ||
                    !readFloat(profile, "waveFrequency",
                        value.screenImpulse.waveFrequency) ||
                    !readFloat(profile, "waveAmplitude",
                        value.screenImpulse.waveAmplitude) ||
                    !readFloat(profile, "waveDamping",
                        value.screenImpulse.waveDamping) ||
                    !readUnsigned(profile, "waveCount",
                        value.screenImpulse.waveCount)) {
                    return false;
                }
            }
            if (node.contains("skinnedAfterimage")) {
                const Json& profile = node["skinnedAfterimage"];
                if (!profile.is_object() ||
                    !readUnsigned(profile, "sampleCount",
                        value.skinnedAfterimage.sampleCount) ||
                    !readFloat(profile, "lifetimeSeconds",
                        value.skinnedAfterimage.lifetimeSeconds) ||
                    !readFloat(profile, "opacity",
                        value.skinnedAfterimage.opacity) ||
                    !readFloat(profile, "fresnel",
                        value.skinnedAfterimage.fresnel) ||
                    !readFloat(profile, "dissolve",
                        value.skinnedAfterimage.dissolve) ||
                    !readFloat(profile, "shellOffset",
                        value.skinnedAfterimage.shellOffset) ||
                    (profile.contains("color") &&
                     !ReadVec4(profile["color"],
                        value.skinnedAfterimage.color)) ||
                    !readFloat(profile, "intensity",
                        value.skinnedAfterimage.intensity)) {
                    return false;
                }
            }
            if (node.contains("light")) {
                const Json& profile = node["light"];
                if (!profile.is_object() ||
                    (profile.contains("color") &&
                     !ReadVec4(profile["color"], value.light.color)) ||
                    !readFloat(profile, "intensity",
                        value.light.intensity) ||
                    !readFloat(profile, "range", value.light.range) ||
                    !readFloat(profile, "sourceRadius",
                        value.light.sourceRadius) ||
                    !readFloat(profile, "flickerFrequency",
                        value.light.flickerFrequency) ||
                    !readFloat(profile, "flickerAmplitude",
                        value.light.flickerAmplitude) ||
                    !readUnsigned(profile, "priority",
                        value.light.priority) ||
                    (profile.contains("castsShadows") &&
                     !profile["castsShadows"].is_boolean())) {
                    return false;
                }
                if (profile.contains("castsShadows")) {
                    value.light.castsShadows =
                        profile["castsShadows"].get<bool>();
                }
            }
            outOutput = std::move(value);
            return true;
        }
    }

    bool ReadVfxSourceEmitter(
        const Json& node,
        uint32_t sourceVersion,
        VfxEmitterData& outEmitter) {
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
            !node.contains("order")) {
            return false;
        }
        emitter.id = node["id"].get<std::string>();
        emitter.name = node["name"].get<std::string>();
        emitter.parentEmitterId =
            node["parentEmitterId"].get<std::string>();
        if (!ReadUint32(node["order"], emitter.order)) return false;
        // Legacy documents do not have an attachment module. Derive its
        // migrated stable ID from the emitter so several legacy emitters do
        // not collide in the asset-wide ID namespace.
        emitter.attachment.id = emitter.id + ".attachment";
        if (node.contains("startDelay")) {
            if (!node["startDelay"].is_number()) return false;
            emitter.startDelay = node["startDelay"].get<float>();
        } else if (node.contains("startDelaySeconds")) {
            if (!node["startDelaySeconds"].is_number()) return false;
            emitter.startDelay =
                node["startDelaySeconds"].get<float>();
        }
        if (node.contains("kind")) {
            if (!node["kind"].is_string()) return false;
            const auto kind = node["kind"].get<std::string>();
            if (kind == "Particles") emitter.kind = VfxLayerKind::Particles;
            else if (kind == "Shape") emitter.kind = VfxLayerKind::Shape;
            else if (kind == "Group") emitter.kind = VfxLayerKind::Group;
            else if (kind == "Adjustment") emitter.kind = VfxLayerKind::Adjustment;
            else return false;
        }
        if (node.contains("activeDuration")) {
            if (!node["activeDuration"].is_number()) return false;
            emitter.activeDuration =
                node["activeDuration"].get<float>();
        } else if (node.contains("activeDurationSeconds")) {
            if (!node["activeDurationSeconds"].is_number()) return false;
            emitter.activeDuration =
                node["activeDurationSeconds"].get<float>();
        }
        if (node.contains("anchor") &&
            !ReadVec3(node["anchor"], emitter.anchor)) {
            return false;
        }
        if (node.contains("connectTarget")) {
            const auto& connect = node["connectTarget"];
            if (!connect.is_object() || !connect.contains("mode") ||
                !connect["mode"].is_string()) {
                return false;
            }
            VfxConnectTargetData value = emitter.connectTarget;
            const std::string mode = connect["mode"].get<std::string>();
            if (mode == "None") value.mode = VfxConnectTargetMode::None;
            else if (mode == "LayerOrigin") value.mode = VfxConnectTargetMode::LayerOrigin;
            else if (mode == "LocalPosition") value.mode = VfxConnectTargetMode::LocalPosition;
            else return false;
            if (connect.contains("targetLayerId")) {
                if (!connect["targetLayerId"].is_string()) return false;
                value.targetLayerId = connect["targetLayerId"].get<std::string>();
            }
            if (connect.contains("localPosition") &&
                !ReadVec3(connect["localPosition"], value.localPosition)) {
                return false;
            }
            const auto readScalar = [&connect](const char* key, float& target) {
                if (!connect.contains(key)) return true;
                if (!connect[key].is_number()) return false;
                target = connect[key].get<float>();
                return true;
            };
            if (!readScalar("pivot", value.pivot) ||
                !readScalar("lengthScale", value.lengthScale) ||
                !readScalar("startOffset", value.startOffset) ||
                !readScalar("endOffset", value.endOffset)) {
                return false;
            }
            if (connect.contains("orient")) {
                if (!connect["orient"].is_boolean()) return false;
                value.orient = connect["orient"].get<bool>();
            }
            if (connect.contains("stretch")) {
                if (!connect["stretch"].is_boolean()) return false;
                value.stretch = connect["stretch"].get<bool>();
            }
            emitter.connectTarget = value;
        }
        if (node.contains("meshOutput")) {
            const auto& mesh = node["meshOutput"];
            if (!mesh.is_object()) {
                return false;
            }
            VfxMeshOutputData value = emitter.meshOutput;
            if (mesh.contains("enabled")) {
                if (!mesh["enabled"].is_boolean()) return false;
                value.enabled = mesh["enabled"].get<bool>();
            }
            if (mesh.contains("meshAssetGuid")) {
                if (!mesh["meshAssetGuid"].is_string()) return false;
                value.meshAssetGuid = AssetGuid{
                    mesh["meshAssetGuid"].get<std::string>() };
            }
            if (mesh.contains("castShadows")) {
                if (!mesh["castShadows"].is_boolean()) return false;
                value.castShadows = mesh["castShadows"].get<bool>();
            }
            if (mesh.contains("receiveShadow")) {
                if (!mesh["receiveShadow"].is_boolean()) return false;
                value.receiveShadow = mesh["receiveShadow"].get<bool>();
            }
            emitter.meshOutput = value;
        }

        if (node.contains("history")) {
            const auto& h = node["history"];
            if (!h.is_object() || !h.contains("mode") || !h["mode"].is_string() ||
                !h.contains("duration") || !h["duration"].is_number() ||
                !h.contains("samples") || !ReadUint32(h["samples"], emitter.history.samples) ||
                !h.contains("breakDistance") || !h["breakDistance"].is_number()) return false;
            const auto mode = h["mode"].get<std::string>();
            if (mode == "None") emitter.history.mode = VfxHistoryMode::None;
            else if (mode == "Afterimage") emitter.history.mode = VfxHistoryMode::Afterimage;
            else if (mode == "Trail") emitter.history.mode = VfxHistoryMode::Trail;
            else return false;
            emitter.history.duration = h["duration"].get<float>();
            emitter.history.breakDistance = h["breakDistance"].get<float>();
            if (!IsValidVfxHistory(emitter.history)) return false;
        }
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
        if (basic.contains("localSimulation") && !basic["localSimulation"].is_boolean()) return false;
        emitter.basic.localSimulation = basic.value("localSimulation",false);

        emitter.emitterTransform.id =
            transform["id"].get<std::string>();
        if (!transform.contains("transform") ||
            !ReadTransform(
                transform["transform"],
                emitter.emitterTransform.transform)) {
            return false;
        }

        if (modules.contains("attachment")) {
            const Json& attachment = modules["attachment"];
            if (!attachment.is_object() ||
                !attachment.contains("id") ||
                !attachment["id"].is_string() ||
                !ReadAttachment(attachment, emitter.attachment)) {
                return false;
            }
            emitter.attachment.id =
                attachment["id"].get<std::string>();
        }

        emitter.spawn.id = spawn["id"].get<std::string>();
        if (!spawn.contains("ratePerSecond") ||
            !spawn["ratePerSecond"].is_number() ||
            !spawn.contains("burstCount") ||
            !spawn.contains("maximumParticles")) {
            return false;
        }
        emitter.spawn.ratePerSecond =
            spawn["ratePerSecond"].get<float>();
        if (!ReadUint32(spawn["burstCount"], emitter.spawn.burstCount)) {
            return false;
        }
        if (spawn.contains("burstTimeline")) {
            if (!spawn["burstTimeline"].is_array() ||
                spawn["burstTimeline"].size() >
                    kMaximumVfxProgramBurstKeys) {
                return false;
            }
            for (const Json& item : spawn["burstTimeline"]) {
                if (!item.is_object() ||
                    !item.contains("id") || !item["id"].is_string() ||
                    !item.contains("time") ||
                    !item["time"].is_number() ||
                    !item.contains("count") ||
                    (!item["count"].is_number_unsigned() &&
                     !item["count"].is_number_integer())) {
                    return false;
                }
                uint32_t count = 0u;
                if (!ReadUint32(item["count"], count)) return false;
                emitter.spawn.burstTimeline.push_back({
                    item["id"].get<std::string>(),
                    item["time"].get<float>(),
                    count,
                });
            }
        }
        if (emitter.spawn.burstTimeline.empty() &&
            emitter.spawn.burstCount > 0u) {
            emitter.spawn.burstTimeline.push_back({
                emitter.spawn.id + ".burst.legacy",
                0.0f,
                emitter.spawn.burstCount });
        }
        if (!emitter.spawn.burstTimeline.empty()) {
            std::sort(
                emitter.spawn.burstTimeline.begin(),
                emitter.spawn.burstTimeline.end(),
                [](const VfxBurstKeyData& left,
                   const VfxBurstKeyData& right) {
                    if (left.time != right.time) {
                        return left.time < right.time;
                    }
                    return left.id < right.id;
                });
            const auto atStart = std::find_if(
                emitter.spawn.burstTimeline.begin(),
                emitter.spawn.burstTimeline.end(),
                [](const VfxBurstKeyData& key) {
                    return key.time == 0.0f;
                });
            emitter.spawn.burstCount = atStart !=
                emitter.spawn.burstTimeline.end()
                ? atStart->count
                : 0u;
        }
        if (sourceVersion >= 3u &&
            !TryParseVfxParticleCapacityMode(
                spawn.value("capacityMode", std::string{}),
                emitter.spawn.capacityMode)) {
            return false;
        }
        if (!ReadUint32(
                spawn["maximumParticles"],
                emitter.spawn.maximumParticles)) {
            return false;
        }

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
        if (shape.contains("boxHalfExtents")) {
            if (!ReadVec3(
                    shape["boxHalfExtents"],
                    emitter.shape.boxHalfExtents)) {
                return false;
            }
        }
        if (shape.contains("cylinderHeight")) {
            if (!shape["cylinderHeight"].is_number()) {
                return false;
            }
            emitter.shape.cylinderHeight =
                shape["cylinderHeight"].get<float>();
        }
        if (shape.contains("circleInnerRadius")) {
            if (!shape["circleInnerRadius"].is_number()) {
                return false;
            }
            emitter.shape.circleInnerRadius =
                shape["circleInnerRadius"].get<float>();
        }
        if (shape.contains("circleEmitOnEdge")) {
            if (!shape["circleEmitOnEdge"].is_boolean()) {
                return false;
            }
            emitter.shape.circleEmitOnEdge =
                shape["circleEmitOnEdge"].get<bool>();
        }
        // 放出角度範囲。古い資産には無いので省略可能 (既定 360 = 全周)。
        if (shape.contains("circleArcDegrees")) {
            if (!shape["circleArcDegrees"].is_number()) {
                return false;
            }
            emitter.shape.circleArcDegrees =
                shape["circleArcDegrees"].get<float>();
        }
        if (shape.contains("edgeLength")) {
            if (!shape["edgeLength"].is_number()) {
                return false;
            }
            emitter.shape.edgeLength =
                shape["edgeLength"].get<float>();
        }

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
        if (forces.contains("collisionPlaneEnabled") && !forces["collisionPlaneEnabled"].is_boolean()) return false;
        for (const char* name : {"collisionPlaneHeight","restitution","friction"})
            if (forces.contains(name) && !forces[name].is_number()) return false;
        emitter.forces.collisionPlaneEnabled = forces.value("collisionPlaneEnabled",false);
        emitter.forces.collisionPlaneHeight = forces.value("collisionPlaneHeight",0.0f);
        emitter.forces.restitution = forces.value("restitution",0.35f);
        emitter.forces.friction = forces.value("friction",0.2f);

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
                emitter.renderCommon.blendMode)) {
            return false;
        }
        emitter.renderCommon.materialAssetGuid = AssetGuid{
            renderCommon["materialAssetGuid"].get<std::string>() };
        if (sourceVersion >= 3u) {
            if (!TryParseVfxMaterialMode(
                    renderCommon.value(
                        "materialMode", std::string{}),
                    emitter.renderCommon.materialMode) ||
                !renderCommon.contains("colorTextureAssetGuid") ||
                !renderCommon["colorTextureAssetGuid"].is_string()) {
                return false;
            }
            emitter.renderCommon.colorTextureAssetGuid = AssetGuid{
                renderCommon["colorTextureAssetGuid"].get<std::string>() };
            if (renderCommon.contains("maskTextureAssetGuid")) {
                if (!renderCommon["maskTextureAssetGuid"].is_string()) {
                    return false;
                }
                emitter.renderCommon.maskTextureAssetGuid = AssetGuid{
                    renderCommon["maskTextureAssetGuid"].get<std::string>() };
            }
            if (renderCommon.contains("noiseTextureAssetGuid")) {
                if (!renderCommon["noiseTextureAssetGuid"].is_string()) {
                    return false;
                }
                emitter.renderCommon.noiseTextureAssetGuid = AssetGuid{
                    renderCommon["noiseTextureAssetGuid"].get<std::string>() };
            }
            // 2 つ目のノイズ場。古い資産には無いので省略可能。
            if (renderCommon.contains("flowMapTextureAssetGuid")) {
                if (!renderCommon["flowMapTextureAssetGuid"].is_string()) return false;
                emitter.renderCommon.flowMapTextureAssetGuid = AssetGuid{
                    renderCommon["flowMapTextureAssetGuid"].get<std::string>() };
            }
            if (renderCommon.contains("detailNoiseTextureAssetGuid")) {
                if (!renderCommon["detailNoiseTextureAssetGuid"].is_string()) {
                    return false;
                }
                emitter.renderCommon.detailNoiseTextureAssetGuid = AssetGuid{
                    renderCommon["detailNoiseTextureAssetGuid"].get<std::string>() };
            }
        } else {
            emitter.spawn.capacityMode =
                VfxParticleCapacityMode::Automatic;
            emitter.renderCommon.materialMode =
                emitter.renderCommon.materialAssetGuid.IsValid()
                    ? VfxMaterialMode::CustomMaterial
                    : VfxMaterialMode::StandardUnlit;
        }
        if (sourceVersion >= 5u) {
            if (!TryParseVfxParticleDepthMode(
                    renderCommon.value("depthMode", std::string{}),
                    emitter.renderCommon.depthMode) ||
                !renderCommon.contains("softIntersectionDistance") ||
                !renderCommon["softIntersectionDistance"].is_number() ||
                !renderCommon.contains("depthBias") ||
                !renderCommon["depthBias"].is_number()) {
                return false;
            }
            emitter.renderCommon.softIntersectionDistance =
                renderCommon["softIntersectionDistance"].get<float>();
            emitter.renderCommon.depthBias =
                renderCommon["depthBias"].get<float>();
        } else {
            if (!renderCommon.contains("depthTest") ||
                !renderCommon["depthTest"].is_boolean()) {
                return false;
            }
            emitter.renderCommon.depthMode =
                renderCommon["depthTest"].get<bool>()
                    ? VfxParticleDepthMode::WorldOccluded
                    : VfxParticleDepthMode::ForegroundAccent;
        }
        const auto readOptionalUnsigned = [&renderCommon](
            const char* name,
            uint32_t& outValue) {
            if (!renderCommon.contains(name)) return true;
            return ReadUint32(renderCommon[name], outValue);
        };
        const auto readOptionalBool = [&renderCommon](
            const char* name,
            bool& outValue) {
            if (!renderCommon.contains(name)) return true;
            if (!renderCommon[name].is_boolean()) return false;
            outValue = renderCommon[name].get<bool>();
            return true;
        };
        if (!readOptionalUnsigned(
                "flipbookColumns",
                emitter.renderCommon.flipbookColumns) ||
            !readOptionalUnsigned(
                "flipbookRows",
                emitter.renderCommon.flipbookRows) ||
            !readOptionalUnsigned(
                "flipbookFrameCount",
                emitter.renderCommon.flipbookFrameCount) ||
            !readOptionalBool(
                "flipbookLoop",
                emitter.renderCommon.flipbookLoop) ||
            !readOptionalBool(
                "flipbookRandomStart",
                emitter.renderCommon.flipbookRandomStart)) {
            return false;
        }
        if (renderCommon.contains("flipbookFramesPerSecond")) {
            if (!renderCommon["flipbookFramesPerSecond"].is_number()) {
                return false;
            }
            emitter.renderCommon.flipbookFramesPerSecond =
                renderCommon["flipbookFramesPerSecond"].get<float>();
        }

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
        // 軸固定ビルボードの軸。後から追加した任意項目。
        if (renderShape.contains("billboardAxis") &&
            (!renderShape["billboardAxis"].is_string() ||
             !TryParseVfxBillboardAxis(
                 renderShape["billboardAxis"].get<std::string>(),
                 emitter.renderShape.billboardAxis))) {
            return false;
        }
        if (renderShape.contains("ribbonSegmentCount")) {
            if (!ReadUint32(
                    renderShape["ribbonSegmentCount"],
                    emitter.renderShape.ribbonSegmentCount)) {
                return false;
            }
        }
        if (renderShape.contains("ribbonWidth")) {
            if (!renderShape["ribbonWidth"].is_number()) {
                return false;
            }
            emitter.renderShape.ribbonWidth =
                renderShape["ribbonWidth"].get<float>();
        }
        if (renderShape.contains("ringOuterRadius")) {
            if (!renderShape["ringOuterRadius"].is_number()) {
                return false;
            }
            emitter.renderShape.ringOuterRadius =
                renderShape["ringOuterRadius"].get<float>();
        }
        if (renderShape.contains("ringInnerRadius")) {
            if (!renderShape["ringInnerRadius"].is_number()) {
                return false;
            }
            emitter.renderShape.ringInnerRadius =
                renderShape["ringInnerRadius"].get<float>();
        }
        if (renderShape.contains("ringSegmentCount")) {
            if (!ReadUint32(
                    renderShape["ringSegmentCount"],
                    emitter.renderShape.ringSegmentCount)) {
                return false;
            }
        }
        if (renderShape.contains("ringStartAngleDegrees")) {
            if (!renderShape["ringStartAngleDegrees"].is_number()) {
                return false;
            }
            emitter.renderShape.ringStartAngleDegrees =
                renderShape["ringStartAngleDegrees"].get<float>();
        }
        if (renderShape.contains("ringEndAngleDegrees")) {
            if (!renderShape["ringEndAngleDegrees"].is_number()) {
                return false;
            }
            emitter.renderShape.ringEndAngleDegrees =
                renderShape["ringEndAngleDegrees"].get<float>();
        }
        if (renderShape.contains("uvMode")) {
            if (!renderShape["uvMode"].is_string() ||
                !TryParseVfxShapeUvMode(
                    renderShape["uvMode"].get<std::string>(),
                    emitter.renderShape.uvMode)) {
                return false;
            }
        }
        if (renderShape.contains("uvTileLength")) {
            if (!renderShape["uvTileLength"].is_number()) {
                return false;
            }
            emitter.renderShape.uvTileLength =
                renderShape["uvTileLength"].get<float>();
        }
        if (renderShape.contains("uvTilingU")) {
            if (!renderShape["uvTilingU"].is_number()) {
                return false;
            }
            emitter.renderShape.uvTilingU =
                renderShape["uvTilingU"].get<float>();
        }
        if (renderShape.contains("uvTilingV")) {
            if (!renderShape["uvTilingV"].is_number()) {
                return false;
            }
            emitter.renderShape.uvTilingV =
                renderShape["uvTilingV"].get<float>();
        }
        if (renderShape.contains("uvRotationDegrees")) {
            if (!renderShape["uvRotationDegrees"].is_number()) {
                return false;
            }
            emitter.renderShape.uvRotationDegrees =
                renderShape["uvRotationDegrees"].get<float>();
        }
        if (renderShape.contains("uvSwapAxes")) {
            if (!renderShape["uvSwapAxes"].is_boolean()) {
                return false;
            }
            emitter.renderShape.uvSwapAxes =
                renderShape["uvSwapAxes"].get<bool>();
        }
        // taper は後から追加した任意項目。無い資産は既定値のまま読む。
        for (const auto& [key, target] : {
                 std::pair{ "taperStart", &emitter.renderShape.taperStart },
                 std::pair{ "taperEnd", &emitter.renderShape.taperEnd },
                 std::pair{ "cylinderTopRadius",
                            &emitter.renderShape.cylinderTopRadius },
                 std::pair{ "cylinderBottomRadius",
                            &emitter.renderShape.cylinderBottomRadius },
                 std::pair{ "coneBaseRadius",
                            &emitter.renderShape.coneBaseRadius },
                 std::pair{ "bendAmount",
                            &emitter.renderShape.bendAmount },
                 std::pair{ "twistDegrees",
                            &emitter.renderShape.twistDegrees },
                 std::pair{ "torusMajorRadius",
                            &emitter.renderShape.torusMajorRadius },
                 std::pair{ "torusMinorRadius",
                            &emitter.renderShape.torusMinorRadius },
                 std::pair{ "capsuleRadius",
                            &emitter.renderShape.capsuleRadius } }) {
            if (!renderShape.contains(key)) {
                continue;
            }
            if (!renderShape[key].is_number()) {
                return false;
            }
            *target = renderShape[key].get<float>();
        }

        if (node.contains("specialOutputs")) {
            if (!node["specialOutputs"].is_array() ||
                node["specialOutputs"].size() >
                    kMaximumVfxProgramSpecialOutputs) {
                return false;
            }
            for (const Json& item : node["specialOutputs"]) {
                VfxSpecialOutputData output{};
                if (!ReadSpecialOutput(item, output)) return false;
                emitter.specialOutputs.push_back(std::move(output));
            }
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
        if (node.contains("effects")) {
            if (!node["effects"].is_array() || node["effects"].size() > kMaximumVfxEffectsPerLayer) return false;
            for (const auto& item : node["effects"]) {
                MaterialEffectStep step;
                if (item.contains("textureTarget") &&
                    (!item["textureTarget"].is_string() || !ParseMaterialEffectTextureTarget(
                        item["textureTarget"].get<std::string>(), step.textureTarget))) return false;
                if (!item.is_object() || !item.contains("id") || !item["id"].is_string() ||
                    !ParseMaterialEffectKind(item.value("kind", ""), step.kind) ||
                    !item.contains("enabled") || !item["enabled"].is_boolean() ||
                    !item.contains("parameters") || !ReadVec4(item["parameters"], step.parameters) ||
                    !item.contains("colorA") || !ReadVec4(item["colorA"], step.colorA) ||
                    !item.contains("colorB") || !ReadVec4(item["colorB"], step.colorB)) return false;
                // parameters2 は後から追加した任意項目。無い資産は既定値のまま読む。
                if (item.contains("parameters2") && !ReadVec4(item["parameters2"], step.parameters2))
                    return false;
                // 多段グラデーションのストップ。無い資産は colorA/colorB のまま。
                if (item.contains("colorStops")) {
                    const auto& stops = item["colorStops"];
                    if (!stops.is_array() ||
                        stops.size() > kMaximumMaterialEffectColorStops) {
                        return false;
                    }
                    for (const auto& entry : stops) {
                        MaterialEffectStep::ColorStop stop{};
                        if (!entry.is_object() || !entry.contains("id") ||
                            !entry["id"].is_string() ||
                            !entry.contains("time") || !entry["time"].is_number() ||
                            !entry.contains("color") ||
                            !ReadVec4(entry["color"], stop.color)) {
                            return false;
                        }
                        stop.id = entry["id"].get<std::string>();
                        stop.time = entry["time"].get<float>();
                        step.colorStops.push_back(std::move(stop));
                    }
                }
                step.id = item["id"].get<std::string>(); step.enabled = item["enabled"].get<bool>();
                emitter.effects.push_back(std::move(step));
            }
        }
        if (node.contains("animationTiming")) {
            const auto& timing=node["animationTiming"];
            if (!timing.is_object() || !timing.contains("sourceDuration") || !timing["sourceDuration"].is_number() ||
                !timing.contains("offset") || !timing["offset"].is_number() || !timing.contains("rate") || !timing["rate"].is_number()) return false;
            emitter.animationTiming={timing["sourceDuration"].get<float>(),timing["offset"].get<float>(),timing["rate"].get<float>()};
        }
        if (node.contains("parameterTracks")) {
            const auto& tracks=node["parameterTracks"];
            if (!tracks.is_array() || tracks.size()>kMaxVfxParameterTracks) return false;
            for (const auto& item:tracks) {
                VfxParameterTrackData track;
                if (!item.is_object() || !item.contains("id") || !item["id"].is_string() ||
                    !item.contains("targetId") || !item["targetId"].is_string() ||
                    !item.contains("propertyId") || !item["propertyId"].is_string() ||
                    !item.contains("curve") || !ReadCurve(item["curve"],track.curve)) return false;
                track.id=item["id"].get<std::string>(); track.targetId=item["targetId"].get<std::string>();
                track.propertyId=item["propertyId"].get<std::string>(); emitter.parameterTracks.push_back(std::move(track));
            }
        }
        outEmitter = std::move(emitter);
        return true;
    }

} // namespace HIKARI::VFX::SERIALIZATION::INTERNAL
