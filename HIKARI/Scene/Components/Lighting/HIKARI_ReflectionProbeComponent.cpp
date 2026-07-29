#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"

#include <algorithm>
#include <cmath>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Math/HIKARI_MathValidation.h"
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
            { "projectionFollowsInfluence", projectionFollowsInfluence_ },
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
        // Existing scenes predate linked authoring and must keep their two
        // independently authored bounds until the user opts in.
        projectionFollowsInfluence_ = in.contains(
                "projectionFollowsInfluence")
            ? in.value("projectionFollowsInfluence", false)
            : false;
        blendDistance_ = in.value("blendDistance", blendDistance_);
        priority_ = in.value("priority", priority_);
        captureResolution_ = in.value(
            "captureResolution",
            captureResolution_);
        bakeSet_ = in.value("bakeSet", bakeSet_);
        scenario_ = in.value("scenario", scenario_);
        ClampSettings();
    }

    bool ReflectionProbeComponent::SetRadius(float radius) noexcept {
        if (!std::isfinite(radius)) {
            return false;
        }
        const float clamped = (std::max)(radius, 0.05f);
        if (radius_ == clamped) {
            return false;
        }
        radius_ = clamped;
        if (projectionFollowsInfluence_ &&
            projectionShape_ ==
                ReflectionProbeProjectionShape::Box) {

            AlignProjectionToInfluence();
        }
        return true;
    }

    bool ReflectionProbeComponent::SetInfluenceBoxSize(
        const MATH::Vec3& size) noexcept {

        if (!MATH::IsFinite(size)) {
            return false;
        }
        const MATH::Vec3 clamped{
            (std::max)(size.x, 0.05f),
            (std::max)(size.y, 0.05f),
            (std::max)(size.z, 0.05f)
        };
        if (influenceBoxSize_.x == clamped.x &&
            influenceBoxSize_.y == clamped.y &&
            influenceBoxSize_.z == clamped.z) {
            return false;
        }
        influenceBoxSize_ = clamped;
        if (projectionFollowsInfluence_) {
            AlignProjectionToInfluence();
        }
        return true;
    }

    void ReflectionProbeComponent::SetProjectionFollowsInfluence(
        bool enabled) noexcept {

        projectionFollowsInfluence_ = enabled;
        if (projectionFollowsInfluence_) {
            viewportBoundsTarget_ =
                ReflectionProbeViewportBoundsTarget::Influence;
            AlignProjectionToInfluence();
        }
    }

    bool ReflectionProbeComponent::SetProjectionBoxSize(
        const MATH::Vec3& size) noexcept {

        if (!MATH::IsFinite(size)) {
            return false;
        }
        const MATH::Vec3 clamped{
            (std::max)(size.x, 0.05f),
            (std::max)(size.y, 0.05f),
            (std::max)(size.z, 0.05f)
        };
        if (projectionBoxSize_.x == clamped.x &&
            projectionBoxSize_.y == clamped.y &&
            projectionBoxSize_.z == clamped.z) {
            return false;
        }
        projectionBoxSize_ = clamped;
        return true;
    }

    void ReflectionProbeComponent::AlignProjectionToInfluence() noexcept {
        projectionShape_ = ReflectionProbeProjectionShape::Box;
        if (influenceShape_ ==
            ReflectionProbeInfluenceShape::Box) {

            projectionBoxOffset_ = influenceBoxOffset_;
            projectionBoxSize_ = influenceBoxSize_;
            return;
        }

        projectionBoxOffset_ = { 0.0f, 0.0f, 0.0f };
        const float diameter = radius_ * 2.0f;
        projectionBoxSize_ = {
            diameter,
            diameter,
            diameter
        };
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
        if (projectionFollowsInfluence_) {
            AlignProjectionToInfluence();
        }
    }

} // namespace HIKARI
