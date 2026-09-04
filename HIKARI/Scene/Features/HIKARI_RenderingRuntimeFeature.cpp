#include "Scene/Features/HIKARI_RenderingRuntimeFeature.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponentAuthoring.h"
#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpriteComponent.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/HIKARI_SceneDocument.h"
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
                std::string(RuntimeFeatureIds::Rendering)
            };
        }

        class RenderingRuntimeFeature final : public IRuntimeFeature {
        public:
            std::string_view GetFeatureId() const noexcept override {
                return RuntimeFeatureIds::Rendering;
            }

            std::string_view GetDisplayName() const noexcept override {
                return "Rendering Core";
            }

            std::string_view GetDescription() const noexcept override {
                return "Model, material, and procedural geometry rendering.";
            }

            std::span<const std::string_view>
                GetComponentTypeNames() const noexcept override {
                static constexpr std::array<std::string_view, 4> names{
                    "ModelComponent",
                    "MaterialFxComponent",
                    "ProceduralMeshComponent",
                    "WorldSpriteComponent"
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
                ComponentRegistry& components =
                    RuntimeFeatureContextAccess::Components(context);
                SystemTypeRegistry& systems =
                    RuntimeFeatureContextAccess::Systems(context);

                ComponentTypeInfo model{};
                model.typeName = "ModelComponent";
                model.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<ModelComponent>();
                };
                model.affectsRenderState = true;
                model.optionalComponents = {
                    "MaterialFxComponent",
                    "ProceduralMeshComponent"
                };
                model.presentation = RenderingPresentation(
                    "Model",
                    "Controls model assets, materials, visibility, and shadows.");
                model.authoringCommands =
                    BuildModelComponentAuthoringCommands();

                ComponentTypeInfo materialFx{};
                materialFx.typeName = "MaterialFxComponent";
                materialFx.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<MaterialFxComponent>();
                };
                materialFx.requiredComponents = { "ModelComponent" };
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
                    "Applies a Material FX profile and per-object overrides.");

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
                    "Generates editable primitive geometry.");

                ComponentTypeInfo worldSprite{};
                worldSprite.typeName = "WorldSpriteComponent";
                worldSprite.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<WorldSpriteComponent>();
                };
                worldSprite.affectsRenderState = true;
                worldSprite.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties = {
                        { "version", 1u },
                        { "textureAssetGuid", "" },
                        { "visible", true },
                        { "size", { 1.0f, 1.0f } },
                        { "preserveAspect", true },
                        { "facing", "CameraFacing" },
                        { "tint", { 1.0f, 1.0f, 1.0f } },
                        { "opacity", 1.0f },
                        { "pivot", { 0.5f, 0.5f } },
                        { "alphaMode", "Auto" },
                        { "alphaCutoff", 0.5f },
                        { "transparentSortPriority", 0 }
                    };
                };
                worldSprite.presentation = RenderingPresentation(
                    "World Sprite (3D)",
                    "Displays a texture directly on an engine-owned quad in "
                    "3D space, including transparent textures.");

                bool success = components.Register(std::move(model));
                success = components.Register(std::move(materialFx)) &&
                    success;
                success = components.Register(std::move(procedural)) &&
                    success;
                success = components.Register(std::move(worldSprite)) &&
                    success;
                success = systems.Register(SystemTypeInfo{
                    "ModelRenderSystem",
                    [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return std::make_unique<RenderSubmissionSystem>();
                    },
                    "Model Rendering",
                    std::string(RuntimeFeatureIds::Rendering)
                }) && success;
                return success;
            }

            std::span<const RuntimeSystemDefault>
                GetDefaultSystems() const noexcept override {
                static constexpr std::array<RuntimeSystemDefault, 1>
                    defaults{{ "ModelRenderSystem", true, 100 }};
                return defaults;
            }
        };
    } // namespace

    std::unique_ptr<IRuntimeFeature> CreateRenderingRuntimeFeature() {
        return std::make_unique<RenderingRuntimeFeature>();
    }

} // namespace HIKARI
