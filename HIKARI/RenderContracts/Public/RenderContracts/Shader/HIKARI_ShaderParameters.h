#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Binding/HIKARI_BindingValue.h"

namespace HIKARI::ASSETS::SHADER {

inline constexpr uint32_t kShaderParameterSlotCount = 16u;
inline constexpr size_t kShaderParameterMetadataStringCapacity = 128u;
inline constexpr size_t kShaderParameterMetadataMaximumBytes = kShaderParameterMetadataStringCapacity - 1u;
inline constexpr int64_t kShaderParameterMaximumExactInteger = 16777216ll;

struct ShaderParameterDefinition {
    std::string key{};
    std::string label{};
    std::string group{};
    uint32_t slot = 0u;
    BINDING::Value defaultValue{};
    bool hasRange = false;
    float minimum = 0.0f;
    float maximum = 0.0f;
    float step = 0.0f;
};

struct ShaderParameterOverride {
    std::string key{};
    BINDING::Value value{};
};

using ShaderParameterSlot = std::array<float, 4u>;
using ShaderParameterSlots = std::array<ShaderParameterSlot, kShaderParameterSlotCount>;

// Validates the current declaration contract and the default values. Keys are
// case-sensitive stable identifiers; display strings are complete UTF-8 text.
bool ValidateShaderParameterDefinitions(std::span<const ShaderParameterDefinition> definitions, std::string& outError);
// Validates the declared type/range and writes a canonical value with every
// inactive union-like field zeroed. outCanonical is unchanged on failure.
bool ValidateShaderParameterValue(const ShaderParameterDefinition& definition, const BINDING::Value& value,
                                  BINDING::Value& outCanonical, std::string& outError);
bool PackShaderParameterValue(const ShaderParameterDefinition& definition, const BINDING::Value& value,
                              ShaderParameterSlot& outSlot, std::string& outError);

// Builds a zero-filled 16-slot constant block from defaults and typed
// overrides. Definitions and override values are checked before output commits.
bool PackShaderParameterValues(std::span<const ShaderParameterDefinition> definitions,
                               std::span<const ShaderParameterOverride> overrides, ShaderParameterSlots& outSlots,
                               std::string& outError);

} // namespace HIKARI::ASSETS::SHADER
