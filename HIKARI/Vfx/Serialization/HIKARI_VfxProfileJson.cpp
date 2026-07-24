#include "Vfx/Serialization/HIKARI_VfxProfileJson.h"

#include <string>

namespace HIKARI::VFX::SERIALIZATION {

    FxDomain ParseFxDomain(
        const nlohmann::json& valueNode,
        FxDomain fallback) {

        const std::string value = valueNode.is_string()
            ? valueNode.get<std::string>()
            : std::string{};
        if (value == "GlobalPost") return FxDomain::GlobalPost;
        if (value == "ScopedPost") return FxDomain::ScopedPost;
        if (value == "ObjectMaterial") return FxDomain::ObjectMaterial;
        if (value == "Particle") return FxDomain::Particle;
        return fallback;
    }

    CompositeMode ParseCompositeMode(
        const nlohmann::json& valueNode,
        CompositeMode fallback) {

        const std::string value = valueNode.is_string()
            ? valueNode.get<std::string>()
            : std::string{};
        if (value == "Alpha") return CompositeMode::Alpha;
        if (value == "Additive") return CompositeMode::Additive;
        if (value == "Multiply") return CompositeMode::Multiply;
        if (value == "Screen") return CompositeMode::Screen;
        if (value == "Replace") return CompositeMode::Replace;
        return fallback;
    }

} // namespace HIKARI::VFX::SERIALIZATION
