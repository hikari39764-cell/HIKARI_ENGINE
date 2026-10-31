#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include <json.hpp>

#include "Core/Math/HIKARI_Math3D.h"

// 型不一致を診断付きで拒否する JSON 読み取り。キーが無い場合は fallback を使う。
// 診断文は "<path>.<key> must be ..." で統一する。
namespace HIKARI::JSONSTRICT {

inline bool Fail(std::string& outDiagnostic, std::string message) {
    outDiagnostic = std::move(message);
    return false;
}

inline std::string FieldPath(std::string_view path, const char* key) { return std::string(path) + "." + key; }

inline bool ReadBool(const nlohmann::json& object, const char* key, bool fallback, bool& outValue,
                     std::string& outDiagnostic, std::string_view path) {
    const auto member = object.find(key);
    if (member == object.end()) {
        outValue = fallback;
        return true;
    }
    if (!member->is_boolean()) {
        return Fail(outDiagnostic, FieldPath(path, key) + " must be a boolean");
    }
    outValue = member->get<bool>();
    return true;
}

inline bool ReadString(const nlohmann::json& object, const char* key, std::string fallback, std::string& outValue,
                       std::string& outDiagnostic, std::string_view path) {
    const auto member = object.find(key);
    if (member == object.end()) {
        outValue = std::move(fallback);
        return true;
    }
    if (!member->is_string()) {
        return Fail(outDiagnostic, FieldPath(path, key) + " must be a string");
    }
    outValue = member->get<std::string>();
    return true;
}

inline bool IsFloatRange(double value) noexcept {
    return std::isfinite(value) && std::abs(value) <= static_cast<double>((std::numeric_limits<float>::max)());
}

inline bool ReadFloat(const nlohmann::json& object, const char* key, float fallback, float& outValue,
                      std::string& outDiagnostic, std::string_view path) {
    const auto member = object.find(key);
    if (member == object.end()) {
        outValue = fallback;
        return true;
    }
    if (!member->is_number()) {
        return Fail(outDiagnostic, FieldPath(path, key) + " must be a finite number");
    }
    const double value = member->get<double>();
    if (!IsFloatRange(value)) {
        return Fail(outDiagnostic, FieldPath(path, key) + " must be a finite float");
    }
    outValue = static_cast<float>(value);
    return true;
}

inline bool ReadUint64(const nlohmann::json& object, const char* key, uint64_t fallback, uint64_t& outValue,
                       std::string& outDiagnostic, std::string_view path) {
    const auto member = object.find(key);
    if (member == object.end()) {
        outValue = fallback;
        return true;
    }
    if (member->is_number_unsigned()) {
        outValue = member->get<uint64_t>();
        return true;
    }
    if (member->is_number_integer() && member->get<int64_t>() >= 0) {
        outValue = static_cast<uint64_t>(member->get<int64_t>());
        return true;
    }
    return Fail(outDiagnostic, FieldPath(path, key) + " must be a non-negative integer");
}

inline bool ReadUint32(const nlohmann::json& object, const char* key, uint32_t fallback, uint32_t& outValue,
                       std::string& outDiagnostic, std::string_view path) {
    uint64_t value = fallback;
    if (!ReadUint64(object, key, fallback, value, outDiagnostic, path)) {
        return false;
    }
    if (value > (std::numeric_limits<uint32_t>::max)()) {
        return Fail(outDiagnostic, FieldPath(path, key) + " exceeds uint32 range");
    }
    outValue = static_cast<uint32_t>(value);
    return true;
}

// 資産の root.formatVersion が expected と一致すること（古い形式は読まない）。
inline bool RequireFormatVersion(const nlohmann::json& root, uint32_t expected, std::string& outDiagnostic) {
    if (!root.is_object() || !root.contains("formatVersion")) {
        return Fail(outDiagnostic, "root.formatVersion is required");
    }
    uint32_t version = 0u;
    if (!ReadUint32(root, "formatVersion", 0u, version, outDiagnostic, "root")) {
        return false;
    }
    if (version != expected) {
        return Fail(outDiagnostic, "unsupported formatVersion " + std::to_string(version) +
                                       " (supported: " + std::to_string(expected) + ")");
    }
    return true;
}

} // namespace HIKARI::JSONSTRICT
