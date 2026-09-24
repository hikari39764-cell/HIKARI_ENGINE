#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJsonInternal.h"

#include <algorithm>
#include <utility>

#include "Assets/Vfx/HIKARI_VfxValidation.h"

namespace HIKARI::VFX::SERIALIZATION::INTERNAL {
using Json = nlohmann::json;
bool ReadVec4(const Json& node, MATH::Vec4& outValue) {
    if (!node.is_array() || node.size() != 4u || !node[0].is_number() || !node[1].is_number() || !node[2].is_number() ||
        !node[3].is_number()) {
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
namespace {
using Json = nlohmann::json;

template <class Distribution, class ReadNumber>
bool ReadDistributionData(const Json& node, Distribution& outValue, ReadNumber&& readNumber) {
    if (!node.is_object())
        return false;
    Distribution value{};
    const std::string mode = node.value("mode", std::string{});
    if (!TryParseVfxDistributionMode(mode, value.mode) || !node.contains("minimum") || !node.contains("maximum") ||
        !readNumber(node["minimum"], value.minimum) || !readNumber(node["maximum"], value.maximum) ||
        !node.contains("randomGroup") || !node["randomGroup"].is_number_unsigned()) {
        return false;
    }
    value.randomGroup = node["randomGroup"].get<uint32_t>();
    outValue = value;
    return true;
}

bool ReadVec3Curve(const Json& node, VfxVec3CurveData& outCurve) {
    VfxVec3CurveData curve{};
    if (!node.is_object() || !node.contains("x") || !node.contains("y") || !node.contains("z") ||
        !ReadCurve(node["x"], curve.x) || !ReadCurve(node["y"], curve.y) || !ReadCurve(node["z"], curve.z)) {
        return false;
    }
    outCurve = std::move(curve);
    return true;
}

bool ReadColorCurve(const Json& node, VfxColorCurveData& outCurve) {
    VfxColorCurveData curve{};
    if (!node.is_object() || !node.contains("r") || !node.contains("g") || !node.contains("b") || !node.contains("a") ||
        !ReadCurve(node["r"], curve.r) || !ReadCurve(node["g"], curve.g) || !ReadCurve(node["b"], curve.b) ||
        !ReadCurve(node["a"], curve.a)) {
        return false;
    }
    outCurve = std::move(curve);
    return true;
}

bool ReadGradient(const Json& node, VfxColorGradientData& outGradient) {
    if (!node.is_object() || !node.contains("stops") || !node["stops"].is_array()) {
        return false;
    }
    VfxColorGradientData gradient{};
    for (const Json& item : node["stops"]) {
        VfxColorStopData stop{};
        if (!item.is_object() || !item.contains("id") || !item["id"].is_string() || !item.contains("time") ||
            !item["time"].is_number() || !item.contains("color") || !ReadVec4(item["color"], stop.color)) {
            return false;
        }
        stop.id = item["id"].get<std::string>();
        stop.time = item["time"].get<float>();
        gradient.stops.push_back(std::move(stop));
    }
    outGradient = std::move(gradient);
    return true;
}

template <class T>
bool ReadEasing(const Json& node, VfxEasingData<T>& outValue) {
    VfxEasingData<T> value{};
    if (!node.is_object() || !node.contains("start") || !node.contains("middle") || !node.contains("end") ||
        !ReadDistribution(node["start"], value.start) || !ReadDistribution(node["middle"], value.middle) ||
        !ReadDistribution(node["end"], value.end) ||
        !TryParseVfxEasingFunction(node.value("function", std::string{}), value.function) ||
        !node.contains("useMiddle") || !node["useMiddle"].is_boolean()) {
        return false;
    }
    value.useMiddle = node["useMiddle"].get<bool>();
    outValue = std::move(value);
    return true;
}

std::string FirstValidationError(const std::vector<VfxDiagnostic>& diagnostics) {
    const auto found = std::find_if(diagnostics.begin(), diagnostics.end(), [](const VfxDiagnostic& diagnostic) {
        return diagnostic.severity == VfxDiagnosticSeverity::Error;
    });
    return found == diagnostics.end() ? std::string{} : found->code + ": " + found->message;
}
} // namespace

bool ReadCurve(const Json& node, VfxCurveData& outCurve) {
    if (!node.is_object() || !node.contains("randomOffset") || !node.contains("keys") || !node["keys"].is_array()) {
        return false;
    }
    VfxCurveData curve{};
    if (!TryParseVfxCurveInterpolation(node.value("interpolation", std::string{}), curve.interpolation) ||
        !ReadDistribution(node["randomOffset"], curve.randomOffset)) {
        return false;
    }
    for (const Json& item : node["keys"]) {
        if (!item.is_object() || !item.contains("id") || !item["id"].is_string() || !item.contains("time") ||
            !item["time"].is_number() || !item.contains("value") || !item["value"].is_number() ||
            !item.contains("inTangent") || !item["inTangent"].is_number() || !item.contains("outTangent") ||
            !item["outTangent"].is_number()) {
            return false;
        }
        curve.keys.push_back({
            item["id"].get<std::string>(),
            item["time"].get<float>(),
            item["value"].get<float>(),
            item["inTangent"].get<float>(),
            item["outTangent"].get<float>(),
        });
    }
    outCurve = std::move(curve);
    return true;
}

bool ReadVec3(const Json& node, MATH::Vec3& outValue) {
    if (!node.is_array() || node.size() != 3u || !node[0].is_number() || !node[1].is_number() || !node[2].is_number()) {
        return false;
    }
    outValue = {
        node[0].get<float>(),
        node[1].get<float>(),
        node[2].get<float>(),
    };
    return true;
}

bool ReadValue(const Json& node, VfxValueType type, VfxValue& outValue) {
    VfxValue value = MakeDefaultVfxValue(type);
    switch (type) {
    case VfxValueType::Bool:
        if (!node.is_boolean())
            return false;
        value.boolean = node.get<bool>();
        break;
    case VfxValueType::Int:
        if (!node.is_number_integer())
            return false;
        value.integer = node.get<int64_t>();
        break;
    case VfxValueType::Float:
        if (!node.is_number())
            return false;
        value.numeric.x = node.get<float>();
        break;
    case VfxValueType::Float2:
        if (!node.is_array() || node.size() != 2u || !node[0].is_number() || !node[1].is_number()) {
            return false;
        }
        value.numeric.x = node[0].get<float>();
        value.numeric.y = node[1].get<float>();
        break;
    case VfxValueType::Float3: {
        MATH::Vec3 numeric{};
        if (!ReadVec3(node, numeric))
            return false;
        value.numeric = {numeric.x, numeric.y, numeric.z, 0.0f};
        break;
    }
    case VfxValueType::Float4:
    case VfxValueType::Color:
        if (!ReadVec4(node, value.numeric))
            return false;
        break;
    }
    outValue = value;
    return true;
}

bool ReadDistribution(const Json& node, VfxFloatDistributionData& outValue) {
    return ReadDistributionData(node, outValue, [](const Json& value, float& output) {
        if (!value.is_number())
            return false;
        output = value.get<float>();
        return true;
    });
}

bool ReadDistribution(const Json& node, VfxVec3DistributionData& outValue) {
    return ReadDistributionData(node, outValue, ReadVec3);
}

bool ReadDistribution(const Json& node, VfxColorDistributionData& outValue) {
    return ReadDistributionData(node, outValue, ReadVec4);
}

bool ReadAnimatedVec3(const Json& node, VfxAnimatedVec3Data& outValue) {
    VfxAnimatedVec3Data value{};
    if (!node.is_object() || !TryParseVfxAnimatedValueMode(node.value("mode", std::string{}), value.mode) ||
        !node.contains("fixed") || !ReadDistribution(node["fixed"], value.fixed) || !node.contains("pva") ||
        !node["pva"].is_object() || !node["pva"].contains("initial") || !node["pva"].contains("velocity") ||
        !node["pva"].contains("acceleration") || !ReadDistribution(node["pva"]["initial"], value.pva.initial) ||
        !ReadDistribution(node["pva"]["velocity"], value.pva.velocity) ||
        !ReadDistribution(node["pva"]["acceleration"], value.pva.acceleration) || !node.contains("easing") ||
        !ReadEasing(node["easing"], value.easing) || !node.contains("curve") ||
        !ReadVec3Curve(node["curve"], value.curve)) {
        return false;
    }
    outValue = std::move(value);
    return true;
}

bool ReadAnimatedColor(const Json& node, VfxAnimatedColorData& outValue) {
    VfxAnimatedColorData value{};
    if (!node.is_object() || !TryParseVfxAnimatedColorMode(node.value("mode", std::string{}), value.mode) ||
        !node.contains("fixed") || !ReadDistribution(node["fixed"], value.fixed) || !node.contains("easing") ||
        !ReadEasing(node["easing"], value.easing) || !node.contains("curve") ||
        !ReadColorCurve(node["curve"], value.curve) || !node.contains("gradient") ||
        !ReadGradient(node["gradient"], value.gradient)) {
        return false;
    }
    outValue = std::move(value);
    return true;
}

bool ReadTransform(const Json& node, VfxTransformData& outValue) {
    VfxTransformData value{};
    if (!node.is_object() || !node.contains("position") || !node.contains("rotationEulerDegrees") ||
        !node.contains("scale") || !ReadVec3(node["position"], value.position) ||
        !ReadVec3(node["rotationEulerDegrees"], value.rotationEulerDegrees) || !ReadVec3(node["scale"], value.scale)) {
        return false;
    }
    outValue = value;
    return true;
}

bool ValidateVfxSourceAsset(const VfxAssetData& asset, std::string& outError) {
    const std::vector<VfxDiagnostic> diagnostics = ValidateVfxAsset(asset);
    if (!HasVfxErrors(diagnostics)) {
        return true;
    }
    outError = FirstValidationError(diagnostics);
    return false;
}

bool ReadVfxSourceDocument(const Json& root, VfxAssetData& outAsset, std::string& outError) {
    if (!root.is_object() || !root.contains("version") || !root["version"].is_number_unsigned() ||
        !root.contains("name") || !root["name"].is_string() || !root.contains("durationSeconds") ||
        !root["durationSeconds"].is_number() || !root.contains("loop") || !root["loop"].is_boolean() ||
        !root.contains("inputs") || !root["inputs"].is_array() || !root.contains("emitters") ||
        !root["emitters"].is_array()) {
        outError = "invalid VFX source document";
        return false;
    }

    const uint32_t sourceVersion = root["version"].get<uint32_t>();
    if (sourceVersion < 2u || sourceVersion > kVfxSourceVersion) {
        outError = "unsupported VFX source version";
        return false;
    }

    VfxAssetData asset{};
    asset.version = kVfxSourceVersion;
    asset.name = root["name"].get<std::string>();
    asset.durationSeconds = root["durationSeconds"].get<float>();
    asset.loop = root["loop"].get<bool>();
    for (const Json& item : root["inputs"]) {
        if (!item.is_object() || !item.contains("id") || !item["id"].is_string() || !item.contains("name") ||
            !item["name"].is_string() || !item.contains("defaultValue")) {
            outError = "invalid VFX exposed input";
            return false;
        }
        VfxExposedInputData input{};
        input.id = item["id"].get<std::string>();
        input.name = item["name"].get<std::string>();
        if (!TryParseVfxValueType(item.value("type", std::string{}), input.type) ||
            !ReadValue(item["defaultValue"], input.type, input.defaultValue)) {
            outError = "invalid VFX exposed input value";
            return false;
        }
        asset.inputs.push_back(std::move(input));
    }
    for (const Json& item : root["emitters"]) {
        VfxEmitterData emitter{};
        if (!ReadVfxSourceEmitter(item, sourceVersion, emitter)) {
            outError = "invalid VFX emitter document";
            return false;
        }
        asset.emitters.push_back(std::move(emitter));
    }

    if (!ValidateVfxSourceAsset(asset, outError)) {
        return false;
    }
    outAsset = std::move(asset);
    outError.clear();
    return true;
}

} // namespace HIKARI::VFX::SERIALIZATION::INTERNAL
