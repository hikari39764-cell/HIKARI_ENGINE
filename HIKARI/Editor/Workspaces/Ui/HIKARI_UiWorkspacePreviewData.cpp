#include "Editor/Workspaces/Ui/HIKARI_UiWorkspaceController.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Widgets/HIKARI_TypedValueEditor.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Ui/HIKARI_UiBindingSourceComponent.h"
#include <set>

namespace HIKARI::EDITOR {
void UiWorkspaceController::DrawPreviewData(DocumentSceneBase& scene, EditorContext& context) {
    const auto L = [](const char* key) { return TEXT::Localize(key).c_str(); };
    const auto* live = scene.GetWorld().Services().Find<BINDING::RuntimeBindingStore>();
    auto* object = scene.GetWorld().FindObject(context.selection.GetActiveObjectId());
    projector_.SetPreviewBindings(nullptr);
    if (scene.IsRuntimePlayActive() || !live || !object)
        return;
    ImGui::Checkbox(L("uix.previewData"), &previewDataEnabled_);
    if (!previewDataEnabled_)
        return;
    RuntimeObjectHandle target;
    if (ResolveInheritedUiBindingSource(*object, target) == UiBindingSourceState::Invalid) {
        ImGui::TextWrapped("%s", L("uix.bindingSourceInvalid"));
        return;
    }
    if (ImGui::SmallButton(L("uix.resetPreviewData")))
        previewDataSource_ = 0;
    const bool reset = previewDataSource_ != live->GetInstanceId() || previewDataRevision_ != live->GetRevision() ||
                       previewDataObject_ != object->GetDocumentId() || previewDataTarget_ != target;
    if (reset) {
        if (!previewData_.Initialize(live->GetCatalog()))
            return;
        previewDataSource_ = live->GetInstanceId();
        previewDataRevision_ = live->GetRevision();
        previewDataObject_ = object->GetDocumentId();
        previewDataTarget_ = target;
        std::set<uint64_t> targets;
        for (const auto& candidate : scene.GetWorld().GetObjects()) {
            RuntimeObjectHandle source;
            if (candidate && ResolveInheritedUiBindingSource(*candidate, source) == UiBindingSourceState::Resolved)
                targets.insert(source.ToValue());
        }
        for (const auto& definition : live->GetCatalog().variables) {
            BINDING::Value value;
            if (live->ReadVariable(definition.id, value))
                previewData_.WriteVariable(definition.id, value, "UiPreview");
            for (const auto id : targets) {
                const auto source = RuntimeObjectHandle::FromValue(id);
                if (live->ReadObjectVariable(source, definition.id, value))
                    previewData_.WriteObjectVariable(source, definition.id, value, "UiPreview");
            }
        }
    }
    std::set<std::string> variables;
    for (const auto& source : scene.GetSceneDocument().objects)
        if (source.id == object->GetDocumentId())
            for (const auto& component : source.components) {
                if (component.type != "UiProgressComponent" && component.type != "UiDataBindingComponent")
                    continue;
                for (const auto* field : {"variableId", "textVariable", "visibleVariable", "interactableVariable"}) {
                    const auto id = component.properties.value(field, std::string{});
                    if (!id.empty())
                        variables.insert(id);
                }
            }
    ImGui::TextWrapped("%s", L(variables.empty() ? "uix.previewDataEmpty" : "uix.previewDataHelp"));
    for (const auto& id : variables) {
        const auto* definition = live->GetCatalog().FindVariable(id);
        if (!definition) {
            ImGui::Text("%s: %s", L("uix.previewDataMissing"), id.c_str());
            continue;
        }
        BINDING::Value value;
        const bool read = target.IsValid() ? previewData_.ReadObjectVariable(target, id, value)
                                           : previewData_.ReadVariable(id, value);
        if (!read)
            continue;
        ImGui::PushID(id.c_str());
        if (DrawTypedEditorValue(definition->name.c_str(), value,
                                 {definition->hasRange, definition->minimum, definition->maximum, definition->step},
                                 180)
                .changed) {
            const bool written = target.IsValid() ? previewData_.WriteObjectVariable(target, id, value, "UiPreview")
                                                  : previewData_.WriteVariable(id, value, "UiPreview");
            if (!written)
                status_ = L("uix.previewDataRejected");
        }
        ImGui::PopID();
    }
    projector_.SetPreviewBindings(&previewData_);
}
} // namespace HIKARI::EDITOR
