#include "Scene/Components/Rendering/Model/HIKARI_ModelComponentAuthoring.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Models/HIKARI_ModelMaterialSlot.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

namespace HIKARI {

    namespace {

        AuthoringCommandResult ModelCommandFailure(
            std::string code,
            std::string message) {

            AuthoringCommandResult result{};
            result.code = std::move(code);
            result.message = std::move(message);
            return result;
        }

        ModelComponent* ResolveModelComponent(IComponent& component) {
            return dynamic_cast<ModelComponent*>(&component);
        }

        AuthoringCommandParameter Parameter(
            std::string name,
            AuthoringCommandParameterType type,
            bool required,
            std::string description) {

            return AuthoringCommandParameter{
                std::move(name),
                type,
                required,
                std::move(description)
            };
        }

    } // namespace

    std::vector<ComponentAuthoringCommandDefinition>
    BuildModelComponentAuthoringCommands() {

        using ParameterType = AuthoringCommandParameterType;
        std::vector<ComponentAuthoringCommandDefinition> commands{};

        commands.push_back({
            "model.material_override.bind",
            "Bind a material asset to a stable model material slot.",
            {
                Parameter(
                    "slotId",
                    ParameterType::String,
                    true,
                    "Stable MaterialSlotId returned by the model slot query."),
                Parameter(
                    "materialGuid",
                    ParameterType::String,
                    true,
                    "Material asset GUID."),
                Parameter(
                    "legacySlotIndex",
                    ParameterType::Int,
                    false,
                    "Compatibility slot index used only as fallback metadata.")
            },
            [](IComponent& component,
               const nlohmann::json& arguments) {
                ModelComponent* model = ResolveModelComponent(component);
                if (model == nullptr) {
                    return ModelCommandFailure(
                        "COMPONENT_TYPE_MISMATCH",
                        "The command requires ModelComponent.");
                }
                const std::optional<MaterialSlotId> slotId =
                    ASSETS::MODELS::ParseMaterialSlotId(
                        arguments.value("slotId", std::string{}));
                if (!slotId.has_value()) {
                    return ModelCommandFailure(
                        "INVALID_MATERIAL_SLOT_ID",
                        "slotId is not a valid MaterialSlotId.");
                }
                const std::string materialGuid = arguments.value(
                    "materialGuid",
                    std::string{});
                if (!IsValidAssetGuid(materialGuid)) {
                    return ModelCommandFailure(
                        "INVALID_ASSET_GUID",
                        "materialGuid is not a valid asset GUID.");
                }
                uint64_t legacyIndex = 0u;
                if (const auto found =
                        arguments.find("legacySlotIndex");
                    found != arguments.end()) {
                    if (found->is_number_unsigned()) {
                        legacyIndex = found->get<uint64_t>();
                    } else {
                        const int64_t signedIndex = found->get<int64_t>();
                        if (signedIndex < 0) {
                            return ModelCommandFailure(
                                "VALUE_OUT_OF_RANGE",
                                "legacySlotIndex must not be negative.");
                        }
                        legacyIndex = static_cast<uint64_t>(signedIndex);
                    }
                    if (legacyIndex >
                        (std::numeric_limits<uint32_t>::max)()) {
                        return ModelCommandFailure(
                            "VALUE_OUT_OF_RANGE",
                            "legacySlotIndex is outside the uint32 range.");
                    }
                }
                model->SetMaterialOverride(
                    *slotId,
                    static_cast<uint32_t>(legacyIndex),
                    AssetGuid{ materialGuid });
                AuthoringCommandResult result{};
                result.ok = true;
                result.message = "Model material override bound.";
                result.data = {
                    { "slotId", arguments["slotId"] },
                    { "materialGuid", materialGuid }
                };
                return result;
            }
        });

        commands.push_back({
            "model.material_override.clear",
            "Clear the authored material override for a stable model slot.",
            {
                Parameter(
                    "slotId",
                    ParameterType::String,
                    true,
                    "Stable MaterialSlotId to restore to its inherited material.")
            },
            [](IComponent& component,
               const nlohmann::json& arguments) {
                ModelComponent* model = ResolveModelComponent(component);
                if (model == nullptr) {
                    return ModelCommandFailure(
                        "COMPONENT_TYPE_MISMATCH",
                        "The command requires ModelComponent.");
                }
                const std::optional<MaterialSlotId> slotId =
                    ASSETS::MODELS::ParseMaterialSlotId(
                        arguments.value("slotId", std::string{}));
                if (!slotId.has_value()) {
                    return ModelCommandFailure(
                        "INVALID_MATERIAL_SLOT_ID",
                        "slotId is not a valid MaterialSlotId.");
                }
                model->ClearMaterialOverride(*slotId);
                AuthoringCommandResult result{};
                result.ok = true;
                result.message = "Model material override cleared.";
                result.data = {
                    { "slotId", arguments["slotId"] }
                };
                return result;
            }
        });

        commands.push_back({
            "model.material_set.select",
            "Select a compatible Model Material Set asset.",
            {
                Parameter(
                    "materialSetGuid",
                    ParameterType::String,
                    true,
                    "ModelMaterialBinding asset GUID.")
            },
            [](IComponent& component,
               const nlohmann::json& arguments) {
                ModelComponent* model = ResolveModelComponent(component);
                if (model == nullptr) {
                    return ModelCommandFailure(
                        "COMPONENT_TYPE_MISMATCH",
                        "The command requires ModelComponent.");
                }
                const std::string guid = arguments.value(
                    "materialSetGuid",
                    std::string{});
                if (!IsValidAssetGuid(guid)) {
                    return ModelCommandFailure(
                        "INVALID_ASSET_GUID",
                        "materialSetGuid is not a valid asset GUID.");
                }
                model->SetMaterialSetAssetGuid(AssetGuid{ guid });
                AuthoringCommandResult result{};
                result.ok = true;
                result.message = "Model Material Set selected.";
                result.data = { { "materialSetGuid", guid } };
                return result;
            }
        });

        commands.push_back({
            "model.materials.use_imported",
            "Restore all model slots to imported model materials.",
            {},
            [](IComponent& component, const nlohmann::json&) {
                ModelComponent* model = ResolveModelComponent(component);
                if (model == nullptr) {
                    return ModelCommandFailure(
                        "COMPONENT_TYPE_MISMATCH",
                        "The command requires ModelComponent.");
                }
                model->UseImportedModelMaterials();
                AuthoringCommandResult result{};
                result.ok = true;
                result.message = "Imported model materials restored.";
                return result;
            }
        });

        return commands;
    }

} // namespace HIKARI
