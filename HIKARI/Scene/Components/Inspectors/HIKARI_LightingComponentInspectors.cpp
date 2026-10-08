// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"
#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {
namespace {

bool MeasureSceneExtent(const SceneDocument* document, MATH::Vec3& outSize) {
    if (document == nullptr || document->objects.empty()) {
        return false;
    }
    MATH::Vec3 minimum = document->objects.front().transform.position;
    MATH::Vec3 maximum = minimum;
    for (const SceneObjectData& object : document->objects) {
        minimum.x = (std::min)(minimum.x, object.transform.position.x);
        minimum.y = (std::min)(minimum.y, object.transform.position.y);
        minimum.z = (std::min)(minimum.z, object.transform.position.z);
        maximum.x = (std::max)(maximum.x, object.transform.position.x);
        maximum.y = (std::max)(maximum.y, object.transform.position.y);
        maximum.z = (std::max)(maximum.z, object.transform.position.z);
    }
    outSize = {(std::max)(maximum.x - minimum.x, 1.0f), (std::max)(maximum.y - minimum.y, 1.0f),
               (std::max)(maximum.z - minimum.z, 1.0f)};
    return true;
}

} // namespace

void LightComponent::BuildInspector(IInspectorBuilder& builder) {
    const bool previousEnabled = enabled_;
    const LightComponentType previousType = type_;
    const MATH::Vec3 previousColor = color_;
    const float previousIntensity = intensity_;
    const float previousRange = range_;
    const float previousSourceRadius = sourceRadius_;
    const bool previousCastsShadows = castsShadows_;
    const float previousInnerConeAngle = innerConeAngleDegrees_;
    const float previousOuterConeAngle = outerConeAngleDegrees_;
    const float previousAngularFalloff = angularFalloff_;
    const int previousPriority = priority_;

    builder.Text("Light");
    builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled"}, enabled_);

    int lightType = static_cast<int>(type_);
    static constexpr std::array<AuthoringChoice, 2> typeChoices{AuthoringChoice{"Point", "Point"},
                                                                AuthoringChoice{"Spot", "Spot"}};
    if (builder.Choice(AuthoringFieldDescriptor{"type", "Type"}, lightType, typeChoices)) {
        type_ = static_cast<LightComponentType>(std::clamp(lightType, 0, 1));
    }

    builder.Color3(AuthoringFieldDescriptor{"color", "Color"}, color_.x, color_.y, color_.z);
    builder.FloatRange(
        AuthoringFieldDescriptor{"intensity", "Luminous Flux (lm)",
                                 "Total emitted light. A 60 W bulb is about 800 lm. Spot cones mask, they do not "
                                 "concentrate the flux."},
        intensity_, 0.0f, kMaximumLightLumens, 10.0f);
    builder.Int(AuthoringFieldDescriptor{"priority", "Priority"}, priority_);
    builder.Bool(AuthoringFieldDescriptor{"castsShadows", "Cast Shadows",
                                          "Request local-light shadows within the active rendering budget."},
                 castsShadows_);
    builder.FloatRange(AuthoringFieldDescriptor{"range", "Range (m)",
                                                "Inverse-square falloff fades smoothly to zero at this distance."},
                       range_, 0.05f, 1000.0f, 0.1f);
    builder.FloatRange(
        AuthoringFieldDescriptor{
            "sourceRadius", "Source Radius",
            "Emitter radius in metres. Broadens direct specular independently of material roughness and light range. Zero preserves punctual lighting."},
        sourceRadius_, 0.0f, RENDER3D::LIGHTING::kMaximumLocalLightSourceRadius, 0.01f);
    if (type_ == LightComponentType::Spot) {
        builder.Text("Spot Cone (+Z forward)");
        builder.FloatRange(AuthoringFieldDescriptor{"innerConeAngleDegrees", "Inner Cone Angle"},
                           innerConeAngleDegrees_, 0.1f, 89.0f, 0.1f);
        builder.FloatRange(AuthoringFieldDescriptor{"outerConeAngleDegrees", "Outer Cone Angle"},
                           outerConeAngleDegrees_, 0.1f, 89.0f, 0.1f);
        builder.FloatRange(AuthoringFieldDescriptor{"angularFalloff", "Angular Falloff"}, angularFalloff_, 0.05f, 8.0f,
                           0.05f);
    }
    ClampSettings();

    if (previousEnabled != enabled_ || previousType != type_ || previousColor.x != color_.x ||
        previousColor.y != color_.y || previousColor.z != color_.z || previousIntensity != intensity_ ||
        previousRange != range_ || previousSourceRadius != sourceRadius_ || previousCastsShadows != castsShadows_ ||
        previousInnerConeAngle != innerConeAngleDegrees_ || previousOuterConeAngle != outerConeAngleDegrees_ ||
        previousAngularFalloff != angularFalloff_ || previousPriority != priority_) {
        BumpStateRevision();
    }
}

void ReflectionProbeComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Text("Capture");
    builder.Bool("Enabled", enabled_);
    builder.String("Source Cubemap Asset", sourceCubemapAsset_);
    builder.Float("Sphere / Capture Radius", radius_);
    builder.Float("Intensity", intensity_);
    if (builder.Button("Fit Influence Size To Scene")) {
        MATH::Vec3 sceneSize{};
        if (MeasureSceneExtent(builder.GetContext().sceneDocument, sceneSize)) {
            influenceBoxSize_ = sceneSize;
            projectionBoxSize_ = sceneSize;
            radius_ =
                0.5f * std::sqrt(sceneSize.x * sceneSize.x + sceneSize.y * sceneSize.y + sceneSize.z * sceneSize.z);
        }
    }
    int captureQuality = captureResolution_ <= 64u ? 0 : (captureResolution_ <= 128u ? 1 : 2);
    static constexpr std::array<const char*, 3> reflectionCaptureChoices{"64 - Preview", "128 - Standard",
                                                                         "256 - High"};
    if (builder.Choice("Capture Quality", captureQuality, reflectionCaptureChoices)) {
        static constexpr uint32_t resolutions[] = {64u, 128u, 256u};
        captureResolution_ = resolutions[std::clamp(captureQuality, 0, 2)];
    }
    builder.Text("Higher quality uses a full roughness mip chain and more samples.");

    builder.Text("Influence");
    int influenceShape = influenceShape_ == ReflectionProbeInfluenceShape::Box ? 1 : 0;
    static constexpr std::array<const char*, 2> influenceChoices{"Sphere", "Box"};
    if (builder.Choice("Influence Shape", influenceShape, influenceChoices)) {
        influenceShape_ =
            influenceShape == 1 ? ReflectionProbeInfluenceShape::Box : ReflectionProbeInfluenceShape::Sphere;
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
    builder.Bool("Link Projection To Influence", projectionFollowsInfluence_);
    if (!wasLinked && projectionFollowsInfluence_) {
        SetProjectionFollowsInfluence(true);
    }

    if (projectionFollowsInfluence_) {
        AlignProjectionToInfluence();
        viewportBoundsTarget_ = ReflectionProbeViewportBoundsTarget::Influence;
        builder.Text("Projection Shape: Box (linked)");
        builder.Text("Viewport Bounds: Linked Influence + Projection");
    } else {
        int projectionShape = projectionShape_ == ReflectionProbeProjectionShape::Box ? 1 : 0;
        static constexpr std::array<const char*, 2> projectionChoices{"Infinite", "Box"};
        if (builder.Choice("Projection Shape", projectionShape, projectionChoices)) {
            projectionShape_ =
                projectionShape == 1 ? ReflectionProbeProjectionShape::Box : ReflectionProbeProjectionShape::Infinite;
        }
        builder.Float("Projection Offset X", projectionBoxOffset_.x);
        builder.Float("Projection Offset Y", projectionBoxOffset_.y);
        builder.Float("Projection Offset Z", projectionBoxOffset_.z);
        builder.Float("Projection Size X", projectionBoxSize_.x);
        builder.Float("Projection Size Y", projectionBoxSize_.y);
        builder.Float("Projection Size Z", projectionBoxSize_.z);
        if (builder.Button("Match Projection To Influence")) {
            AlignProjectionToInfluence();
        }

        if (projectionShape_ == ReflectionProbeProjectionShape::Box) {
            int viewportTarget = viewportBoundsTarget_ == ReflectionProbeViewportBoundsTarget::Projection ? 1 : 0;
            static constexpr std::array<const char*, 2> viewportTargetChoices{"Influence", "Projection"};
            if (builder.Choice("Viewport Bounds Target", viewportTarget, viewportTargetChoices)) {
                viewportBoundsTarget_ = viewportTarget == 1 ? ReflectionProbeViewportBoundsTarget::Projection
                                                            : ReflectionProbeViewportBoundsTarget::Influence;
            }
        } else {
            viewportBoundsTarget_ = ReflectionProbeViewportBoundsTarget::Influence;
            builder.Text("Viewport Bounds: Influence");
        }
    }

    builder.Text("Bake Routing");
    builder.String("Bake Set", bakeSet_);
    builder.String("Scenario", scenario_);
    ClampSettings();
}

void IrradianceVolumeComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Text("Volume");
    builder.Bool("Enabled", enabled_);
    std::array<const char*, std::size(kLightProbeVolumeModeNames)> modeLabels{};
    int mode = 0;
    for (size_t index = 0; index < modeLabels.size(); ++index) {
        modeLabels[index] = kLightProbeVolumeModeNames[index].name;
        if (kLightProbeVolumeModeNames[index].value == mode_)
            mode = static_cast<int>(index);
    }
    if (builder.Choice("Mode", mode, modeLabels))
        mode_ = kLightProbeVolumeModeNames[mode].value;
    if (mode_ == LightProbeVolumeMode::Realtime)
        builder.Text("Realtime volumes are updated by ray-traced dynamic GI and need no bake.");
    builder.Float("Size X", size_.x);
    builder.Float("Size Y", size_.y);
    builder.Float("Size Z", size_.z);
    builder.Float("Intensity", intensity_);
    builder.Float("Blend Distance", blendDistance_);
    builder.FloatRange("Surface Normal Bias", normalBias_, 0.0f, 0.5f, 0.01f);
    builder.Text("Normal bias samples GI slightly away from walls to reduce leaks.");
    builder.Int("Priority", priority_);
    if (builder.Button("Fit Size To Scene")) {
        MATH::Vec3 sceneSize{};
        if (MeasureSceneExtent(builder.GetContext().sceneDocument, sceneSize)) {
            size_ = sceneSize;
        }
    }

    builder.Text("Probe Grid");
    int countX = static_cast<int>(countX_);
    int countY = static_cast<int>(countY_);
    int countZ = static_cast<int>(countZ_);
    builder.Int("Count X", countX);
    builder.Int("Count Y", countY);
    builder.Int("Count Z", countZ);
    countX_ = static_cast<uint32_t>((std::max)(countX, 1));
    countY_ = static_cast<uint32_t>((std::max)(countY, 1));
    countZ_ = static_cast<uint32_t>((std::max)(countZ, 1));
    int captureQuality = captureResolution_ <= 16u ? 0 : (captureResolution_ <= 32u ? 1 : 2);
    static constexpr std::array<const char*, 3> irradianceCaptureChoices{"16 - Preview", "32 - Standard", "64 - High"};
    if (builder.Choice("Capture Quality", captureQuality, irradianceCaptureChoices)) {
        static constexpr uint32_t resolutions[] = {16u, 32u, 64u};
        captureResolution_ = resolutions[std::clamp(captureQuality, 0, 2)];
    }

    builder.Text("Bake Routing");
    builder.String("Bake Set", bakeSet_);
    builder.String("Scenario", scenario_);
    ClampSettings();
}

} // namespace HIKARI

#endif // defined(HIKARI_WITH_EDITOR)
