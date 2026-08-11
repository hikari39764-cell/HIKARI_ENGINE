#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingWorkspaceController.h"

#include <algorithm>
#include <string>
#include <utility>

#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingEditorValue.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
    namespace {

        std::string DefaultVariableName(
            BINDING::ValueType type) {

            return "New " + std::string(BINDING::ToString(type)) +
                " Variable";
        }

    } // namespace

    bool RuntimeBindingWorkspaceController::CreateVariable(
        DocumentSceneBase& scene,
        BINDING::ValueType type,
        std::string_view displayName,
        std::string_view requestedId,
        std::string& outId,
        std::string& outMessage) {

        if (!EnsureDocumentOpen(scene, outMessage)) {
            return false;
        }
        const std::string id = requestedId.empty()
            ? AllocateStableId("variable")
            : std::string(requestedId);
        if (ContainsId(id)) {
            outMessage = "Runtime binding id already exists: " + id;
            return false;
        }

        BINDING::VariableDefinition definition{};
        definition.id = id;
        definition.name = displayName.empty()
            ? DefaultVariableName(type)
            : std::string(displayName);
        definition.defaultValue = BINDING::MakeDefaultValue(type);

        BINDING::Catalog next = document_.Catalog();
        next.variables.push_back(std::move(definition));
        if (!ApplyCatalog(scene, std::move(next), outMessage)) {
            return false;
        }
        outId = id;
        SelectItem(id);
        outMessage = "Runtime variable created";
        statusMessage_ = outMessage;
        return true;
    }

    bool RuntimeBindingWorkspaceController::AddEventDefinition(
        DocumentSceneBase& scene,
        bool hasPayload,
        BINDING::ValueType payloadType,
        std::string_view displayName,
        std::string_view requestedId,
        std::string& outId,
        std::string& outMessage) {

        if (!EnsureDocumentOpen(scene, outMessage)) {
            return false;
        }
        const std::string id = requestedId.empty()
            ? AllocateStableId("event")
            : std::string(requestedId);
        if (ContainsId(id)) {
            outMessage = "Runtime binding id already exists: " + id;
            return false;
        }

        BINDING::EventDefinition definition{};
        definition.id = id;
        definition.name = displayName.empty()
            ? "New Event"
            : std::string(displayName);
        definition.hasPayload = hasPayload;
        definition.payloadType = payloadType;

        BINDING::Catalog next = document_.Catalog();
        next.events.push_back(std::move(definition));
        if (!ApplyCatalog(scene, std::move(next), outMessage)) {
            return false;
        }
        outId = id;
        SelectItem(id);
        outMessage = "Runtime event created";
        statusMessage_ = outMessage;
        return true;
    }

    bool RuntimeBindingWorkspaceController::SetItemProperty(
        DocumentSceneBase& scene,
        std::string_view itemId,
        std::string_view propertyId,
        const nlohmann::json& value,
        std::string& outMessage,
        uint64_t mergeGroup) {

        if (!EnsureDocumentOpen(scene, outMessage)) {
            return false;
        }
        BINDING::Catalog next = document_.Catalog();
        if (BINDING::VariableDefinition* variable =
                next.FindVariable(itemId)) {
            if (propertyId == "name" || propertyId == "description") {
                if (!value.is_string()) {
                    outMessage =
                        "Runtime binding text property requires a string";
                    return false;
                }
                if (propertyId == "name") {
                    variable->name = value.get<std::string>();
                } else {
                    variable->description = value.get<std::string>();
                }
            } else if (propertyId == "type") {
                if (!value.is_string()) {
                    outMessage = "Variable type requires a string";
                    return false;
                }
                BINDING::ValueType type{};
                if (!TryParseRuntimeBindingEditorValueType(
                        value.get<std::string>(), type)) {
                    outMessage = "Unknown runtime binding value type";
                    return false;
                }
                if (variable->defaultValue.type != type) {
                    variable->defaultValue =
                        BINDING::MakeDefaultValue(type);
                    if (!BINDING::IsNumeric(type)) {
                        variable->hasRange = false;
                    }
                }
            } else if (propertyId == "default") {
                BINDING::Value parsed{};
                if (!TryReadRuntimeBindingEditorValue(
                        value,
                        variable->defaultValue.type,
                        parsed,
                        outMessage)) {
                    return false;
                }
                variable->defaultValue = parsed;
            } else if (propertyId == "hasRange") {
                if (!value.is_boolean()) {
                    outMessage = "hasRange requires a boolean";
                    return false;
                }
                variable->hasRange = value.get<bool>();
            } else if (propertyId == "range") {
                if (!value.is_object()) {
                    outMessage = "Variable range requires an object";
                    return false;
                }
                const auto enabled = value.find("enabled");
                const auto minimum = value.find("minimum");
                const auto maximum = value.find("maximum");
                const auto step = value.find("step");
                if (enabled == value.end() || !enabled->is_boolean() ||
                    minimum == value.end() || !minimum->is_number() ||
                    maximum == value.end() || !maximum->is_number() ||
                    step == value.end() || !step->is_number()) {
                    outMessage =
                        "Range requires enabled, minimum, maximum, and step";
                    return false;
                }
                variable->hasRange = enabled->get<bool>();
                variable->minimum = minimum->get<float>();
                variable->maximum = maximum->get<float>();
                variable->step = step->get<float>();
            } else if (propertyId == "minimum" ||
                       propertyId == "maximum" ||
                       propertyId == "step") {
                if (!value.is_number()) {
                    outMessage =
                        "Variable range properties require numbers";
                    return false;
                }
                const float number = value.get<float>();
                if (propertyId == "minimum") {
                    variable->minimum = number;
                } else if (propertyId == "maximum") {
                    variable->maximum = number;
                } else {
                    variable->step = number;
                }
            } else {
                outMessage = "Unknown variable property: " +
                    std::string(propertyId);
                return false;
            }
        } else if (BINDING::EventDefinition* event =
                next.FindEvent(itemId)) {
            if (propertyId == "name" || propertyId == "description") {
                if (!value.is_string()) {
                    outMessage =
                        "Runtime binding text property requires a string";
                    return false;
                }
                if (propertyId == "name") {
                    event->name = value.get<std::string>();
                } else {
                    event->description = value.get<std::string>();
                }
            } else if (propertyId == "hasPayload") {
                if (!value.is_boolean()) {
                    outMessage = "hasPayload requires a boolean";
                    return false;
                }
                event->hasPayload = value.get<bool>();
            } else if (propertyId == "payloadType") {
                if (!value.is_string()) {
                    outMessage = "Event payload type requires a string";
                    return false;
                }
                BINDING::ValueType type{};
                if (!TryParseRuntimeBindingEditorValueType(
                        value.get<std::string>(), type)) {
                    outMessage = "Unknown runtime binding value type";
                    return false;
                }
                event->payloadType = type;
            } else {
                outMessage = "Unknown event property: " +
                    std::string(propertyId);
                return false;
            }
        } else {
            outMessage = "Runtime binding item not found: " +
                std::string(itemId);
            return false;
        }

        if (!ApplyCatalog(
                scene,
                std::move(next),
                outMessage,
                mergeGroup)) {
            return false;
        }
        SelectItem(std::string(itemId));
        statusMessage_ = "Runtime binding property updated";
        outMessage = statusMessage_;
        return true;
    }

    RuntimeBindingRemovalResult
        RuntimeBindingWorkspaceController::RemoveItem(
            DocumentSceneBase& scene,
            std::string_view itemId,
            bool confirmed) {

        RuntimeBindingRemovalResult result{};
        if (!EnsureDocumentOpen(scene, result.message)) {
            return result;
        }
        if (!ContainsId(itemId)) {
            result.message = "Runtime binding item not found: " +
                std::string(itemId);
            return result;
        }

        result.references = GetReferences(itemId);
        result.referenceIndexAvailable =
            IsReferenceIndexAvailable();
        // Stable binding IDs may be referenced by scenes that are not loaded.
        // The current index is useful evidence, but never proof of project-wide
        // absence. Every destructive removal therefore requires an explicit
        // second request.
        if (!confirmed) {
            result.confirmationRequired = true;
            if (!result.referenceIndexAvailable) {
                result.message =
                    "Reference index is unavailable; deletion requires confirmation";
            } else if (!result.references.empty()) {
                result.message =
                    "Runtime binding item is referenced in the loaded authoring scope";
            } else {
                result.message =
                    "No reference was found in the loaded authoring scope; "
                    "project-wide deletion still requires confirmation";
            }
            return result;
        }

        BINDING::Catalog next = document_.Catalog();
        std::erase_if(
            next.variables,
            [itemId](const BINDING::VariableDefinition& definition) {
                return definition.id == itemId;
            });
        std::erase_if(
            next.events,
            [itemId](const BINDING::EventDefinition& definition) {
                return definition.id == itemId;
            });
        if (!ApplyCatalog(scene, std::move(next), result.message)) {
            return result;
        }

        previewVariableIds_.erase(std::string(itemId));
        if (selectedItemId_ == itemId) {
            selectedItemId_.clear();
            selectionKind_ = SelectionKind::None;
            NormalizeSelection();
        }
        InvalidateInspectorDraft();
        result.removed = true;
        result.message = "Runtime binding item removed";
        statusMessage_ = result.message;
        return result;
    }

} // namespace HIKARI::EDITOR
