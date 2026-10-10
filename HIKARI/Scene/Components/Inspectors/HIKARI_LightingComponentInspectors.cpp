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

// 撮影解像度は決まった三段から選ぶ。選択肢 ID は解像度の数字。
bool CaptureResolutionChoice(IInspectorBuilder& builder, uint32_t& resolution,
                             const std::array<uint32_t, 3>& resolutions,
                             const std::array<AuthoringChoice, 3>& choices) {
    int selected = resolution <= resolutions[0] ? 0 : (resolution <= resolutions[1] ? 1 : 2);
    if (!builder.Choice(AuthoringFieldDescriptor{"captureResolution", "Capture Quality"}, selected, choices))
        return false;
    resolution = resolutions[static_cast<size_t>(std::clamp(selected, 0, 2))];
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
    int unit = static_cast<int>(next.intensityUnit);
    static constexpr std::array<AuthoringChoice, 2> unitChoices{AuthoringChoice{"Lumens", "Physical (lm)"},
                                                                AuthoringChoice{"SceneRelative", "Relative to Scene"}};
    if (builder.Choice(
            AuthoringFieldDescriptor{"intensityUnit", "Brightness Unit",
                                     "Physical: real lamps in lumens; they fade out in daylight as real lamps "
                                     "do. Relative: a multiple of the sun and sky light, so fill and accent "
                                     "lights keep their look across time of day, weather and presets."},
            unit, unitChoices)) {
        // 単位を変えても今の明るさを保つ。
        next = ConvertLightIntensityUnit(next, static_cast<LightIntensityUnit>(std::clamp(unit, 0, 1)));
    }
    if (next.intensityUnit == LightIntensityUnit::SceneRelative) {
        builder.FloatRange(AuthoringFieldDescriptor{"sceneRelativeIntensity", "Brightness (x scene light at 1 m)",
                                                    "1 lights a surface 1 m away as brightly as the sun and sky light "
                                                    "the open ground."},
                           next.sceneRelativeIntensity, 0.0f, kMaximumSceneRelativeIntensity, 0.01f);
    } else {
        builder.FloatRange(
            AuthoringFieldDescriptor{"intensity", "Luminous Flux (lm)",
                                     "Total emitted light. A 60 W bulb is about 800 lm. Spot cones mask, they do not "
                                     "concentrate the flux; rect lights emit it from the front face only."},
            next.intensity, 0.0f, kMaximumLightLumens, 10.0f);
    }
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
        builder.FloatRange(
            AuthoringFieldDescriptor{.id = "innerConeAngleDegrees", .label = "Inner Cone Angle", .slider = true},
            next.innerConeAngleDegrees, 0.1f, 89.0f, 0.1f);
        builder.FloatRange(
            AuthoringFieldDescriptor{.id = "outerConeAngleDegrees", .label = "Outer Cone Angle", .slider = true},
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
    using D = AuthoringFieldDescriptor;
    builder.Text("Capture");
    builder.Bool(D{"enabled", "Enabled"}, enabled_);
    builder.String(D{"sourceCubemapAsset", "Source Cubemap Asset"}, sourceCubemapAsset_);
    builder.Float(D{"radius", "Sphere / Capture Radius"}, radius_);
    builder.Float(D{"intensity", "Intensity"}, intensity_);
    if (builder.Button("Fit Influence Size To Scene")) {
        MATH::Vec3 sceneSize{};
        if (MeasureSceneExtent(builder.GetContext().sceneDocument, sceneSize)) {
            influenceBoxSize_ = sceneSize;
            projectionBoxSize_ = sceneSize;
            radius_ =
                0.5f * std::sqrt(sceneSize.x * sceneSize.x + sceneSize.y * sceneSize.y + sceneSize.z * sceneSize.z);
        }
    }
    static constexpr std::array<AuthoringChoice, 3> captureChoices{AuthoringChoice{"64", "64 - Preview"},
                                                                   AuthoringChoice{"128", "128 - Standard"},
                                                                   AuthoringChoice{"256", "256 - High"}};
    (void)CaptureResolutionChoice(builder, captureResolution_, {64u, 128u, 256u}, captureChoices);
    builder.Text("Higher quality uses a full roughness mip chain and more samples.");

    builder.Text("Influence");
    (void)EnumChoice(builder, D{"influenceShape", "Influence Shape"}, influenceShape_,
                     kReflectionProbeInfluenceShapeNames);
    builder.Vec3(D{"influenceBoxOffset", "Influence Offset"}, influenceBoxOffset_.x, influenceBoxOffset_.y,
                 influenceBoxOffset_.z);
    builder.Vec3(D{"influenceBoxSize", "Influence Size"}, influenceBoxSize_.x, influenceBoxSize_.y,
                 influenceBoxSize_.z);
    builder.Float(D{"blendDistance", "Blend Distance"}, blendDistance_);
    builder.Int(D{"priority", "Priority"}, priority_);

    builder.Text("Projection");
    const bool wasLinked = projectionFollowsInfluence_;
    builder.Bool(D{"projectionFollowsInfluence", "Link Projection To Influence"}, projectionFollowsInfluence_);
    if (!wasLinked && projectionFollowsInfluence_) {
        SetProjectionFollowsInfluence(true);
    }

    if (projectionFollowsInfluence_) {
        AlignProjectionToInfluence();
        viewportBoundsTarget_ = ReflectionProbeViewportBoundsTarget::Influence;
        builder.Text("Projection Shape: Box (linked)");
        builder.Text("Viewport Bounds: Linked Influence + Projection");
    } else {
        (void)EnumChoice(builder, D{"projectionShape", "Projection Shape"}, projectionShape_,
                         kReflectionProbeProjectionShapeNames);
        builder.Vec3(D{"projectionBoxOffset", "Projection Offset"}, projectionBoxOffset_.x, projectionBoxOffset_.y,
                     projectionBoxOffset_.z);
        builder.Vec3(D{"projectionBoxSize", "Projection Size"}, projectionBoxSize_.x, projectionBoxSize_.y,
                     projectionBoxSize_.z);
        if (builder.Button("Match Projection To Influence")) {
            AlignProjectionToInfluence();
        }

        if (projectionShape_ == ReflectionProbeProjectionShape::Box) {
            (void)EnumChoice(builder, D{"viewportBoundsTarget", "Viewport Bounds Target"}, viewportBoundsTarget_,
                             kReflectionProbeViewportBoundsTargetNames);
        } else {
            viewportBoundsTarget_ = ReflectionProbeViewportBoundsTarget::Influence;
            builder.Text("Viewport Bounds: Influence");
        }
    }

    builder.Text("Bake Routing");
    builder.String(D{"bakeSet", "Bake Set"}, bakeSet_);
    builder.String(D{"scenario", "Scenario"}, scenario_);
    ClampSettings();
}

void IrradianceVolumeComponent::BuildInspector(IInspectorBuilder& builder) {
    using D = AuthoringFieldDescriptor;
    builder.Text("Volume");
    builder.Bool(D{"enabled", "Enabled"}, enabled_);
    (void)EnumChoice(builder, D{"mode", "Mode"}, mode_, kLightProbeVolumeModeNames);
    if (mode_ == LightProbeVolumeMode::Realtime)
        builder.Text("Realtime volumes are updated by ray-traced dynamic GI and need no bake.");
    builder.Vec3(D{"size", "Size"}, size_.x, size_.y, size_.z);
    builder.Float(D{"intensity", "Intensity"}, intensity_);
    builder.Float(D{"blendDistance", "Blend Distance"}, blendDistance_);
    builder.FloatRange(D{"normalBias", "Surface Normal Bias"}, normalBias_, 0.0f, 0.5f, 0.01f);
    builder.Text("Normal bias samples GI slightly away from walls to reduce leaks.");
    builder.Int(D{"priority", "Priority"}, priority_);
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
    builder.Int(D{"countX", "Count X"}, countX);
    builder.Int(D{"countY", "Count Y"}, countY);
    builder.Int(D{"countZ", "Count Z"}, countZ);
    countX_ = static_cast<uint32_t>((std::max)(countX, 1));
    countY_ = static_cast<uint32_t>((std::max)(countY, 1));
    countZ_ = static_cast<uint32_t>((std::max)(countZ, 1));
    static constexpr std::array<AuthoringChoice, 3> captureChoices{AuthoringChoice{"16", "16 - Preview"},
                                                                   AuthoringChoice{"32", "32 - Standard"},
                                                                   AuthoringChoice{"64", "64 - High"}};
    (void)CaptureResolutionChoice(builder, captureResolution_, {16u, 32u, 64u}, captureChoices);

    builder.Text("Bake Routing");
    builder.String(D{"bakeSet", "Bake Set"}, bakeSet_);
    builder.String(D{"scenario", "Scenario"}, scenario_);
    ClampSettings();
}

} // namespace HIKARI

#endif // defined(HIKARI_WITH_EDITOR)
