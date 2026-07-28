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

    void ReflectionProbeComponent::BuildInspector(
        IInspectorBuilder& builder) {

        builder.Text("Capture");
        builder.Bool("Enabled", enabled_);
        builder.String("Source Cubemap Asset", sourceCubemapAsset_);
        builder.Float("Radius", radius_);
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
        int projectionShape = projectionShape_ ==
            ReflectionProbeProjectionShape::Box ? 1 : 0;
        static constexpr std::array<const char*, 2> projectionChoices{
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
        builder.Float("Projection Offset X", projectionBoxOffset_.x);
        builder.Float("Projection Offset Y", projectionBoxOffset_.y);
        builder.Float("Projection Offset Z", projectionBoxOffset_.z);
        builder.Float("Projection Size X", projectionBoxSize_.x);
        builder.Float("Projection Size Y", projectionBoxSize_.y);
        builder.Float("Projection Size Z", projectionBoxSize_.z);

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
