#include "Assets/Vfx/HIKARI_VfxBudgets.h"
#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJsonInternal.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

#include "Assets/Vfx/HIKARI_VfxEnumNames.h"
#include "Assets/Vfx/HIKARI_VfxProgram.h"

namespace HIKARI::VFX::SERIALIZATION::INTERNAL {
namespace {
using Json = nlohmann::json;

bool ReadUint32(const Json& node, uint32_t& outValue) {
    if (node.is_number_unsigned()) {
        const uint64_t value = node.get<uint64_t>();
        if (value > (std::numeric_limits<uint32_t>::max)()) {
            return false;
        }
        outValue = static_cast<uint32_t>(value);
        return true;
    }
    if (!node.is_number_integer())
        return false;
    const int64_t value = node.get<int64_t>();
    if (value < 0 || static_cast<uint64_t>(value) > (std::numeric_limits<uint32_t>::max)()) {
        return false;
    }
    outValue = static_cast<uint32_t>(value);
    return true;
}

// 省略可能な欄。キーが有る時だけ型を検査して target へ読み、無ければ target を保つ。
bool OptionalFloat(const Json& object, const char* key, float& target) {
    const auto member = object.find(key);
    if (member == object.end())
        return true;
    if (!member->is_number())
        return false;
    target = member->get<float>();
    return true;
}

bool OptionalBool(const Json& object, const char* key, bool& target) {
    const auto member = object.find(key);
    if (member == object.end())
        return true;
    if (!member->is_boolean())
        return false;
    target = member->get<bool>();
    return true;
}

bool OptionalString(const Json& object, const char* key, std::string& target) {
    const auto member = object.find(key);
    if (member == object.end())
        return true;
    if (!member->is_string())
        return false;
    target = member->get<std::string>();
    return true;
}

bool OptionalGuid(const Json& object, const char* key, AssetGuid& target) {
    return OptionalString(object, key, target.value);
}

bool OptionalUint32(const Json& object, const char* key, uint32_t& target) {
    const auto member = object.find(key);
    return member == object.end() || ReadUint32(*member, target);
}

bool OptionalVec3(const Json& object, const char* key, MATH::Vec3& target) {
    const auto member = object.find(key);
    return member == object.end() || ReadVec3(*member, target);
}

bool OptionalVec4(const Json& object, const char* key, MATH::Vec4& target) {
    const auto member = object.find(key);
    return member == object.end() || ReadVec4(*member, target);
}

template <class E, size_t N>
bool OptionalEnum(const Json& object, const char* key, const TEXT::EnumName<E> (&names)[N], E& target) {
    const auto member = object.find(key);
    return member == object.end() ||
           (member->is_string() && TEXT::TryParseEnum(names, member->get_ref<const std::string&>(), target));
}

// 必須の欄。キーが無ければ失敗する。
bool RequiredFloat(const Json& object, const char* key, float& target) {
    return object.contains(key) && OptionalFloat(object, key, target);
}

bool RequiredBool(const Json& object, const char* key, bool& target) {
    return object.contains(key) && OptionalBool(object, key, target);
}

bool RequiredString(const Json& object, const char* key, std::string& target) {
    return object.contains(key) && OptionalString(object, key, target);
}

bool RequiredGuid(const Json& object, const char* key, AssetGuid& target) {
    return object.contains(key) && OptionalGuid(object, key, target);
}

bool RequiredUint32(const Json& object, const char* key, uint32_t& target) {
    return object.contains(key) && OptionalUint32(object, key, target);
}

bool RequiredVec3(const Json& object, const char* key, MATH::Vec3& target) {
    return object.contains(key) && OptionalVec3(object, key, target);
}

bool RequiredVec4(const Json& object, const char* key, MATH::Vec4& target) {
    return object.contains(key) && OptionalVec4(object, key, target);
}

template <class E, size_t N>
bool RequiredEnum(const Json& object, const char* key, const TEXT::EnumName<E> (&names)[N], E& target) {
    return object.contains(key) && OptionalEnum(object, key, names, target);
}

// 省略可能な配列。キーが有る時は上限以内の配列であること。無い時と不正な時は nullptr。
const Json* OptionalArray(const Json& object, const char* key, size_t maximumCount, bool& valid) {
    const auto member = object.find(key);
    valid = member == object.end() || (member->is_array() && member->size() <= maximumCount);
    return valid && member != object.end() ? &*member : nullptr;
}

// 配列の各要素を reader で読み、outItems へ追加する。
template <class T, class Reader>
bool ReadItems(const Json& items, std::vector<T>& outItems, Reader&& reader) {
    for (const Json& item : items) {
        T value{};
        if (!reader(item, value))
            return false;
        outItems.push_back(std::move(value));
    }
    return true;
}

bool ReadAttachment(const Json& node, VfxAttachmentData& attachment) {
    return node.is_object() && RequiredString(node, "id", attachment.id) &&
           RequiredEnum(node, "mode", kVfxAttachmentModeNames, attachment.mode) &&
           RequiredEnum(node, "follow", kVfxFollowModeNames, attachment.follow) &&
           OptionalString(node, "socketName", attachment.socketName) &&
           (!node.contains("offset") || ReadTransform(node["offset"], attachment.offset));
}

bool ReadSpecialOutput(const Json& node, VfxSpecialOutputData& output) {
    if (!node.is_object() || !RequiredString(node, "id", output.id) ||
        !RequiredEnum(node, "type", kVfxSpecialOutputTypeNames, output.type) ||
        !OptionalBool(node, "enabled", output.enabled)) {
        return false;
    }
    if (const auto profile = node.find("weaponPathDistortion"); profile != node.end()) {
        auto& value = output.weaponPathDistortion;
        if (!profile->is_object() || !OptionalFloat(*profile, "distortion", value.distortion) ||
            !OptionalFloat(*profile, "blur", value.blur) || !OptionalFloat(*profile, "opacity", value.opacity) ||
            !OptionalFloat(*profile, "emissive", value.emissive) || !OptionalFloat(*profile, "flow", value.flow) ||
            !OptionalFloat(*profile, "width", value.width) ||
            !OptionalFloat(*profile, "lifetimeSeconds", value.lifetimeSeconds) ||
            !OptionalVec4(*profile, "bodyColor", value.bodyColor) ||
            !OptionalVec4(*profile, "edgeColor", value.edgeColor)) {
            return false;
        }
    }
    if (const auto profile = node.find("screenImpulse"); profile != node.end()) {
        auto& value = output.screenImpulse;
        if (!profile->is_object() || !OptionalFloat(*profile, "durationSeconds", value.durationSeconds) ||
            !OptionalFloat(*profile, "maxRadius", value.maxRadius) ||
            !OptionalFloat(*profile, "ringThickness", value.ringThickness) ||
            !OptionalFloat(*profile, "distortion", value.distortion) ||
            !OptionalFloat(*profile, "chromaticAberration", value.chromaticAberration) ||
            !OptionalFloat(*profile, "flash", value.flash) || !OptionalFloat(*profile, "intensity", value.intensity) ||
            !OptionalVec4(*profile, "color", value.color) ||
            !OptionalFloat(*profile, "waveFrequency", value.waveFrequency) ||
            !OptionalFloat(*profile, "waveAmplitude", value.waveAmplitude) ||
            !OptionalFloat(*profile, "waveDamping", value.waveDamping) ||
            !OptionalUint32(*profile, "waveCount", value.waveCount)) {
            return false;
        }
    }
    if (const auto profile = node.find("skinnedAfterimage"); profile != node.end()) {
        auto& value = output.skinnedAfterimage;
        if (!profile->is_object() || !OptionalUint32(*profile, "sampleCount", value.sampleCount) ||
            !OptionalFloat(*profile, "lifetimeSeconds", value.lifetimeSeconds) ||
            !OptionalFloat(*profile, "opacity", value.opacity) || !OptionalFloat(*profile, "fresnel", value.fresnel) ||
            !OptionalFloat(*profile, "dissolve", value.dissolve) ||
            !OptionalFloat(*profile, "shellOffset", value.shellOffset) ||
            !OptionalVec4(*profile, "color", value.color) || !OptionalFloat(*profile, "intensity", value.intensity)) {
            return false;
        }
    }
    if (const auto profile = node.find("light"); profile != node.end()) {
        auto& value = output.light;
        if (!profile->is_object() || !OptionalVec4(*profile, "color", value.color) ||
            !OptionalFloat(*profile, "intensity", value.intensity) || !OptionalFloat(*profile, "range", value.range) ||
            !OptionalFloat(*profile, "sourceRadius", value.sourceRadius) ||
            !OptionalFloat(*profile, "flickerFrequency", value.flickerFrequency) ||
            !OptionalFloat(*profile, "flickerAmplitude", value.flickerAmplitude) ||
            !OptionalUint32(*profile, "priority", value.priority) ||
            !OptionalBool(*profile, "castsShadows", value.castsShadows)) {
            return false;
        }
    }
    return true;
}

bool ReadConnectTarget(const Json& node, VfxConnectTargetData& target) {
    return node.is_object() && RequiredEnum(node, "mode", kVfxConnectTargetModeNames, target.mode) &&
           OptionalString(node, "targetLayerId", target.targetLayerId) &&
           OptionalVec3(node, "localPosition", target.localPosition) && OptionalFloat(node, "pivot", target.pivot) &&
           OptionalFloat(node, "lengthScale", target.lengthScale) &&
           OptionalFloat(node, "startOffset", target.startOffset) &&
           OptionalFloat(node, "endOffset", target.endOffset) && OptionalBool(node, "orient", target.orient) &&
           OptionalBool(node, "stretch", target.stretch);
}

bool ReadMeshOutput(const Json& node, VfxMeshOutputData& mesh) {
    return node.is_object() && OptionalBool(node, "enabled", mesh.enabled) &&
           OptionalGuid(node, "meshAssetGuid", mesh.meshAssetGuid) &&
           OptionalBool(node, "castShadows", mesh.castShadows) &&
           OptionalBool(node, "receiveShadow", mesh.receiveShadow);
}

bool ReadMatte(const Json& node, VfxTrackMatteData& matte) {
    return node.is_object() && OptionalString(node, "sourceLayerId", matte.sourceLayerId) &&
           OptionalBool(node, "luminance", matte.luminance) && OptionalBool(node, "inverted", matte.inverted) &&
           OptionalBool(node, "sourceOnly", matte.sourceOnly);
}

bool ReadHistory(const Json& node, VfxHistoryData& history) {
    return node.is_object() && RequiredEnum(node, "mode", kVfxHistoryModeNames, history.mode) &&
           RequiredFloat(node, "duration", history.duration) && RequiredUint32(node, "samples", history.samples) &&
           RequiredFloat(node, "breakDistance", history.breakDistance) &&
           OptionalFloat(node, "tailWidth", history.tailWidth) && OptionalFloat(node, "fadePower", history.fadePower) &&
           OptionalVec4(node, "tailColor", history.tailColor) && IsValidVfxHistory(history);
}

bool ReadSpawn(const Json& node, VfxSpawnModuleData& spawn) {
    if (!RequiredFloat(node, "ratePerSecond", spawn.ratePerSecond) ||
        !RequiredEnum(node, "capacityMode", kVfxParticleCapacityModeNames, spawn.capacityMode) ||
        !RequiredUint32(node, "maximumParticles", spawn.maximumParticles)) {
        return false;
    }
    const auto timeline = node.find("burstTimeline");
    if (timeline == node.end() || !timeline->is_array() || timeline->size() > kMaximumVfxProgramBurstKeys) {
        return false;
    }
    for (const Json& item : *timeline) {
        VfxBurstKeyData key{};
        if (!item.is_object() || !RequiredString(item, "id", key.id) || !RequiredFloat(item, "time", key.time) ||
            !RequiredUint32(item, "count", key.count)) {
            return false;
        }
        spawn.burstTimeline.push_back(std::move(key));
    }
    std::sort(spawn.burstTimeline.begin(), spawn.burstTimeline.end(),
              [](const VfxBurstKeyData& left, const VfxBurstKeyData& right) {
                  if (left.time != right.time) {
                      return left.time < right.time;
                  }
                  return left.id < right.id;
              });
    return true;
}

bool ReadShape(const Json& node, VfxShapeModuleData& shape) {
    // circleArcDegrees は放出角度範囲。省略時は既定 360 (全周)。
    return RequiredEnum(node, "type", kVfxShapeTypeNames, shape.type) && node.contains("radius") &&
           ReadDistribution(node["radius"], shape.radius) &&
           RequiredFloat(node, "coneAngleDegrees", shape.coneAngleDegrees) &&
           OptionalVec3(node, "boxHalfExtents", shape.boxHalfExtents) &&
           OptionalFloat(node, "cylinderHeight", shape.cylinderHeight) &&
           OptionalFloat(node, "circleInnerRadius", shape.circleInnerRadius) &&
           OptionalBool(node, "circleEmitOnEdge", shape.circleEmitOnEdge) &&
           OptionalFloat(node, "circleArcDegrees", shape.circleArcDegrees) &&
           OptionalFloat(node, "edgeLength", shape.edgeLength);
}

bool ReadForces(const Json& node, VfxForcesModuleData& forces) {
    if (!RequiredVec3(node, "gravity", forces.gravity) || !RequiredFloat(node, "drag", forces.drag) ||
        !OptionalBool(node, "collisionPlaneEnabled", forces.collisionPlaneEnabled) ||
        !OptionalFloat(node, "collisionPlaneHeight", forces.collisionPlaneHeight) ||
        !OptionalFloat(node, "restitution", forces.restitution) || !OptionalFloat(node, "friction", forces.friction)) {
        return false;
    }
    const auto field = node.find("field");
    if (field == node.end())
        return true;
    auto& value = forces.field;
    return field->is_object() && RequiredVec3(*field, "center", value.center) &&
           RequiredVec3(*field, "axis", value.axis) && RequiredFloat(*field, "radius", value.radius) &&
           RequiredFloat(*field, "vortex", value.vortex) && RequiredFloat(*field, "turbulence", value.turbulence) &&
           RequiredFloat(*field, "frequency", value.frequency) && RequiredFloat(*field, "evolution", value.evolution) &&
           RequiredFloat(*field, "attraction", value.attraction);
}

bool ReadRenderCommon(const Json& node, VfxRenderCommonModuleData& render) {
    // flowMap / detailNoise は 2 つ目のノイズ場（省略可能）。
    return RequiredGuid(node, "materialAssetGuid", render.materialAssetGuid) &&
           RequiredEnum(node, "blendMode", kVfxBlendModeNames, render.blendMode) &&
           OptionalEnum(node, "postProcessMode", kVfxPostProcessModeNames, render.postProcessMode) &&
           RequiredEnum(node, "materialMode", kVfxMaterialModeNames, render.materialMode) &&
           RequiredGuid(node, "colorTextureAssetGuid", render.colorTextureAssetGuid) &&
           OptionalGuid(node, "maskTextureAssetGuid", render.maskTextureAssetGuid) &&
           OptionalGuid(node, "noiseTextureAssetGuid", render.noiseTextureAssetGuid) &&
           OptionalGuid(node, "flowMapTextureAssetGuid", render.flowMapTextureAssetGuid) &&
           OptionalGuid(node, "detailNoiseTextureAssetGuid", render.detailNoiseTextureAssetGuid) &&
           RequiredEnum(node, "depthMode", kVfxParticleDepthModeNames, render.depthMode) &&
           RequiredFloat(node, "softIntersectionDistance", render.softIntersectionDistance) &&
           RequiredFloat(node, "depthBias", render.depthBias) &&
           OptionalUint32(node, "flipbookColumns", render.flipbookColumns) &&
           OptionalUint32(node, "flipbookRows", render.flipbookRows) &&
           OptionalUint32(node, "flipbookFrameCount", render.flipbookFrameCount) &&
           OptionalBool(node, "flipbookLoop", render.flipbookLoop) &&
           OptionalBool(node, "flipbookRandomStart", render.flipbookRandomStart) &&
           OptionalFloat(node, "flipbookFramesPerSecond", render.flipbookFramesPerSecond);
}

bool ReadRenderShape(const Json& node, VfxRenderShapeModuleData& shape) {
    return RequiredEnum(node, "type", kVfxRenderShapeTypeNames, shape.type) &&
           RequiredEnum(node, "billboardMode", kVfxBillboardModeNames, shape.billboardMode) &&
           OptionalEnum(node, "billboardAxis", kVfxBillboardAxisNames, shape.billboardAxis) &&
           OptionalUint32(node, "ribbonSegmentCount", shape.ribbonSegmentCount) &&
           OptionalFloat(node, "ribbonWidth", shape.ribbonWidth) &&
           OptionalFloat(node, "ringOuterRadius", shape.ringOuterRadius) &&
           OptionalFloat(node, "ringInnerRadius", shape.ringInnerRadius) &&
           OptionalUint32(node, "ringSegmentCount", shape.ringSegmentCount) &&
           OptionalFloat(node, "ringStartAngleDegrees", shape.ringStartAngleDegrees) &&
           OptionalFloat(node, "ringEndAngleDegrees", shape.ringEndAngleDegrees) &&
           OptionalEnum(node, "uvMode", kVfxShapeUvModeNames, shape.uvMode) &&
           OptionalFloat(node, "uvTileLength", shape.uvTileLength) &&
           OptionalFloat(node, "uvTilingU", shape.uvTilingU) && OptionalFloat(node, "uvTilingV", shape.uvTilingV) &&
           OptionalFloat(node, "uvRotationDegrees", shape.uvRotationDegrees) &&
           OptionalBool(node, "uvSwapAxes", shape.uvSwapAxes) && OptionalFloat(node, "taperStart", shape.taperStart) &&
           OptionalFloat(node, "taperEnd", shape.taperEnd) &&
           OptionalFloat(node, "cylinderTopRadius", shape.cylinderTopRadius) &&
           OptionalFloat(node, "cylinderBottomRadius", shape.cylinderBottomRadius) &&
           OptionalFloat(node, "coneBaseRadius", shape.coneBaseRadius) &&
           OptionalFloat(node, "bendAmount", shape.bendAmount) &&
           OptionalFloat(node, "twistDegrees", shape.twistDegrees) &&
           OptionalFloat(node, "torusMajorRadius", shape.torusMajorRadius) &&
           OptionalFloat(node, "torusMinorRadius", shape.torusMinorRadius) &&
           OptionalFloat(node, "capsuleRadius", shape.capsuleRadius);
}

bool ReadMaterialEffect(const Json& node, MaterialEffectStep& step) {
    if (!node.is_object())
        return false;
    if (const auto mask = node.find("mask"); mask != node.end()) {
        if (!mask->is_number_integer() || mask->get<int>() < 0 || mask->get<int>() > 5)
            return false;
        step.mask = static_cast<MaterialEffectMask>(mask->get<int>());
    }
    const auto kind = node.find("kind");
    if (kind == node.end() || !kind->is_string() ||
        !ParseMaterialEffectKind(kind->get_ref<const std::string&>(), step.kind)) {
        return false;
    }
    if (const auto target = node.find("textureTarget");
        target != node.end() &&
        (!target->is_string() ||
         !ParseMaterialEffectTextureTarget(target->get_ref<const std::string&>(), step.textureTarget))) {
        return false;
    }
    if (!RequiredString(node, "id", step.id) || !RequiredBool(node, "enabled", step.enabled) ||
        !OptionalFloat(node, "mix", step.mix) || !OptionalBool(node, "maskInvert", step.maskInvert) ||
        !OptionalEnum(node, "space", kMaterialEffectSpaceNames, step.space) ||
        !OptionalVec4(node, "maskParameters", step.maskParameters) ||
        !RequiredVec4(node, "parameters", step.parameters) || !OptionalVec4(node, "parameters2", step.parameters2) ||
        !RequiredVec4(node, "colorA", step.colorA) || !RequiredVec4(node, "colorB", step.colorB)) {
        return false;
    }
    // 多段グラデーションのストップ。無い資産は colorA/colorB のまま。
    bool valid = false;
    const Json* stops = OptionalArray(node, "colorStops", kMaximumMaterialEffectColorStops, valid);
    return valid && (stops == nullptr || ReadItems(*stops, step.colorStops, [](const Json& entry, auto& stop) {
                         return entry.is_object() && RequiredString(entry, "id", stop.id) &&
                                RequiredFloat(entry, "time", stop.time) && RequiredVec4(entry, "color", stop.color);
                     }));
}

bool ReadInputLink(const Json& node, VfxInputLinkData& link) {
    if (!node.is_object() || !RequiredString(node, "id", link.id) || !RequiredString(node, "inputId", link.inputId) ||
        !RequiredFloat(node, "scale", link.scale) || !RequiredFloat(node, "bias", link.bias)) {
        return false;
    }
    const auto target = node.find("target");
    return target != node.end() && target->is_object() && RequiredString(*target, "emitterId", link.target.emitterId) &&
           RequiredString(*target, "moduleId", link.target.moduleId) &&
           RequiredString(*target, "propertyId", link.target.propertyId);
}

bool ReadParameterTrack(const Json& node, VfxParameterTrackData& track) {
    return node.is_object() && RequiredString(node, "id", track.id) &&
           RequiredString(node, "targetId", track.targetId) && RequiredString(node, "propertyId", track.propertyId) &&
           node.contains("curve") && ReadCurve(node["curve"], track.curve);
}

bool ReadModules(const Json& modules, VfxEmitterData& emitter) {
    constexpr const char* kModuleNames[] = {
        "basic", "emitterTransform", "attachment", "spawn",        "shape",       "position", "rotation",
        "scale", "forces",           "color",      "renderCommon", "renderShape",
    };
    for (const char* name : kModuleNames) {
        if (!modules.contains(name) || !modules[name].is_object() || !modules[name].contains("id") ||
            !modules[name]["id"].is_string()) {
            return false;
        }
    }
    const Json& basic = modules["basic"];
    const Json& transform = modules["emitterTransform"];
    const Json& position = modules["position"];
    const Json& rotation = modules["rotation"];
    const Json& scale = modules["scale"];
    const Json& color = modules["color"];
    emitter.basic.id = basic["id"].get<std::string>();
    emitter.emitterTransform.id = transform["id"].get<std::string>();
    emitter.spawn.id = modules["spawn"]["id"].get<std::string>();
    emitter.shape.id = modules["shape"]["id"].get<std::string>();
    emitter.position.id = position["id"].get<std::string>();
    emitter.rotation.id = rotation["id"].get<std::string>();
    emitter.scale.id = scale["id"].get<std::string>();
    emitter.forces.id = modules["forces"]["id"].get<std::string>();
    emitter.color.id = color["id"].get<std::string>();
    emitter.renderCommon.id = modules["renderCommon"]["id"].get<std::string>();
    emitter.renderShape.id = modules["renderShape"]["id"].get<std::string>();

    return RequiredBool(basic, "enabled", emitter.basic.enabled) && basic.contains("lifetime") &&
           ReadDistribution(basic["lifetime"], emitter.basic.lifetime) &&
           OptionalBool(basic, "localSimulation", emitter.basic.localSimulation) && transform.contains("transform") &&
           ReadTransform(transform["transform"], emitter.emitterTransform.transform) &&
           ReadAttachment(modules["attachment"], emitter.attachment) && ReadSpawn(modules["spawn"], emitter.spawn) &&
           ReadShape(modules["shape"], emitter.shape) &&
           OptionalBool(position, "orientAlongPath", emitter.position.orientAlongPath) &&
           position.contains("radialSpeed") &&
           ReadDistribution(position["radialSpeed"], emitter.position.radialSpeed) && position.contains("value") &&
           ReadAnimatedVec3(position["value"], emitter.position.value) && rotation.contains("eulerDegrees") &&
           ReadAnimatedVec3(rotation["eulerDegrees"], emitter.rotation.eulerDegrees) && scale.contains("value") &&
           ReadAnimatedVec3(scale["value"], emitter.scale.value) && ReadForces(modules["forces"], emitter.forces) &&
           color.contains("value") && ReadAnimatedColor(color["value"], emitter.color.value) &&
           ReadRenderCommon(modules["renderCommon"], emitter.renderCommon) &&
           ReadRenderShape(modules["renderShape"], emitter.renderShape);
}

bool ReadAnimationTiming(const Json& node, VfxAnimationTiming& timing) {
    return node.is_object() && RequiredFloat(node, "sourceDuration", timing.sourceDuration) &&
           RequiredFloat(node, "offset", timing.offset) && RequiredFloat(node, "rate", timing.rate);
}

// 省略可能な配列欄を reader で読む。
template <class T, class Reader>
bool ReadOptionalItems(const Json& object, const char* key, size_t maximumCount, std::vector<T>& outItems,
                       Reader&& reader) {
    bool valid = false;
    const Json* items = OptionalArray(object, key, maximumCount, valid);
    return valid && (items == nullptr || ReadItems(*items, outItems, reader));
}
} // namespace

bool ReadVfxSourceEmitter(const Json& node, VfxEmitterData& outEmitter) {
    if (!node.is_object() || !node.contains("modules") || !node["modules"].is_object() ||
        !node.contains("inputLinks") || !node["inputLinks"].is_array()) {
        return false;
    }
    VfxEmitterData emitter{};
    const auto optionalObject = [&node](const char* key, auto&& reader, auto& target) {
        const auto member = node.find(key);
        return member == node.end() || reader(*member, target);
    };
    if (!RequiredString(node, "id", emitter.id) || !RequiredString(node, "name", emitter.name) ||
        !RequiredString(node, "parentEmitterId", emitter.parentEmitterId) ||
        !RequiredUint32(node, "order", emitter.order) || !OptionalFloat(node, "startDelay", emitter.startDelay) ||
        !OptionalEnum(node, "kind", kVfxLayerKindNames, emitter.kind) ||
        !OptionalFloat(node, "activeDuration", emitter.activeDuration) ||
        !OptionalVec3(node, "anchor", emitter.anchor) ||
        !optionalObject("connectTarget", ReadConnectTarget, emitter.connectTarget) ||
        !optionalObject("meshOutput", ReadMeshOutput, emitter.meshOutput) ||
        !optionalObject("matte", ReadMatte, emitter.matte) ||
        !optionalObject("history", ReadHistory, emitter.history) ||
        !optionalObject("animationTiming", ReadAnimationTiming, emitter.animationTiming) ||
        !ReadModules(node["modules"], emitter) || !ReadItems(node["inputLinks"], emitter.inputLinks, ReadInputLink) ||
        !ReadOptionalItems(node, "specialOutputs", kMaximumVfxProgramSpecialOutputs, emitter.specialOutputs,
                           ReadSpecialOutput) ||
        !ReadOptionalItems(node, "effects", kMaximumVfxEffectsPerLayer, emitter.effects, ReadMaterialEffect) ||
        !ReadOptionalItems(node, "parameterTracks", kMaxVfxParameterTracks, emitter.parameterTracks,
                           ReadParameterTrack)) {
        return false;
    }
    outEmitter = std::move(emitter);
    return true;
}

} // namespace HIKARI::VFX::SERIALIZATION::INTERNAL
