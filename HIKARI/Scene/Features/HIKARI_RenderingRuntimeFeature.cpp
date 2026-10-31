#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

#include "Scene/Features/HIKARI_RuntimeFeatureContextAccess.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Components/Rendering/Decal/HIKARI_DecalComponent.h"
#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponentAuthoring.h"
#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpriteComponent.h"
#include "Scene/Components/Rendering/WorldText/HIKARI_WorldTextComponent.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

namespace HIKARI {

namespace {
ComponentTypePresentation RenderingPresentation(const char* displayName, const char* description) {
    return ComponentTypePresentation{displayName, "Rendering", description, std::string(RuntimeFeatureIds::Rendering)};
}

class RenderingRuntimeFeature final : public IRuntimeFeature {
public:
    std::string_view GetFeatureId() const noexcept override { return RuntimeFeatureIds::Rendering; }

    std::string_view GetDisplayName() const noexcept override { return "Rendering Core"; }

    std::string_view GetDescription() const noexcept override {
        return "Model, material, procedural geometry, decal, world sprite, "
               "and world text rendering.";
    }

    bool Register(RuntimeFeatureContext& context) override {
        ComponentRegistry& components = RuntimeFeatureContextAccess::Components(context);
        SystemTypeRegistry& systems = RuntimeFeatureContextAccess::Systems(context);

        ComponentTypeInfo model{};
        model.typeName = "ModelComponent";
        model.factory = []() -> std::unique_ptr<IComponent> { return IComponent::Create<ModelComponent>(); };
        model.affectsRenderState = true;
        model.optionalComponents = {"MaterialFxComponent", "ProceduralMeshComponent"};
        model.presentation =
            RenderingPresentation("Model", "Controls model assets, materials, visibility, and shadows.");
        model.authoringCommands = BuildModelComponentAuthoringCommands();

        ComponentTypeInfo materialFx{};
        materialFx.typeName = "MaterialFxComponent";
        materialFx.factory = []() -> std::unique_ptr<IComponent> { return IComponent::Create<MaterialFxComponent>(); };
        materialFx.requiredComponents = {"ModelComponent"};
        materialFx.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            properties = {{"profileId", ""}, {"valuesInitialized", false}};
        };
        materialFx.presentation =
            RenderingPresentation("Material FX", "Applies a Material FX profile and per-object overrides.");

        ComponentTypeInfo procedural{};
        procedural.typeName = "ProceduralMeshComponent";
        procedural.factory = []() -> std::unique_ptr<IComponent> {
            return IComponent::Create<ProceduralMeshComponent>();
        };
        procedural.requiredComponents = {"ModelComponent"};
        procedural.runtimeApplyPolicy = ComponentRuntimeApplyPolicy::OnEditCommit;
        procedural.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            properties = {{"kind", "Plane"},     {"width", 1.0f},           {"height", 1.0f},
                          {"depth", 1.0f},       {"segmentsX", 10u},        {"segmentsY", 10u},
                          {"segmentsZ", 1u},     {"sphereSlices", 32u},     {"sphereStacks", 16u},
                          {"doubleSided", true}, {"generateTangents", true}};
        };
        procedural.presentation = RenderingPresentation("Procedural Mesh", "Generates editable primitive geometry.");

        ComponentTypeInfo decal{};
        decal.typeName = "DecalComponent";
        decal.factory = []() -> std::unique_ptr<IComponent> { return IComponent::Create<DecalComponent>(); };
        decal.runtimeApplyPolicy = ComponentRuntimeApplyPolicy::Immediate;
        decal.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            DecalComponent{}.Serialize(properties);
        };
        decal.fields = JSONFIELDS::Describe<RENDER3D::DECAL::DecalSettings>(kDecalSettingsFields);
        decal.presentation = RenderingPresentation(
            "Decal", "Projects colour, normal, and roughness onto opaque surfaces inside a box along local +Z.");

        ComponentTypeInfo worldSprite{};
        worldSprite.typeName = "WorldSpriteComponent";
        worldSprite.factory = []() -> std::unique_ptr<IComponent> {
            return IComponent::Create<WorldSpriteComponent>();
        };
        worldSprite.affectsRenderState = true;
        worldSprite.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            WorldSpriteComponent{}.Serialize(properties);
        };
        worldSprite.presentation =
            RenderingPresentation("World Sprite (3D)", "Displays a texture directly on an engine-owned quad in "
                                                       "3D space, including transparent textures.");

        ComponentTypeInfo worldText{};
        worldText.typeName = "WorldTextComponent";
        worldText.factory = []() -> std::unique_ptr<IComponent> { return IComponent::Create<WorldTextComponent>(); };
        worldText.affectsRenderState = true;
        worldText.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            properties = {{"version", 1u},
                          {"text", "World Text"},
                          {"fontAssetPath", "Fonts/NotoSansCJKjp-Regular.otf"},
                          {"visible", true},
                          {"layoutSize", {4.0f, 1.0f}},
                          {"fontHeight", 0.5f},
                          {"horizontalAlign", "Center"},
                          {"verticalAlign", "Center"},
                          {"wrap", "Character"},
                          {"lineHeightMultiplier", 1.2f},
                          {"letterSpacing", 0.0f},
                          {"facing", "CameraFacing"},
                          {"color", {1.0f, 1.0f, 1.0f}},
                          {"outlineColor", {0.0f, 0.0f, 0.0f}},
                          {"outlineWidth", 0.04f},
                          {"opacity", 1.0f},
                          {"revealAmount", 1.0f},
                          {"pivot", {0.5f, 0.5f}},
                          {"transparentSortPriority", 0}};
        };
        worldText.presentation =
            RenderingPresentation("World Text (3D)", "Lays out UTF-8 SDF text in world space with outline, "
                                                     "wrapping, alignment, and reveal controls.");

        bool success = components.Register(std::move(model));
        success = components.Register(std::move(materialFx)) && success;
        success = components.Register(std::move(procedural)) && success;
        success = components.Register(std::move(decal)) && success;
        success = components.Register(std::move(worldSprite)) && success;
        success = components.Register(std::move(worldText)) && success;
        success = systems.Register(SystemTypeInfo{"ModelRenderSystem",
                                                  [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                                                      return std::make_unique<RenderSubmissionSystem>();
                                                  },
                                                  "Model Rendering", std::string(RuntimeFeatureIds::Rendering)}) &&
                  success;
        // 明示的な gameplay system 一覧があっても、描画コンポーネントは送信系を伴う。
        auto& policy = RuntimeFeatureContextAccess::Policy(context);
        for (const char* type : {"ModelComponent", "WorldSpriteComponent", "WorldTextComponent"}) {
            success = policy.Register({type, "ModelRenderSystem", 100}) && success;
        }
        return success;
    }

    std::span<const RuntimeSystemDefault> GetDefaultSystems() const noexcept override {
        static constexpr std::array<RuntimeSystemDefault, 1> defaults{{"ModelRenderSystem", true, 100}};
        return defaults;
    }
};
} // namespace

std::unique_ptr<IRuntimeFeature> CreateRenderingRuntimeFeature() { return std::make_unique<RenderingRuntimeFeature>(); }

} // namespace HIKARI
