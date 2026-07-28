#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"

#include <algorithm>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"

namespace HIKARI {

    namespace JsonMath = SERIALIZATION::JSON::MATH;

    namespace {

        const char* InfluenceShapeName(
            ReflectionProbeInfluenceShape shape) noexcept {

            return shape == ReflectionProbeInfluenceShape::Box
                ? "Box"
                : "Sphere";
        }

        const char* ProjectionShapeName(
            ReflectionProbeProjectionShape shape) noexcept {

            return shape == ReflectionProbeProjectionShape::Box
                ? "Box"
                : "Infinite";
        }

        ReflectionProbeInfluenceShape ReadInfluenceShape(
            const nlohmann::json& in) {

            return in.value("influenceShape", std::string("Sphere")) == "Box"
                ? ReflectionProbeInfluenceShape::Box
                : ReflectionProbeInfluenceShape::Sphere;
        }

        ReflectionProbeProjectionShape ReadProjectionShape(
            const nlohmann::json& in) {

            return in.value("projectionShape", std::string("Infinite")) == "Box"
                ? ReflectionProbeProjectionShape::Box
                : ReflectionProbeProjectionShape::Infinite;
        }

    } // namespace

    void ReflectionProbeComponent::Serialize(nlohmann::json& out) const {
        out = {
            { "enabled", enabled_ },
            { "sourceCubemapAsset", sourceCubemapAsset_ },
            { "radius", radius_ },
            { "intensity", intensity_ },
            { "influenceShape", InfluenceShapeName(influenceShape_) },
            { "influenceBoxOffset", JsonMath::ToJsonArray(influenceBoxOffset_) },
            { "influenceBoxSize", JsonMath::ToJsonArray(influenceBoxSize_) },
            { "projectionShape", ProjectionShapeName(projectionShape_) },
            { "projectionBoxOffset", JsonMath::ToJsonArray(projectionBoxOffset_) },
            { "projectionBoxSize", JsonMath::ToJsonArray(projectionBoxSize_) },
            { "blendDistance", blendDistance_ },
            { "priority", priority_ },
            { "captureResolution", captureResolution_ },
            { "bakeSet", bakeSet_ },
            { "scenario", scenario_ }
        };
    }

    void ReflectionProbeComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        sourceCubemapAsset_ = in.value(
            "sourceCubemapAsset",
            sourceCubemapAsset_);
        radius_ = in.value("radius", radius_);
        intensity_ = in.value("intensity", intensity_);
        influenceShape_ = ReadInfluenceShape(in);
        influenceBoxOffset_ = JSONREAD::Vec3Or(
            in.value("influenceBoxOffset", nlohmann::json::array()),
            influenceBoxOffset_);
        influenceBoxSize_ = JSONREAD::Vec3Or(
            in.value("influenceBoxSize", nlohmann::json::array()),
            influenceBoxSize_);
        projectionShape_ = ReadProjectionShape(in);
        projectionBoxOffset_ = JSONREAD::Vec3Or(
            in.value("projectionBoxOffset", nlohmann::json::array()),
            projectionBoxOffset_);
        projectionBoxSize_ = JSONREAD::Vec3Or(
            in.value("projectionBoxSize", nlohmann::json::array()),
            projectionBoxSize_);
        blendDistance_ = in.value("blendDistance", blendDistance_);
        priority_ = in.value("priority", priority_);
        captureResolution_ = in.value(
            "captureResolution",
            captureResolution_);
        bakeSet_ = in.value("bakeSet", bakeSet_);
        scenario_ = in.value("scenario", scenario_);
        ClampSettings();
    }

    void ReflectionProbeComponent::ClampSettings() noexcept {
        radius_ = (std::max)(radius_, 0.05f);
        intensity_ = std::clamp(intensity_, 0.0f, 8.0f);
        influenceBoxSize_.x = (std::max)(influenceBoxSize_.x, 0.05f);
        influenceBoxSize_.y = (std::max)(influenceBoxSize_.y, 0.05f);
        influenceBoxSize_.z = (std::max)(influenceBoxSize_.z, 0.05f);
        projectionBoxSize_.x = (std::max)(projectionBoxSize_.x, 0.05f);
        projectionBoxSize_.y = (std::max)(projectionBoxSize_.y, 0.05f);
        projectionBoxSize_.z = (std::max)(projectionBoxSize_.z, 0.05f);
        blendDistance_ = (std::max)(blendDistance_, 0.0f);
        if (captureResolution_ <= 64u) {
            captureResolution_ = 64u;
        } else if (captureResolution_ <= 128u) {
            captureResolution_ = 128u;
        } else {
            captureResolution_ = 256u;
        }
        if (bakeSet_.empty()) {
            bakeSet_ = "Default";
        }
        if (scenario_.empty()) {
            scenario_ = "Default";
        }
    }

} // namespace HIKARI
