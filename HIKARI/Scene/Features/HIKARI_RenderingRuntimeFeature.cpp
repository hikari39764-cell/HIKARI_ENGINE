#include "Scene/Features/HIKARI_RenderingRuntimeFeature.h"

#include <array>
#include <memory>

#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"
#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"
#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

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
                static constexpr std::array<std::string_view, 5> names{
                    "ModelComponent",
                    "MaterialFxComponent",
                    "ProceduralMeshComponent",
                    "ReflectionProbeComponent",
                    "IrradianceVolumeComponent"
                };
                return names;
            }

            std::span<const std::string_view>
                GetSystemIds() const noexcept override {
                static constexpr std::array<std::string_view, 1> ids{
                    "ModelRenderSystem"
                };
                return ids;
            }

            bool Register(RuntimeFeatureContext& context) override {
                ComponentRegistry& componentRegistry =
                    RuntimeFeatureContextAccess::Components(context);
                SystemTypeRegistry& systemTypeRegistry =
                    RuntimeFeatureContextAccess::Systems(context);
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
                        { "kind", "Box" },
                        { "width", 1.0f },
                        { "height", 1.0f },
                        { "depth", 1.0f },
                        { "segmentsX", 10u },
                        { "segmentsY", 10u },
                        { "segmentsZ", 1u },
                        { "sphereSlices", 32u },
                        { "sphereStacks", 16u },
                        { "doubleSided", false },
                        { "generateTangents", true }
                    };
                };
                procedural.presentation = RenderingPresentation(
                    "Procedural Mesh",
                    "Generates editable plane, box, sphere, cylinder, or capsule geometry.");

                ComponentTypeInfo reflectionProbe{};
                reflectionProbe.typeName = "ReflectionProbeComponent";
                reflectionProbe.factory =
                    []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<ReflectionProbeComponent>();
                };
                reflectionProbe.runtimeApplyPolicy =
                    ComponentRuntimeApplyPolicy::OnEditCommit;
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
                    ComponentRuntimeApplyPolicy::OnEditCommit;
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
                return success;
            }

            std::span<const RuntimeSystemDefault>
                GetDefaultSystems() const noexcept override {
                static constexpr std::array<
                    RuntimeSystemDefault,
                    1> defaults{
                    RuntimeSystemDefault{
                        "ModelRenderSystem",
                        true,
                        100
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
