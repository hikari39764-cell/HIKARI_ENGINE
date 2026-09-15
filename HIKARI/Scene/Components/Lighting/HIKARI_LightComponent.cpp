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
            { "sourceRadius", sourceRadius_ },
            { "castsShadows", castsShadows_ },
            { "distanceFalloff", RENDER3D::LIGHTING::ToString(distanceFalloff_) },
            { "innerConeAngleDegrees", innerConeAngleDegrees_ },
            { "outerConeAngleDegrees", outerConeAngleDegrees_ },
            { "angularFalloff", angularFalloff_ },
            { "priority", priority_ }
        };
    }

    void LightComponent::Deserialize(const nlohmann::json& in) {
        const bool previousEnabled = enabled_;
        const LightComponentType previousType = type_;
        const MATH::Vec3 previousColor = color_;
        const float previousIntensity = intensity_;
        const float previousRange = range_;
        const float previousSourceRadius = sourceRadius_;
        const bool previousCastsShadows = castsShadows_;
        const auto previousDistanceFalloff = distanceFalloff_;
        const float previousInnerConeAngle = innerConeAngleDegrees_;
        const float previousOuterConeAngle = outerConeAngleDegrees_;
        const float previousAngularFalloff = angularFalloff_;
        const int previousPriority = priority_;

        enabled_ = in.value("enabled", enabled_);
        type_ = ReadLightType(in);
        color_ = JSONREAD::Vec3Or(
            in.value("color", nlohmann::json::array()),
            color_);
        intensity_ = in.value("intensity", intensity_);
        range_ = in.value("range", range_);
        sourceRadius_ = in.value("sourceRadius", 0.0f);
        castsShadows_ = in.value("castsShadows", false);
        // 旧シーンを読み直した際も、新規作成用の既定値で照度を変えない。
        distanceFalloff_ = RENDER3D::LIGHTING::ParseLocalLightFalloff(
            in.value("distanceFalloff", std::string("LegacyRange")));
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

        if (previousEnabled != enabled_ || previousType != type_ ||
            previousColor.x != color_.x ||
            previousColor.y != color_.y ||
            previousColor.z != color_.z ||
            previousIntensity != intensity_ || previousRange != range_ ||
            previousSourceRadius != sourceRadius_ || previousCastsShadows != castsShadows_ ||
            previousInnerConeAngle != innerConeAngleDegrees_ ||
            previousOuterConeAngle != outerConeAngleDegrees_ ||
            previousAngularFalloff != angularFalloff_ ||
            previousDistanceFalloff != distanceFalloff_ ||
            previousPriority != priority_) {
            BumpStateRevision();
        }
    }

    void LightComponent::SetLightType(
        LightComponentType type) noexcept {

        LightComponentType canonical = LightComponentType::Point;
        switch (type) {
        case LightComponentType::Directional:
        case LightComponentType::Point:
        case LightComponentType::Spot:
            canonical = type;
            break;
        default:
            break;
        }
        if (type_ != canonical) {
            type_ = canonical;
            BumpStateRevision();
        }
    }

    void LightComponent::SetEnabled(bool enabled) noexcept {
        if (enabled_ != enabled) {
            enabled_ = enabled;
            BumpStateRevision();
        }
    }

    void LightComponent::SetColor(
        const MATH::Vec3& color) noexcept {

        const MATH::Vec3 previous = color_;
        color_ = color;
        ClampSettings();
        if (previous.x != color_.x || previous.y != color_.y ||
            previous.z != color_.z) {
            BumpStateRevision();
        }
    }

    void LightComponent::SetIntensity(float intensity) noexcept {
        const float previous = intensity_;
        intensity_ = intensity;
        ClampSettings();
        if (previous != intensity_) {
            BumpStateRevision();
        }
    }

    void LightComponent::SetRange(float range) noexcept {
        const float previous = range_;
        range_ = range;
        ClampSettings();
        if (previous != range_) {
            BumpStateRevision();
        }
    }

    void LightComponent::SetCastsShadows(bool value) noexcept {
        if (castsShadows_ != value) {
            castsShadows_ = value;
            BumpStateRevision();
        }
    }

    void LightComponent::SetSourceRadius(float radius) noexcept {
        const float canonical = RENDER3D::LIGHTING::SanitizeLocalLightSourceRadius(radius);
        if (sourceRadius_ != canonical) {
            sourceRadius_ = canonical;
            BumpStateRevision();
        }
    }

    void LightComponent::SetSpotConeAnglesDegrees(
        float innerAngleDegrees,
        float outerAngleDegrees) noexcept {

        const float previousInner = innerConeAngleDegrees_;
        const float previousOuter = outerConeAngleDegrees_;
        innerConeAngleDegrees_ = innerAngleDegrees;
        outerConeAngleDegrees_ = outerAngleDegrees;
        ClampSettings();
        if (previousInner != innerConeAngleDegrees_ ||
            previousOuter != outerConeAngleDegrees_) {
            BumpStateRevision();
        }
    }

    void LightComponent::SetAngularFalloff(
        float angularFalloff) noexcept {

        const float previous = angularFalloff_;
        angularFalloff_ = angularFalloff;
        ClampSettings();
        if (previous != angularFalloff_) {
            BumpStateRevision();
        }
    }

    void LightComponent::SetDistanceFalloff(
        RENDER3D::LIGHTING::LocalLightFalloff value) noexcept {
        const auto canonical = RENDER3D::LIGHTING::CanonicalLocalLightFalloff(value);
        if (distanceFalloff_ != canonical) {
            distanceFalloff_ = canonical;
            BumpStateRevision();
        }
    }

    void LightComponent::SetPriority(int priority) noexcept {
        if (priority_ != priority) {
            priority_ = priority;
            BumpStateRevision();
        }
    }

    void LightComponent::BumpStateRevision() noexcept {
        ++stateRevision_;
        if (stateRevision_ == 0u) {
            stateRevision_ = 1u;
        }
    }

    void LightComponent::ClampSettings() noexcept {
        sourceRadius_ = RENDER3D::LIGHTING::SanitizeLocalLightSourceRadius(sourceRadius_);
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
