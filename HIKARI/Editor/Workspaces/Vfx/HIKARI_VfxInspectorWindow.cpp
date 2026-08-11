#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    void VfxWorkspaceController::DrawInspectorWindow(
        AssetDatabase& database) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Properties###Vfx/Inspector")) {
            ImGui::End();
            return;
        }
        if (!document_.IsOpen()) {
            ImGui::TextDisabled("No VFX document is open.");
            ImGui::End();
            return;
        }

        if (selection_.kind == VfxWorkspaceSelectionKind::Asset) {
            const VFX::VfxAssetData snapshot = document_.Asset();
            ImGui::TextDisabled("VFX ASSET");
            ImGui::TextWrapped(
                "%s",
                document_.GetSourcePath().empty()
                    ? "Unsaved asset"
                    : document_.GetSourcePath().generic_string().c_str());
            VfxInspectorPropertyEditor editor(
                *this, database, "asset", statusMessage_);
            editor.Text("Name", "name", snapshot.name);
            editor.Float(
                "Duration (seconds)",
                "durationSeconds",
                snapshot.durationSeconds,
                0.01f,
                0.01f,
                3600.0f);
            editor.Bool("Loop in Runtime", "loop", snapshot.loop);
            ImGui::TextDisabled(
                "Preview loop and range are session-only Timeline settings.");
            ImGui::End();
            return;
        }

        if (selection_.kind == VfxWorkspaceSelectionKind::Inputs) {
            ImGui::TextDisabled("EXPOSED INPUTS");
            ImGui::TextWrapped(
                "Inputs are named parameters supplied by components, systems or the runtime variable bridge. Bind them to emitter properties below.");
            ImGui::Text("%zu input(s)", document_.Asset().inputs.size());
            ImGui::End();
            return;
        }

        if (selection_.kind == VfxWorkspaceSelectionKind::Input) {
            if (const VFX::VfxExposedInputData* input =
                    FindInput(selection_.targetId)) {
                DrawInputInspector(database, *input);
            }
            ImGui::End();
            return;
        }

        const VFX::VfxEmitterData* emitter =
            FindEmitter(selection_.emitterId);
        if (emitter == nullptr) {
            ImGui::TextDisabled("Select an emitter or exposed input.");
            ImGui::End();
            return;
        }
        if (selection_.kind == VfxWorkspaceSelectionKind::Link) {
            if (const VFX::VfxInputLinkData* link =
                    FindInputLink(selection_.targetId)) {
                DrawLinkInspector(database, *emitter, *link);
            }
        } else if (selection_.kind ==
                   VfxWorkspaceSelectionKind::CurveKey) {
            if (const VFX::VfxCurveKeyData* key =
                    FindCurveKey(selection_.targetId)) {
                const VFX::VfxCurveKeyData snapshot = *key;
                VfxInspectorPropertyEditor editor(
                    *this, database, snapshot.id, statusMessage_);
                ImGui::TextDisabled(
                    "CURVE KEY  %s.%s.%s",
                    selection_.moduleId.c_str(),
                    selection_.propertyId.c_str(),
                    selection_.channel.c_str());
                editor.Float("Time", "time", snapshot.time, 0.002f, 0.0f, 1.0f);
                editor.Float("Value", "value", snapshot.value);
                editor.Float("In Tangent", "inTangent", snapshot.inTangent);
                editor.Float("Out Tangent", "outTangent", snapshot.outTangent);
            }
        } else if (selection_.kind ==
                   VfxWorkspaceSelectionKind::ColorStop) {
            if (const VFX::VfxColorStopData* stop =
                    FindColorStop(selection_.targetId)) {
                const VFX::VfxColorStopData snapshot = *stop;
                VfxInspectorPropertyEditor editor(
                    *this, database, snapshot.id, statusMessage_);
                ImGui::TextDisabled("GRADIENT STOP");
                editor.Float("Time", "time", snapshot.time, 0.002f, 0.0f, 1.0f);
                editor.Color("Color", "color", snapshot.color);
            }
        } else {
            DrawEmitterInspector(database, *emitter);
        }
        ImGui::End();
#else
        (void)database;
#endif
    }

} // namespace HIKARI::EDITOR
