#include "Scene/Components/Lighting/HIKARI_LightComponent.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"

namespace HIKARI {

    namespace JsonMath = SERIALIZATION::JSON::MATH;

    namespace {

        const char* LightTypeName(LightComponentType type) noexcept {
            switch (type) {
            case LightComponentType::Directional:
                return "Directional";
            case LightComponentType::Spot:
                return "Spot";
            case LightComponentType::Point:
            default:
                return "Point";
            }
        }

        LightComponentType ReadLightType(const nlohmann::json& in) {
            const std::string value =
                in.value("type", std::string("Point"));
            if (value == "Directional") {
                return LightComponentType::Directional;
            }
            if (value == "Spot") {
                return LightComponentType::Spot;
            }
            return LightComponentType::Point;
        }

    } // namespace

    void LightComponent::Serialize(nlohmann::json& out) const {
        out = {
            { "enabled", enabled_ },
            { "type", LightTypeName(type_) },
            { "color", JsonMath::ToJsonArray(color_) },
            { "intensity", intensity_ },
            { "range", range_ },
            { "innerConeAngleDegrees", innerConeAngleDegrees_ },
            { "outerConeAngleDegrees", outerConeAngleDegrees_ },
            { "angularFalloff", angularFalloff_ },
            { "priority", priority_ }
        };
    }

    void LightComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        type_ = ReadLightType(in);
        color_ = JSONREAD::Vec3Or(
            in.value("color", nlohmann::json::array()),
            color_);
        intensity_ = in.value("intensity", intensity_);
        range_ = in.value("range", range_);
        innerConeAngleDegrees_ = in.value(
            "innerConeAngleDegrees",
            innerConeAngleDegrees_);
        outerConeAngleDegrees_ = in.value(
            "outerConeAngleDegrees",
            outerConeAngleDegrees_);
        angularFalloff_ = in.value(
            "angularFalloff",
            angularFalloff_);
        priority_ = in.value("priority", priority_);
        ClampSettings();
    }

    void LightComponent::SetLightType(
        LightComponentType type) noexcept {

        switch (type) {
        case LightComponentType::Directional:
        case LightComponentType::Point:
        case LightComponentType::Spot:
            type_ = type;
            break;
        default:
            type_ = LightComponentType::Point;
            break;
        }
    }

    void LightComponent::SetColor(
        const MATH::Vec3& color) noexcept {

        color_ = color;
        ClampSettings();
    }

    void LightComponent::SetIntensity(float intensity) noexcept {
        intensity_ = intensity;
        ClampSettings();
    }

    void LightComponent::SetRange(float range) noexcept {
        range_ = range;
        ClampSettings();
    }

    void LightComponent::SetSpotConeAnglesDegrees(
        float innerAngleDegrees,
        float outerAngleDegrees) noexcept {

        innerConeAngleDegrees_ = innerAngleDegrees;
        outerConeAngleDegrees_ = outerAngleDegrees;
        ClampSettings();
    }

    void LightComponent::SetAngularFalloff(
        float angularFalloff) noexcept {

        angularFalloff_ = angularFalloff;
        ClampSettings();
    }

    void LightComponent::ClampSettings() noexcept {
        color_.x = std::clamp(
            std::isfinite(color_.x) ? color_.x : 1.0f,
            0.0f,
            1.0f);
        color_.y = std::clamp(
            std::isfinite(color_.y) ? color_.y : 1.0f,
            0.0f,
            1.0f);
        color_.z = std::clamp(
            std::isfinite(color_.z) ? color_.z : 1.0f,
            0.0f,
            1.0f);
        intensity_ = std::clamp(
            std::isfinite(intensity_) ? intensity_ : 1.0f,
            0.0f,
            100.0f);
        range_ = std::clamp(
            std::isfinite(range_) ? range_ : 5.0f,
            0.05f,
            1000.0f);
        innerConeAngleDegrees_ = std::clamp(
            std::isfinite(innerConeAngleDegrees_)
                ? innerConeAngleDegrees_
                : 25.0f,
            0.1f,
            89.0f);
        outerConeAngleDegrees_ = std::clamp(
            std::isfinite(outerConeAngleDegrees_)
                ? outerConeAngleDegrees_
                : 35.0f,
            innerConeAngleDegrees_,
            89.0f);
        angularFalloff_ = std::clamp(
            std::isfinite(angularFalloff_) ? angularFalloff_ : 1.0f,
            0.05f,
            8.0f);
    }

} // namespace HIKARI
