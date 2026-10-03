#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonStrict.h"
#include "Assets/Sprite/HIKARI_SpriteAsset.h"

namespace HIKARI::SPRITE::JSON_INTERNAL {

inline bool ReadUint32Value(const nlohmann::json& node, uint32_t& outValue, std::string& outError,
                            std::string_view path) {
    uint64_t value = 0u;
    if (node.is_number_unsigned()) {
        value = node.get<uint64_t>();
    } else if (node.is_number_integer()) {
        const int64_t signedValue = node.get<int64_t>();
        if (signedValue < 0) {
            return JSONSTRICT::Fail(outError, std::string(path) + " must be a non-negative integer");
        }
        value = static_cast<uint64_t>(signedValue);
    } else {
        return JSONSTRICT::Fail(outError, std::string(path) + " must be a non-negative integer");
    }
    if (value > (std::numeric_limits<uint32_t>::max)()) {
        return JSONSTRICT::Fail(outError, std::string(path) + " exceeds uint32 range");
    }
    outValue = static_cast<uint32_t>(value);
    return true;
}

inline bool ReadUintPair(const nlohmann::json& node, uint32_t& outX, uint32_t& outY, std::string& outError,
                         std::string_view path) {
    if (!node.is_array() || node.size() != 2u) {
        return JSONSTRICT::Fail(outError, std::string(path) + " must be an array of two non-negative integers");
    }
    return ReadUint32Value(node[0], outX, outError, std::string(path) + "[0]") &&
           ReadUint32Value(node[1], outY, outError, std::string(path) + "[1]");
}

inline bool ReadFloatPair(const nlohmann::json& node, float& outX, float& outY, std::string& outError,
                          std::string_view path) {
    if (!node.is_array() || node.size() != 2u || !node[0].is_number() || !node[1].is_number()) {
        return JSONSTRICT::Fail(outError, std::string(path) + " must be an array of two finite numbers");
    }
    const double x = node[0].get<double>();
    const double y = node[1].get<double>();
    if (!JSONSTRICT::IsFloatRange(x) || !JSONSTRICT::IsFloatRange(y)) {
        return JSONSTRICT::Fail(outError, std::string(path) + " must contain finite float values");
    }
    outX = static_cast<float>(x);
    outY = static_cast<float>(y);
    return true;
}

inline bool ReadRequiredString(const nlohmann::json& object, const char* key, std::string& outValue,
                               std::string& outError, std::string_view path) {
    if (!object.contains(key)) {
        return JSONSTRICT::Fail(outError, JSONSTRICT::FieldPath(path, key) + " is required");
    }
    return JSONSTRICT::ReadString(object, key, {}, outValue, outError, path);
}

inline bool ReadFrameGeometry(const nlohmann::json& node, SpriteFrame& outFrame, bool requireRotated,
                              std::string& outError, std::string_view path) {
    if (!node.is_object()) {
        return JSONSTRICT::Fail(outError, std::string(path) + " must be an object");
    }
    if (!ReadRequiredString(node, "id", outFrame.id, outError, path) || !node.contains("rect") ||
        !node.at("rect").is_array() || node.at("rect").size() != 4u ||
        !ReadUint32Value(node.at("rect")[0], outFrame.x, outError, std::string(path) + ".rect[0]") ||
        !ReadUint32Value(node.at("rect")[1], outFrame.y, outError, std::string(path) + ".rect[1]") ||
        !ReadUint32Value(node.at("rect")[2], outFrame.width, outError, std::string(path) + ".rect[2]") ||
        !ReadUint32Value(node.at("rect")[3], outFrame.height, outError, std::string(path) + ".rect[3]") ||
        !node.contains("sourceSize") ||
        !ReadUintPair(node.at("sourceSize"), outFrame.sourceWidth, outFrame.sourceHeight, outError,
                      std::string(path) + ".sourceSize") ||
        !node.contains("trimOffset") ||
        !ReadUintPair(node.at("trimOffset"), outFrame.trimX, outFrame.trimY, outError,
                      std::string(path) + ".trimOffset") ||
        !node.contains("pivot") ||
        !ReadFloatPair(node.at("pivot"), outFrame.pivotX, outFrame.pivotY, outError, std::string(path) + ".pivot") ||
        (requireRotated && !node.contains("rotated")) ||
        !JSONSTRICT::ReadBool(node, "rotated", false, outFrame.rotated, outError, path) || !node.contains("empty") ||
        !JSONSTRICT::ReadBool(node, "empty", false, outFrame.empty, outError, path)) {
        if (outError.empty()) {
            return JSONSTRICT::Fail(outError, std::string(path) + " is missing Sprite frame geometry");
        }
        return false;
    }
    return true;
}

} // namespace HIKARI::SPRITE::JSON_INTERNAL
