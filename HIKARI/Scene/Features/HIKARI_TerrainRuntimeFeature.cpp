#include "Scene/Features/HIKARI_TerrainRuntimeFeature.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

namespace HIKARI {

    namespace {
        class TerrainRuntimeFeature final : public IRuntimeFeature {
        public:
            std::string_view GetFeatureId() const noexcept override {
                return RuntimeFeatureIds::Terrain;
            }

            std::string_view GetDisplayName() const noexcept override {
                return "Terrain";
            }

            std::string_view GetDescription() const noexcept override {
                return "Editable solid terrain and layered surface data.";
            }

            std::span<const std::string_view>
                GetRequiredFeatureIds() const noexcept override {
                static constexpr std::array<std::string_view, 1> ids{
                    RuntimeFeatureIds::Rendering
                };
                return ids;
            }

            std::span<const std::string_view>
                GetComponentTypeNames() const noexcept override {
                static constexpr std::array<std::string_view, 1> names{
                    "TerrainSurfaceComponent"
                };
                return names;
            }

            bool Register(RuntimeFeatureContext& context) override {
                ComponentRegistry& components =
                    RuntimeFeatureContextAccess::Components(context);

                ComponentTypeInfo terrain{};
                terrain.typeName = "TerrainSurfaceComponent";
                terrain.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<TerrainSurfaceComponent>();
                };
                terrain.requiredComponents = {
                    "ModelComponent",
                    "ProceduralMeshComponent"
                };
                terrain.runtimeApplyPolicy =
                    ComponentRuntimeApplyPolicy::OnEditCommit;
                terrain.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    TerrainSurfaceComponent defaults{};
                    defaults.Serialize(properties);
                };
                terrain.presentation = ComponentTypePresentation{
                    "Terrain Surface",
                    "Terrain",
                    "Editable heightfield terrain with paintable layers.",
                    std::string(RuntimeFeatureIds::Terrain)
                };
                terrain.authoringCommands = {
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
                        "Sculpt one brush stroke in terrain-object local space.",
                        {
                            { "localX", AuthoringCommandParameterType::Float, true, "Brush center local X." },
                            { "localY", AuthoringCommandParameterType::Float, false, "Brush center local Y." },
                            { "localZ", AuthoringCommandParameterType::Float, true, "Brush center local Z." },
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
                            component->SculptAtLocal({
                                args.value("localX", 0.0f),
                                args.value("localY", 0.0f),
                                args.value("localZ", 0.0f)
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
                        "Remove every authored terrain chunk.",
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
                        "Paint one preset layer stroke in terrain-object local space.",
                        {
                            { "presetId", AuthoringCommandParameterType::String, true, "Preset id: sand, snow, or dirt." },
                            { "localX", AuthoringCommandParameterType::Float, true, "Brush center local X." },
                            { "localY", AuthoringCommandParameterType::Float, false, "Brush center local Y." },
                            { "localZ", AuthoringCommandParameterType::Float, true, "Brush center local Z." },
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
                            component->PaintLayerAtLocal(
                                args.value(
                                    "presetId",
                                    std::string("sand")),
                                {
                                    args.value("localX", 0.0f),
                                    args.value("localY", 0.0f),
                                    args.value("localZ", 0.0f)
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
                        "Paint or erase coverage in terrain-object local space.",
                        {
                            { "target", AuthoringCommandParameterType::Float, true, "Target coverage 1=paint, 0=erase." },
                            { "localX", AuthoringCommandParameterType::Float, true, "Brush center local X." },
                            { "localY", AuthoringCommandParameterType::Float, false, "Brush center local Y." },
                            { "localZ", AuthoringCommandParameterType::Float, true, "Brush center local Z." },
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
                            component->PaintCoverageAtLocal(
                                args.value("target", 1.0f),
                                {
                                    args.value("localX", 0.0f),
                                    args.value("localY", 0.0f),
                                    args.value("localZ", 0.0f)
                                });
                            return AuthoringCommandResult{
                                true,
                                "OK",
                                "Terrain coverage painted."
                            };
                        }
                    }
                };
                return components.Register(std::move(terrain));
            }
        };
    } // namespace

    std::unique_ptr<IRuntimeFeature> CreateTerrainRuntimeFeature() {
        return std::make_unique<TerrainRuntimeFeature>();
    }

} // namespace HIKARI
