// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"
#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Assets/HIKARI_AssetTypes.h"
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
    using RENDER3D::LIGHTING::SceneLightType;
    LightSettings next = settings_;
    builder.Text("Light");
    builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled"}, next.enabled);

    int lightType = static_cast<int>(next.type);
    static constexpr std::array<AuthoringChoice, 4> typeChoices{
        AuthoringChoice{"Point", "Point"}, AuthoringChoice{"Spot", "Spot"}, AuthoringChoice{"Tube", "Tube"},
        AuthoringChoice{"Rect", "Rect"}};
    if (builder.Choice(AuthoringFieldDescriptor{"type", "Type"}, lightType, typeChoices)) {
        next.type = static_cast<SceneLightType>(std::clamp(lightType, 0, static_cast<int>(typeChoices.size()) - 1));
    }

    builder.Color3(AuthoringFieldDescriptor{"color", "Color"}, next.color.x, next.color.y, next.color.z);
    builder.FloatRange(
        AuthoringFieldDescriptor{"intensity", "Luminous Flux (lm)",
                                 "Total emitted light. A 60 W bulb is about 800 lm. Spot cones mask, they do not "
                                 "concentrate the flux; rect lights emit it from the front face only."},
        next.intensity, 0.0f, kMaximumLightLumens, 10.0f);
    builder.Int(AuthoringFieldDescriptor{"priority", "Priority"}, next.priority);
    builder.Bool(AuthoringFieldDescriptor{"castsShadows", "Cast Shadows",
                                          "Request local-light shadows within the active rendering budget."},
                 next.castsShadows);
    builder.FloatRange(AuthoringFieldDescriptor{"range", "Range (m)",
                                                "Inverse-square falloff fades smoothly to zero at this distance."},
                       next.range, 0.05f, 1000.0f, 0.1f);
    const bool tube = next.type == SceneLightType::Tube;
    const bool rect = next.type == SceneLightType::Rect;
    if (!rect) {
        builder.FloatRange(
            AuthoringFieldDescriptor{
                "sourceRadius", tube ? "Tube Radius" : "Source Radius",
                "Emitter radius in metres. Broadens direct specular independently of material roughness and light range. Zero preserves punctual lighting."},
            next.sourceRadius, 0.0f, RENDER3D::LIGHTING::kMaximumLocalLightSourceRadius, 0.01f);
    }
    if (tube || rect) {
        builder.Text(tube ? "Tube (along local X)" : "Rect Emitter (local X width, Y height, lights +Z)");
        builder.FloatRange(AuthoringFieldDescriptor{"length", tube ? "Length (m)" : "Width (m)"}, next.length, 0.01f,
                           kMaximumAreaLightExtent, 0.01f);
        if (rect) {
            builder.FloatRange(AuthoringFieldDescriptor{"height", "Height (m)"}, next.height, 0.01f,
                               kMaximumAreaLightExtent, 0.01f);
        }
    }
    if (next.type == SceneLightType::Spot) {
        builder.Text("Spot Cone (+Z forward)");
        builder.FloatRange(AuthoringFieldDescriptor{"innerConeAngleDegrees", "Inner Cone Angle"},
                           next.innerConeAngleDegrees, 0.1f, 89.0f, 0.1f);
        builder.FloatRange(AuthoringFieldDescriptor{"outerConeAngleDegrees", "Outer Cone Angle"},
                           next.outerConeAngleDegrees, 0.1f, 89.0f, 0.1f);
        builder.FloatRange(AuthoringFieldDescriptor{"angularFalloff", "Angular Falloff"}, next.angularFalloff, 0.05f,
                           8.0f, 0.05f);
        builder.AssetIdPicker(
            AuthoringFieldDescriptor{"cookie", "Cookie Texture",
                                     "Projected colour pattern multiplied into the light. It fills the outer cone and "
                                     "reads unmirrored from behind the light (texture up = local +Y)."},
            AssetType::Texture, next.cookie);
    }
    if (!tube && !rect) {
        builder.AssetIdPicker(
            AuthoringFieldDescriptor{
                "iesProfile", "IES Profile",
                "Photometric distribution (.ies, Type C). Vertical 0 degrees points along local +Z, "
                "horizontal 0 degrees along local +X. The flux setting stays the total output."},
            AssetType::LightProfile, next.iesProfile);
    }
    SetSettings(next);
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
