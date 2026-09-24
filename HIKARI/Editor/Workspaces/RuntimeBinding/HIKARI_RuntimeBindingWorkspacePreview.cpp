#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingWorkspaceController.h"

#include <string>

#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingEditorValue.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

bool RuntimeBindingWorkspaceController::PreviewSetVariable(DocumentSceneBase& scene, std::string_view variableId,
                                                           const nlohmann::json& value, std::string& outMessage) {
    if (!EnsureDocumentOpen(scene, outMessage)) {
        return false;
    }
    const BINDING::VariableDefinition* definition = document_.Catalog().FindVariable(variableId);
    if (definition == nullptr) {
        outMessage = "Runtime variable not found: " + std::string(variableId);
        return false;
    }
    BINDING::Value parsed{};
    if (!TryReadRuntimeBindingEditorValue(value, definition->defaultValue.type, parsed, outMessage)) {
        return false;
    }
    BINDING::RuntimeBindingStore& store = scene.GetRuntimeBindingStore();
    if (!store.WriteVariable(variableId, parsed, kPreviewWriterId)) {
        const std::string writer(store.GetWriter(variableId));
        outMessage = writer.empty() ? "Runtime variable rejected the preview value"
                                    : "Runtime variable is owned by writer: " + writer;
        return false;
    }
    previewVariableIds_.insert(std::string(variableId));
    store.PublishSnapshot();
    outMessage = "Runtime variable preview updated";
    statusMessage_ = outMessage;
    return true;
}

bool RuntimeBindingWorkspaceController::PreviewEmitEvent(DocumentSceneBase& scene, std::string_view eventId,
                                                         const nlohmann::json* payload, std::string& outMessage) {
    if (!EnsureDocumentOpen(scene, outMessage)) {
        return false;
    }
    const BINDING::EventDefinition* definition = document_.Catalog().FindEvent(eventId);
    if (definition == nullptr) {
        outMessage = "Runtime event not found: " + std::string(eventId);
        return false;
    }
    if (definition->hasPayload != (payload != nullptr)) {
        outMessage = definition->hasPayload ? "This runtime event requires a payload"
                                            : "This runtime event does not accept a payload";
        return false;
    }

    BINDING::Value parsed{};
    const BINDING::Value* parsedPointer = nullptr;
    if (payload != nullptr) {
        if (!TryReadRuntimeBindingEditorValue(*payload, definition->payloadType, parsed, outMessage)) {
            return false;
        }
        parsedPointer = &parsed;
    }
    if (!scene.GetRuntimeBindingStore().Emit(eventId, {}, parsedPointer)) {
        outMessage = "Runtime event preview was rejected";
        return false;
    }
    outMessage = "Runtime event emitted";
    statusMessage_ = outMessage;
    return true;
}

bool RuntimeBindingWorkspaceController::PreviewReset(DocumentSceneBase& scene, std::string_view variableId,
                                                     std::string& outMessage) {
    if (!EnsureDocumentOpen(scene, outMessage)) {
        return false;
    }
    BINDING::RuntimeBindingStore& store = scene.GetRuntimeBindingStore();
    size_t releasedCount = 0u;
    if (!variableId.empty()) {
        if (!previewVariableIds_.contains(std::string(variableId))) {
            outMessage = "Variable has no editor preview override";
            return false;
        }
        if (store.ResetVariableToDefault(variableId, kPreviewWriterId)) {
            ++releasedCount;
        }
        previewVariableIds_.erase(std::string(variableId));
    } else {
        for (const std::string& id : previewVariableIds_) {
            if (store.ResetVariableToDefault(id, kPreviewWriterId)) {
                ++releasedCount;
            }
        }
        previewVariableIds_.clear();
    }
    store.PublishSnapshot();
    outMessage = "Reset " + std::to_string(releasedCount) + " runtime binding preview override(s) to authored defaults";
    statusMessage_ = outMessage;
    return true;
}

} // namespace HIKARI::EDITOR
