#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"
#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {
    namespace {

        bool MeasureSceneExtent(
            const SceneDocument* document,
            MATH::Vec3& outSize) {

            if (document == nullptr || document->objects.empty()) {
                return false;
            }
            MATH::Vec3 minimum = document->objects.front().transform.position;
            MATH::Vec3 maximum = minimum;
            for (const SceneObjectData& object : document->objects) {
                minimum.x = (std::min)(
                    minimum.x,
                    object.transform.position.x);
                minimum.y = (std::min)(
                    minimum.y,
                    object.transform.position.y);
                minimum.z = (std::min)(
                    minimum.z,
                    object.transform.position.z);
                maximum.x = (std::max)(
                    maximum.x,
                    object.transform.position.x);
                maximum.y = (std::max)(
                    maximum.y,
                    object.transform.position.y);
                maximum.z = (std::max)(
                    maximum.z,
                    object.transform.position.z);
            }
            outSize = {
                (std::max)(maximum.x - minimum.x, 1.0f),
                (std::max)(maximum.y - minimum.y, 1.0f),
                (std::max)(maximum.z - minimum.z, 1.0f)
            };
            return true;
        }

    } // namespace

    void LightComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.Text("Light");
        builder.Bool("Enabled", enabled_);

        int lightType = static_cast<int>(type_);
        static constexpr std::array<const char*, 3> typeChoices{
            "Directional",
            "Point",
            "Spot"
        };
        if (builder.Choice("Type", lightType, typeChoices)) {
            type_ = static_cast<LightComponentType>(
                std::clamp(lightType, 0, 2));
        }

        builder.Text("Scene-linear Color");
        builder.FloatRange("Red", color_.x, 0.0f, 1.0f, 0.01f);
        builder.FloatRange("Green", color_.y, 0.0f, 1.0f, 0.01f);
        builder.FloatRange("Blue", color_.z, 0.0f, 1.0f, 0.01f);
        builder.FloatRange("Intensity", intensity_, 0.0f, 100.0f, 0.05f);
        builder.Int("Priority", priority_);

        if (type_ != LightComponentType::Directional) {
            builder.FloatRange("Range", range_, 0.05f, 1000.0f, 0.1f);
        }
        if (type_ == LightComponentType::Spot) {
            builder.Text("Spot Cone (+Z forward)");
            builder.FloatRange(
                "Inner Cone Angle",
                innerConeAngleDegrees_,
                0.1f,
                89.0f,
                0.1f);
            builder.FloatRange(
                "Outer Cone Angle",
                outerConeAngleDegrees_,
                0.1f,
                89.0f,
                0.1f);
            builder.FloatRange(
                "Angular Falloff",
                angularFalloff_,
                0.05f,
                8.0f,
                0.05f);
        }
        ClampSettings();
    }

    void ReflectionProbeComponent::BuildInspector(
        IInspectorBuilder& builder) {

        builder.Text("Capture");
        builder.Bool("Enabled", enabled_);
        builder.String("Source Cubemap Asset", sourceCubemapAsset_);
        builder.Float("Sphere / Capture Radius", radius_);
        builder.Float("Intensity", intensity_);
        if (builder.Button("Fit Influence Size To Scene")) {
            MATH::Vec3 sceneSize{};
            if (MeasureSceneExtent(
                    builder.GetContext().sceneDocument,
                    sceneSize)) {
                influenceBoxSize_ = sceneSize;
                projectionBoxSize_ = sceneSize;
                radius_ = 0.5f * std::sqrt(
                    sceneSize.x * sceneSize.x +
                    sceneSize.y * sceneSize.y +
                    sceneSize.z * sceneSize.z);
            }
        }
        int captureResolution = static_cast<int>(captureResolution_);
        if (builder.Int("Capture Resolution", captureResolution)) {
            captureResolution_ = static_cast<uint32_t>(
                (std::max)(captureResolution, 1));
        }

        builder.Text("Influence");
        int influenceShape = influenceShape_ ==
            ReflectionProbeInfluenceShape::Box ? 1 : 0;
        static constexpr std::array<const char*, 2> influenceChoices{
            "Sphere",
            "Box"
        };
        if (builder.Choice(
                "Influence Shape",
                influenceShape,
                influenceChoices)) {
            influenceShape_ = influenceShape == 1
                ? ReflectionProbeInfluenceShape::Box
                : ReflectionProbeInfluenceShape::Sphere;
        }
        builder.Float("Influence Offset X", influenceBoxOffset_.x);
        builder.Float("Influence Offset Y", influenceBoxOffset_.y);
        builder.Float("Influence Offset Z", influenceBoxOffset_.z);
        builder.Float("Influence Size X", influenceBoxSize_.x);
        builder.Float("Influence Size Y", influenceBoxSize_.y);
        builder.Float("Influence Size Z", influenceBoxSize_.z);
        builder.Float("Blend Distance", blendDistance_);
        builder.Int("Priority", priority_);

        builder.Text("Projection");
        const bool wasLinked = projectionFollowsInfluence_;
        builder.Bool(
            "Link Projection To Influence",
            projectionFollowsInfluence_);
        if (!wasLinked && projectionFollowsInfluence_) {
            SetProjectionFollowsInfluence(true);
        }

        if (projectionFollowsInfluence_) {
            AlignProjectionToInfluence();
            viewportBoundsTarget_ =
                ReflectionProbeViewportBoundsTarget::Influence;
            builder.Text(
                "Projection Shape: Box (linked)");
            builder.Text(
                "Viewport Bounds: Linked Influence + Projection");
        } else {
            int projectionShape = projectionShape_ ==
                ReflectionProbeProjectionShape::Box ? 1 : 0;
            static constexpr std::array<const char*, 2>
                projectionChoices{
                    "Infinite",
                    "Box"
                };
            if (builder.Choice(
                    "Projection Shape",
                    projectionShape,
                    projectionChoices)) {
                projectionShape_ = projectionShape == 1
                    ? ReflectionProbeProjectionShape::Box
                    : ReflectionProbeProjectionShape::Infinite;
            }
            builder.Float(
                "Projection Offset X",
                projectionBoxOffset_.x);
            builder.Float(
                "Projection Offset Y",
                projectionBoxOffset_.y);
            builder.Float(
                "Projection Offset Z",
                projectionBoxOffset_.z);
            builder.Float(
                "Projection Size X",
                projectionBoxSize_.x);
            builder.Float(
                "Projection Size Y",
                projectionBoxSize_.y);
            builder.Float(
                "Projection Size Z",
                projectionBoxSize_.z);
            if (builder.Button("Match Projection To Influence")) {
                AlignProjectionToInfluence();
            }

            if (projectionShape_ ==
                ReflectionProbeProjectionShape::Box) {

                int viewportTarget =
                    viewportBoundsTarget_ ==
                        ReflectionProbeViewportBoundsTarget::Projection
                        ? 1
                        : 0;
                static constexpr std::array<const char*, 2>
                    viewportTargetChoices{
                        "Influence",
                        "Projection"
                    };
                if (builder.Choice(
                        "Viewport Bounds Target",
                        viewportTarget,
                        viewportTargetChoices)) {

                    viewportBoundsTarget_ =
                        viewportTarget == 1
                            ? ReflectionProbeViewportBoundsTarget::Projection
                            : ReflectionProbeViewportBoundsTarget::Influence;
                }
            } else {
                viewportBoundsTarget_ =
                    ReflectionProbeViewportBoundsTarget::Influence;
                builder.Text("Viewport Bounds: Influence");
            }
        }

        builder.Text("Bake Routing");
        builder.String("Bake Set", bakeSet_);
        builder.String("Scenario", scenario_);
        ClampSettings();
    }

    void IrradianceVolumeComponent::BuildInspector(
        IInspectorBuilder& builder) {

        builder.Text("Volume");
        builder.Bool("Enabled", enabled_);
        builder.Float("Size X", size_.x);
        builder.Float("Size Y", size_.y);
        builder.Float("Size Z", size_.z);
        builder.Float("Intensity", intensity_);
        builder.Float("Blend Distance", blendDistance_);
        builder.Int("Priority", priority_);
        if (builder.Button("Fit Size To Scene")) {
            MATH::Vec3 sceneSize{};
            if (MeasureSceneExtent(
                    builder.GetContext().sceneDocument,
                    sceneSize)) {
                size_ = sceneSize;
            }
        }

        builder.Text("Probe Grid");
        int countX = static_cast<int>(countX_);
        int countY = static_cast<int>(countY_);
        int countZ = static_cast<int>(countZ_);
        int captureResolution = static_cast<int>(captureResolution_);
        builder.Int("Count X", countX);
        builder.Int("Count Y", countY);
        builder.Int("Count Z", countZ);
        builder.Int("Capture Resolution", captureResolution);
        countX_ = static_cast<uint32_t>((std::max)(countX, 1));
        countY_ = static_cast<uint32_t>((std::max)(countY, 1));
        countZ_ = static_cast<uint32_t>((std::max)(countZ, 1));
        captureResolution_ = static_cast<uint32_t>(
            (std::max)(captureResolution, 1));

        builder.Text("Bake Routing");
        builder.String("Bake Set", bakeSet_);
        builder.String("Scenario", scenario_);
        ClampSettings();
    }

} // namespace HIKARI
