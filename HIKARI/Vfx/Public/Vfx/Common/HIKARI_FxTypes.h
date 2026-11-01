#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Core/Text/HIKARI_EnumNames.h"

namespace HIKARI::VFX {

static constexpr size_t kMaterialFxUserCount = 8;

enum class FxDomain { GlobalPost, ScopedPost, ObjectMaterial, Particle };

enum class CompositeMode { Alpha, Additive, Multiply, Screen, Replace };

inline constexpr TEXT::EnumName<CompositeMode> kCompositeModeNames[] = {
    {CompositeMode::Alpha, "Alpha"},   {CompositeMode::Additive, "Additive"}, {CompositeMode::Multiply, "Multiply"},
    {CompositeMode::Screen, "Screen"}, {CompositeMode::Replace, "Replace"},
};

enum class ParamType { Float, Float2, Float3, Float4, Color, Color3, Color4, Toggle };

struct ParamChannelRef {
    uint8_t slot = 0;
    uint8_t channel = 0;
};

struct ParamDesc {
    std::string key;
    std::string label;
    ParamType type = ParamType::Float;
    ParamChannelRef ref{};
    float defaultValues[4]{};
    float minValues[4]{};
    float maxValues[4]{};
    float speed = 0.01f;
};

struct PassDescriptor {
    std::string passName;
    std::string programId;
    std::string entryId = "Main";
    CompositeMode composite = CompositeMode::Alpha;
    bool depthTest = false;
    bool depthWrite = false;
    bool doubleSided = false;
};

inline bool IsStableFxProfileId(std::string_view value) {
    if (value.empty() || value.size() > 64u) {
        return false;
    }
    for (const char character : value) {
        const bool alphaNumeric = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                                  (character >= '0' && character <= '9');
        if (!alphaNumeric && character != '_' && character != '-') {
            return false;
        }
    }
    return true;
}

struct VariantKey {
    std::string shaderId;
    std::string vertexShaderId;
    std::string pixelShaderId;
    uint32_t featureBits = 0;
    CompositeMode composite = CompositeMode::Alpha;
    bool depthTest = false;
    bool depthWrite = false;
    bool doubleSided = false;

    bool operator==(const VariantKey& rhs) const = default;
};

} // namespace HIKARI::VFX
