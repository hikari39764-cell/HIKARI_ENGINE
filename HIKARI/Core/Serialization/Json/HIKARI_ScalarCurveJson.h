#pragma once
#include "Core/Animation/HIKARI_ScalarCurve.h"
#include <json.hpp>
#include <stdexcept>

namespace HIKARI {
// UI と演出のキーは同じ保存形式。省略した形状は従来と同じ 1/3 の重みになる。
inline nlohmann::json WriteScalarCurveKeyJson(const ScalarCurveKey& key) {
    auto result =
        nlohmann::json::array({key.time, key.value, key.inSlope, key.outSlope, static_cast<int>(key.interpolation)});
    if (key.shape != ScalarCurveShape{})
        result.push_back({{"inWeight", key.shape.inWeight},
                          {"outWeight", key.shape.outWeight},
                          {"tangent", TEXT::EnumToName(kScalarTangentNames, key.shape.tangent)}});
    return result;
}
inline ScalarCurveKey ReadScalarCurveKeyJson(const nlohmann::json& value) {
    if (!value.is_array() || value.size() < 4 || value.size() > 6)
        throw std::runtime_error("Invalid curve key");
    ScalarCurveKey key{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
    if (value.size() >= 5) {
        const int mode = value[4].get<int>();
        if (mode < 0 || mode > static_cast<int>(ScalarCurveInterpolation::Hold))
            throw std::runtime_error("Invalid curve interpolation");
        key.interpolation = static_cast<ScalarCurveInterpolation>(mode);
    }
    if (value.size() == 6) {
        const auto& shape = value[5];
        key.shape.inWeight = shape.at("inWeight").get<float>();
        key.shape.outWeight = shape.at("outWeight").get<float>();
        if (!TEXT::TryParseEnum(kScalarTangentNames, shape.at("tangent").get<std::string>(), key.shape.tangent))
            throw std::runtime_error("Invalid curve tangent mode");
    }
    return key;
}
} // namespace HIKARI
