#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/Components/Lighting/HIKARI_IrradianceVolumeComponent.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Components/Lighting/HIKARI_ReflectionProbeComponent.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

namespace {
ComponentTypePresentation LightingPresentation(const char* displayName, const char* description) {
    return ComponentTypePresentation{displayName, "Lighting", description, std::string(RuntimeFeatureIds::Lighting)};
}

class LightingRuntimeFeature final : public IRuntimeFeature {
public:
    std::string_view GetFeatureId() const noexcept override { return RuntimeFeatureIds::Lighting; }

    std::string_view GetDisplayName() const noexcept override { return "Lighting"; }

    std::string_view GetDescription() const noexcept override {
        return "Runtime lights and environment-lighting providers.";
    }

    std::span<const std::string_view> GetRequiredFeatureIds() const noexcept override {
        static constexpr std::array<std::string_view, 1> ids{RuntimeFeatureIds::Rendering};
        return ids;
    }

    bool Register(RuntimeFeatureContext& context) override {
        ComponentRegistry& components = RuntimeFeatureContextAccess::Components(context);

        ComponentTypeInfo light{};
        light.typeName = "LightComponent";
        light.factory = []() -> std::unique_ptr<IComponent> { return IComponent::Create<LightComponent>(); };
        light.runtimeApplyPolicy = ComponentRuntimeApplyPolicy::Immediate;
        light.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            LightComponent defaults{};
            defaults.Serialize(properties);
        };
        light.presentation =
            LightingPresentation("Light", "Point, spot, tube, or rect light from the object transform.");

        ComponentTypeInfo reflectionProbe{};
        reflectionProbe.typeName = "ReflectionProbeComponent";
        reflectionProbe.factory = []() -> std::unique_ptr<IComponent> {
            return IComponent::Create<ReflectionProbeComponent>();
        };
        reflectionProbe.runtimeApplyPolicy = ComponentRuntimeApplyPolicy::Immediate;
        reflectionProbe.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            ReflectionProbeComponent defaults{};
            defaults.Serialize(properties);
        };
        reflectionProbe.presentation =
            LightingPresentation("Reflection Probe", "Captures and blends local specular environment lighting.");

        ComponentTypeInfo irradianceVolume{};
        irradianceVolume.typeName = "IrradianceVolumeComponent";
        irradianceVolume.factory = []() -> std::unique_ptr<IComponent> {
            return IComponent::Create<IrradianceVolumeComponent>();
        };
        irradianceVolume.runtimeApplyPolicy = ComponentRuntimeApplyPolicy::Immediate;
        irradianceVolume.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            IrradianceVolumeComponent defaults{};
            defaults.Serialize(properties);
        };
        irradianceVolume.presentation =
            LightingPresentation("Irradiance Volume", "Defines a baked diffuse-light probe grid.");

        bool success = components.Register(std::move(light));
        success = components.Register(std::move(reflectionProbe)) && success;
        success = components.Register(std::move(irradianceVolume)) && success;
        return success;
    }
};
} // namespace

std::unique_ptr<IRuntimeFeature> CreateLightingRuntimeFeature() { return std::make_unique<LightingRuntimeFeature>(); }

} // namespace HIKARI
