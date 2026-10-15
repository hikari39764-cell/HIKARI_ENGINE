#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"

#include "imgui.h"

namespace HIKARI::EDITOR {
void VfxWorkspaceController::DrawInspectorWindow(AssetDatabase& database) {
    if (!ImGui::Begin((VfxUiText("Layer Settings") + "###Vfx/Inspector").c_str())) {
        ImGui::End();
        return;
    }
    if (!document_.IsOpen()) {
        ImGui::TextDisabled(VfxUiText("No VFX document is open.").c_str());
        ImGui::End();
        return;
    }

    if (selection_.kind == VfxWorkspaceSelectionKind::Asset) {
        const VFX::VfxAssetData snapshot = document_.Asset();
        ImGui::TextDisabled(VfxUiText("VFX ASSET").c_str());
        ImGui::TextWrapped("%s", document_.GetSourcePath().empty()
                                     ? "Unsaved asset"
                                     : document_.GetSourcePath().generic_string().c_str());
        VfxInspectorPropertyEditor editor(*this, database, "asset", statusMessage_);
        editor.Text("Name", "name", snapshot.name);
        editor.Float("Duration (seconds)", "durationSeconds", snapshot.durationSeconds, 0.01f, 0.01f, 3600.0f);
        editor.Bool("Loop in Runtime", "loop", snapshot.loop);
        ImGui::TextDisabled(VfxUiText("Preview loop and range are session-only Timeline settings.").c_str());
        ImGui::End();
        return;
    }

    if (selection_.kind == VfxWorkspaceSelectionKind::Inputs) {
        ImGui::TextDisabled(VfxUiText("Exposed Inputs").c_str());
        ImGui::TextWrapped(
            VfxUiText(
                "Inputs are named parameters supplied by components, systems or the runtime variable bridge. Bind them to emitter properties below.")
                .c_str());
        ImGui::Text(TEXT::Localize("editor.ui.zu.input.s.1f0629").c_str(), document_.Asset().inputs.size());
        ImGui::End();
        return;
    }

    if (selection_.kind == VfxWorkspaceSelectionKind::Input) {
        if (const VFX::VfxExposedInputData* input = FindInput(selection_.targetId)) {
            DrawInputInspector(database, *input);
        }
        ImGui::End();
        return;
    }

    const VFX::VfxEmitterData* emitter = FindEmitter(selection_.emitterId);
    if (emitter == nullptr) {
        ImGui::TextDisabled(VfxUiText("Select an emitter or exposed input.").c_str());
        ImGui::End();
        return;
    }
    if (selection_.kind == VfxWorkspaceSelectionKind::Link) {
        if (const VFX::VfxInputLinkData* link = FindInputLink(selection_.targetId)) {
            DrawLinkInspector(database, *emitter, *link);
        }
    } else if (selection_.kind == VfxWorkspaceSelectionKind::BurstKey) {
        if (const VFX::VfxBurstKeyData* key = FindBurstKey(selection_.targetId)) {
            const VFX::VfxBurstKeyData snapshot = *key;
            VfxInspectorPropertyEditor editor(*this, database, snapshot.id, statusMessage_);
            ImGui::TextDisabled(VfxUiText("BURST KEY").c_str());
            editor.Float("Time (local active)", "time", snapshot.time, 0.002f, 0.0f, 3600.0f);
            editor.Unsigned("Count", "count", snapshot.count, 1u, emitter->spawn.maximumParticles);
        }
    } else if (selection_.kind == VfxWorkspaceSelectionKind::CurveKey) {
        if (const VFX::VfxCurveKeyData* key = FindCurveKey(selection_.targetId)) {
            const VFX::VfxCurveKeyData snapshot = *key;
            VfxInspectorPropertyEditor editor(*this, database, snapshot.id, statusMessage_);
            ImGui::TextDisabled(TEXT::Localize("editor.ui.curve.key.s.s.s.ab0d37").c_str(), selection_.moduleId.c_str(),
                                selection_.propertyId.c_str(), selection_.channel.c_str());
            editor.Float("Time", "time", snapshot.time, 0.002f, 0.0f, 1.0f);
            editor.Float("Value", "value", snapshot.value);
            editor.Float("In Tangent", "inTangent", snapshot.inTangent);
            editor.Float("Out Tangent", "outTangent", snapshot.outTangent);
        }
    } else if (selection_.kind == VfxWorkspaceSelectionKind::ColorStop) {
        if (const VFX::VfxColorStopData* stop = FindColorStop(selection_.targetId)) {
            const VFX::VfxColorStopData snapshot = *stop;
            VfxInspectorPropertyEditor editor(*this, database, snapshot.id, statusMessage_);
            ImGui::TextDisabled(VfxUiText("GRADIENT STOP").c_str());
            editor.Float("Time", "time", snapshot.time, 0.002f, 0.0f, 1.0f);
            editor.Color("Color", "color", snapshot.color);
        }
    } else {
        DrawEmitterInspector(database, *emitter);
    }
    ImGui::End();
}

} // namespace HIKARI::EDITOR
