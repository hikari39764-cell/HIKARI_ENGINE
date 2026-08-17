#include "Scene/Features/HIKARI_RenderingRuntimeFeature.h"

#include <array>
#include <memory>

#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"
#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponentAuthoring.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/Features/HIKARI_BuoyancyComponent.h"
#include "Scene/Features/HIKARI_BuoyancySystem.h"
#include "Scene/Water/HIKARI_WaterSurfaceSystem.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/Sand/HIKARI_SandSurfaceFollowerComponent.h"
#include "Scene/Sand/HIKARI_SandSurfaceSystem.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Scene/Water/HIKARI_WaterSurfaceComponent.h"

namespace HIKARI {
    namespace {
        ComponentTypePresentation RenderingPresentation(
            const char* displayName,
            const char* description) {

            return ComponentTypePresentation{
                displayName,
                "Rendering",
                description,
                "Rendering"
            };
        }

        class RenderingRuntimeFeature final : public IRuntimeFeature {
        public:
            std::string_view GetFeatureId() const noexcept override {
                return RuntimeFeatureIds::Rendering;
            }

            std::string_view GetDisplayName() const noexcept override {
                return "Rendering";
            }

            std::string_view GetDescription() const noexcept override {
                return "Model rendering and procedural geometry support.";
            }

            std::span<const std::string_view>
                GetComponentTypeNames() const noexcept override {
                static constexpr std::array<std::string_view, 9> names{
                    "ModelComponent",
                    "MaterialFxComponent",
                    "ProceduralMeshComponent",
                    "TerrainSurfaceComponent",
                    "WaterSurfaceComponent",
                    "SandSurfaceFollowerComponent",
                    "LightComponent",
                    "ReflectionProbeComponent",
                    "IrradianceVolumeComponent"
                };
                return names;
            }

            std::span<const std::string_view>
                GetSystemIds() const noexcept override {
                static constexpr std::array<std::string_view, 3> ids{
                    "ModelRenderSystem",
                    "WaterSurfaceSystem",
                    "SandSurfaceSystem"
                };
                return ids;
            }

            bool Register(RuntimeFeatureContext& context) override {
                ComponentRegistry& componentRegistry =
                    RuntimeFeatureContextAccess::Components(context);
                SystemTypeRegistry& systemTypeRegistry =
                    RuntimeFeatureContextAccess::Systems(context);
                ComponentSystemPolicy& componentSystemPolicy =
                    RuntimeFeatureContextAccess::Policy(context);
                ComponentTypeInfo model{};
                model.typeName = "ModelComponent";
                model.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<ModelComponent>();
                };
                model.optionalComponents = {
                    "MaterialFxComponent",
                    "ProceduralMeshComponent"
                };
                model.presentation = RenderingPresentation(
                    "Model",
                    "Controls the model asset, material slots, visibility, shadows, and render diagnostics.");
                model.authoringCommands =
                    BuildModelComponentAuthoringCommands();

                ComponentTypeInfo materialFx{};
                materialFx.typeName = "MaterialFxComponent";
                materialFx.factory =
                    []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<MaterialFxComponent>();
                };
                materialFx.requiredComponents = {
                    "ModelComponent"
                };
                materialFx.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {

                    properties = {
                        { "profileId", "" },
                        { "valuesInitialized", false }
                    };
                };
                materialFx.presentation = RenderingPresentation(
                    "Material FX",
                    "Applies an optional Material FX profile and per-object parameter overrides.");

                ComponentTypeInfo procedural{};
                procedural.typeName = "ProceduralMeshComponent";
                procedural.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<ProceduralMeshComponent>();
                };
                procedural.requiredComponents = { "ModelComponent" };
                procedural.runtimeApplyPolicy =
                    ComponentRuntimeApplyPolicy::OnEditCommit;
                procedural.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties = {
                        { "kind", "Plane" },
                        { "width", 1.0f },
                        { "height", 1.0f },
                        { "depth", 1.0f },
                        { "segmentsX", 10u },
                        { "segmentsY", 10u },
                        { "segmentsZ", 1u },
                        { "sphereSlices", 32u },
                        { "sphereStacks", 16u },
                        { "doubleSided", true },
                        { "generateTangents", true }
                    };
                };
                procedural.presentation = RenderingPresentation(
                    "Procedural Mesh",
                    "Generates editable plane, box, sphere, cylinder, or capsule geometry.");

                ComponentTypeInfo terrainSurface{};
                terrainSurface.typeName = "TerrainSurfaceComponent";
                terrainSurface.factory =
                    []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<TerrainSurfaceComponent>();
                };
                terrainSurface.requiredComponents = {
                    "ModelComponent",
                    "ProceduralMeshComponent"
                };
                terrainSurface.runtimeApplyPolicy =
                    ComponentRuntimeApplyPolicy::OnEditCommit;
                terrainSurface.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    TerrainSurfaceComponent defaults{};
                    defaults.Serialize(properties);
                };
                terrainSurface.presentation = RenderingPresentation(
                    "Terrain Surface",
                    "Unified editable solid terrain with heightfield and paintable layers.");

                ComponentTypeInfo waterSurface{};
                waterSurface.typeName = "WaterSurfaceComponent";
                waterSurface.factory =
                    []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<WaterSurfaceComponent>();
                };
                waterSurface.requiredComponents = {
                    "ModelComponent",
                    "ProceduralMeshComponent"
                };
                waterSurface.runtimeApplyPolicy =
                    ComponentRuntimeApplyPolicy::OnEditCommit;
                waterSurface.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    WaterSurfaceComponent defaults{};
                    defaults.Serialize(properties);
                };
                waterSurface.presentation = RenderingPresentation(
                    "Water Surface",
                    "Gerstner-driven animated water surface with stylized or realistic shading.");
                terrainSurface.authoringCommands = {
                    {
                        "terrain.surfacePreset",
                        "Apply a solid surface preset (sand, snow, dirt).",
                        {
                            { "presetId", AuthoringCommandParameterType::String, true, "Preset id: sand, snow, or dirt." },
                        },
                        [](IComponent& base, const nlohmann::json& args) {
                            auto* component =
                                dynamic_cast<TerrainSurfaceComponent*>(&base);
                            if (component == nullptr) {
                                return AuthoringCommandResult{
                                    false,
                                    "TYPE_MISMATCH",
                                    "Component is not a terrain surface."
                                };
                            }
                            component->ApplySolidPreset(
                                args.value(
                                    "presetId",
                                    std::string("sand")));
                            return AuthoringCommandResult{
                                true,
                                "OK",
                                "Terrain solid preset applied."
                            };
                        }
                    },
                    {
                        "terrain.sculpt",
                        "Sculpt one brush stroke into the authored terrain.",
                        {
                            { "worldX", AuthoringCommandParameterType::Float, true, "Brush center world X." },
                            { "worldY", AuthoringCommandParameterType::Float, false, "Brush center world Y." },
                            { "worldZ", AuthoringCommandParameterType::Float, true, "Brush center world Z." },
                        },
                        [](IComponent& base, const nlohmann::json& args) {
                            auto* component =
                                dynamic_cast<TerrainSurfaceComponent*>(&base);
                            if (component == nullptr) {
                                return AuthoringCommandResult{
                                    false,
                                    "TYPE_MISMATCH",
                                    "Component is not a terrain surface."
                                };
                            }
                            component->SculptAtWorld({
                                args.value("worldX", 0.0f),
                                args.value("worldY", 0.0f),
                                args.value("worldZ", 0.0f)
                            });
                            return AuthoringCommandResult{
                                true,
                                "OK",
                                "Terrain sculpted."
                            };
                        }
                    },
                    {
                        "terrain.reset",
                        "Clear the authored terrain to flat.",
                        {},
                        [](IComponent& base, const nlohmann::json&) {
                            auto* component =
                                dynamic_cast<TerrainSurfaceComponent*>(&base);
                            if (component == nullptr) {
                                return AuthoringCommandResult{
                                    false,
                                    "TYPE_MISMATCH",
                                    "Component is not a terrain surface."
                                };
                            }
                            component->ResetTerrain();
                            return AuthoringCommandResult{
                                true,
                                "OK",
                                "Terrain reset."
                            };
                        }
                    },
                    {
                        "terrain.paintLayer",
                        "Paint one preset layer stroke at a world position.",
                        {
                            { "presetId", AuthoringCommandParameterType::String, true, "Preset id: sand, snow, or dirt." },
                            { "worldX", AuthoringCommandParameterType::Float, true, "Brush center world X." },
                            { "worldY", AuthoringCommandParameterType::Float, false, "Brush center world Y." },
                            { "worldZ", AuthoringCommandParameterType::Float, true, "Brush center world Z." },
                        },
                        [](IComponent& base, const nlohmann::json& args) {
                            auto* component =
                                dynamic_cast<TerrainSurfaceComponent*>(&base);
                            if (component == nullptr) {
                                return AuthoringCommandResult{
                                    false,
                                    "TYPE_MISMATCH",
                                    "Component is not a terrain surface."
                                };
                            }
                            component->PaintLayerAtWorld(
                                args.value("presetId", std::string("sand")),
                                {
                                    args.value("worldX", 0.0f),
                                    args.value("worldY", 0.0f),
                                    args.value("worldZ", 0.0f)
                                });
                            return AuthoringCommandResult{
                                true,
                                "OK",
                                "Terrain layer painted."
                            };
                        }
                    },
                    {
                        "terrain.paintCoverage",
                        "Paint or erase terrain coverage at a world position.",
                        {
                            { "target", AuthoringCommandParameterType::Float, true, "Target coverage 1=paint, 0=erase." },
                            { "worldX", AuthoringCommandParameterType::Float, true, "Brush center world X." },
                            { "worldY", AuthoringCommandParameterType::Float, false, "Brush center world Y." },
                            { "worldZ", AuthoringCommandParameterType::Float, true, "Brush center world Z." },
                        },
                        [](IComponent& base, const nlohmann::json& args) {
                            auto* component =
                                dynamic_cast<TerrainSurfaceComponent*>(&base);
                            if (component == nullptr) {
                                return AuthoringCommandResult{
                                    false,
                                    "TYPE_MISMATCH",
                                    "Component is not a terrain surface."
                                };
                            }
                            component->PaintCoverageAtWorld(
                                args.value("target", 1.0f),
                                {
                                    args.value("worldX", 0.0f),
                                    args.value("worldY", 0.0f),
                                    args.value("worldZ", 0.0f)
                                });
                            return AuthoringCommandResult{
                                true,
                                "OK",
                                "Terrain coverage painted."
                            };
                        }
                    }
                };

                ComponentTypeInfo sandFollower{};
                sandFollower.typeName = "SandSurfaceFollowerComponent";
                sandFollower.factory =
                    []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<
                        SandSurfaceFollowerComponent>();
                };
                sandFollower.runtimeApplyPolicy =
                    ComponentRuntimeApplyPolicy::Immediate;
                sandFollower.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    SandSurfaceFollowerComponent defaults{};
                    defaults.Serialize(properties);
                };
                sandFollower.presentation = RenderingPresentation(
                    "Sand Ball Follower",
                    "Attach this to the protagonist ball so it visually "
                    "rides the sampled sand height; physics stays "
                    "authoritative. The sand ground itself uses "
                    "SandSurfaceComponent.");

                ComponentTypeInfo buoyancy{};
                buoyancy.typeName = "BuoyancyComponent";
                buoyancy.factory =
                    []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<BuoyancyComponent>();
                };
                buoyancy.runtimeApplyPolicy =
                    ComponentRuntimeApplyPolicy::Immediate;
                buoyancy.presentation = RenderingPresentation(
                    "Buoyancy",
                    "Applies a buoyant force to a floating sphere from the water surface query.");

                ComponentTypeInfo light{};
                light.typeName = "LightComponent";
                light.factory =
                    []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<LightComponent>();
                };
                light.runtimeApplyPolicy =
                    ComponentRuntimeApplyPolicy::Immediate;
                light.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    LightComponent defaults{};
                    defaults.Serialize(properties);
                };
                light.presentation = ComponentTypePresentation{
                    "Light",
                    "Lighting",
                    "Provides a directional, point, or spot light using the owning object's transform.",
                    std::string(RuntimeFeatureIds::Rendering)
                };

                ComponentTypeInfo reflectionProbe{};
                reflectionProbe.typeName = "ReflectionProbeComponent";
                reflectionProbe.factory =
                    []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<ReflectionProbeComponent>();
                };
                reflectionProbe.runtimeApplyPolicy =
                    ComponentRuntimeApplyPolicy::Immediate;
                reflectionProbe.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    ReflectionProbeComponent defaults{};
                    defaults.Serialize(properties);
                };
                reflectionProbe.presentation = ComponentTypePresentation{
                    "Reflection Probe",
                    "Lighting",
                    "Captures and blends local specular environment lighting.",
                    std::string(RuntimeFeatureIds::Rendering)
                };

                ComponentTypeInfo irradianceVolume{};
                irradianceVolume.typeName =
                    "IrradianceVolumeComponent";
                irradianceVolume.factory =
                    []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<
                        IrradianceVolumeComponent>();
                };
                irradianceVolume.runtimeApplyPolicy =
                    ComponentRuntimeApplyPolicy::Immediate;
                irradianceVolume.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    IrradianceVolumeComponent defaults{};
                    defaults.Serialize(properties);
                };
                irradianceVolume.presentation =
                    ComponentTypePresentation{
                        "Irradiance Volume",
                        "Lighting",
                        "Defines a baked diffuse-light probe grid.",
                        std::string(RuntimeFeatureIds::Rendering)
                    };

                bool success = componentRegistry.Register(
                    std::move(model));
                success = componentRegistry.Register(
                    std::move(materialFx)) && success;
                success = componentRegistry.Register(
                    std::move(procedural)) && success;
                success = componentRegistry.Register(
                    std::move(terrainSurface)) && success;
                success = componentRegistry.Register(
                    std::move(waterSurface)) && success;
                success = componentRegistry.Register(
                    std::move(sandFollower)) && success;
                success = componentRegistry.Register(
                    std::move(buoyancy)) && success;
                success = componentRegistry.Register(
                    std::move(light)) && success;
                success = componentRegistry.Register(
                    std::move(reflectionProbe)) && success;
                success = componentRegistry.Register(
                    std::move(irradianceVolume)) && success;
                success = systemTypeRegistry.Register(SystemTypeInfo{
                    "ModelRenderSystem",
                    [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return std::make_unique<RenderSubmissionSystem>();
                    },
                    "Model Rendering",
                    "Rendering"
                }) && success;
                success = systemTypeRegistry.Register(SystemTypeInfo{
                    "WaterSurfaceSystem",
                    [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return std::make_unique<WaterSurfaceSystem>();
                    },
                    "Water Surface",
                    "Rendering"
                }) && success;
                success = systemTypeRegistry.Register(SystemTypeInfo{
                    "SandSurfaceSystem",
                    [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return std::make_unique<SandSurfaceSystem>();
                    },
                    "Sand Surface",
                    "Rendering"
                }) && success;
                success = systemTypeRegistry.Register(SystemTypeInfo{
                    "BuoyancySystem",
                    [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return std::make_unique<BuoyancySystem>();
                    },
                    "Buoyancy",
                    "Gameplay"
                }) && success;
                // 水面/砂地のシステムは Scene が明示的な systems 配列で
                // 上書きしていても、コンポーネントが存在すれば必ず導入する。
                success = componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "WaterSurfaceComponent",
                        "WaterSurfaceSystem",
                        95
                    }) && success;
                success = componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "TerrainSurfaceComponent",
                        "SandSurfaceSystem",
                        95
                    }) && success;
                success = componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "SandSurfaceFollowerComponent",
                        "SandSurfaceSystem",
                        95
                    }) && success;
                success = componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "BuoyancyComponent",
                        "BuoyancySystem",
                        90
                    }) && success;
                return success;
            }

            std::span<const RuntimeSystemDefault>
                GetDefaultSystems() const noexcept override {
                static constexpr std::array<
                    RuntimeSystemDefault,
                    3> defaults{
                    RuntimeSystemDefault{
                        "ModelRenderSystem",
                        true,
                        100
                    },
                    RuntimeSystemDefault{
                        "WaterSurfaceSystem",
                        true,
                        95
                    },
                    RuntimeSystemDefault{
                        "SandSurfaceSystem",
                        true,
                        95
                    }
                };
                return defaults;
            }
        };
    }

    std::unique_ptr<IRuntimeFeature> CreateRenderingRuntimeFeature() {
        return std::make_unique<RenderingRuntimeFeature>();
    }

} // namespace HIKARI
