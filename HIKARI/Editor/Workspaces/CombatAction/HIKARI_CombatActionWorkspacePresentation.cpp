#include "Editor/Workspaces/CombatAction/HIKARI_CombatActionWorkspaceController.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/HIKARI_TimeService.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Gameplay/Action/HIKARI_CombatActionEvaluator.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        constexpr ImVec4 kAccent{ 1.0f, 0.46f, 0.24f, 1.0f };

        std::string ShortGuid(const AssetGuid& guid) {
            return guid.value.size() <= 8u
                ? guid.value : guid.value.substr(0u, 8u);
        }

        const char* TrackName(CombatActionTrackKind track) noexcept {
            switch (track) {
            case CombatActionTrackKind::Animation: return "Animation";
            case CombatActionTrackKind::Phase: return "Action Phases";
            case CombatActionTrackKind::Hit: return "Hit Detection";
            case CombatActionTrackKind::Cancel: return "Cancel / Buffer";
            case CombatActionTrackKind::Motion: return "Motion / Warp";
            case CombatActionTrackKind::Defense: return "Defense";
            case CombatActionTrackKind::Vfx: return "VFX Cues";
            case CombatActionTrackKind::Presentation: return "Presentation";
            case CombatActionTrackKind::None:
            default: return "Action";
            }
        }

        bool MatchesAssetSearch(
            const AssetRecord& record,
            std::string_view search) {
            if (search.empty()) return true;
            const std::string needle = TEXT::ToLowerAsciiCopy(
                std::string(search));
            const std::string haystack = TEXT::ToLowerAsciiCopy(
                record.displayName + " " +
                record.sourcePath.generic_string() + " " +
                record.guid.value);
            return haystack.find(needle) != std::string::npos;
        }
#endif
    }

    void CombatActionWorkspaceController::DrawNavigator(
        DocumentSceneBase& scene,
        CombatActionWorkspaceResult& result) {
#if defined(HIKARI_WITH_EDITOR)
        AssetDatabase& database = scene.GetAssetDatabase();
        if (!ImGui::Begin("Action Library###CombatAction/Navigator")) {
            ImGui::End();
            return;
        }
        ImGui::TextColored(kAccent, "COMBAT ACTION");
        ImGui::TextDisabled("Animation, timing, motion and feedback authoring");
        ImGui::Spacing();
        if (ImGui::Button("Save", ImVec2(70.0f, 0.0f))) {
            (void)Save(scene, statusMessage_);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!document_.CanUndo());
        if (ImGui::Button("Undo")) (void)Undo(statusMessage_);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!document_.CanRedo());
        if (ImGui::Button("Redo")) (void)Redo(statusMessage_);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (document_.IsDirty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.28f, 1.0f), "Unsaved");
        } else {
            ImGui::TextDisabled("Saved");
        }
        if (ImGui::Button("New Action", ImVec2(-1.0f, 0.0f))) {
            (void)NewAsset(statusMessage_);
        }
        std::array<char, 192> search{};
        const size_t count = (std::min)(assetSearch_.size(), search.size() - 1u);
        if (count > 0u) std::memcpy(search.data(), assetSearch_.data(), count);
        if (ImGui::InputTextWithHint(
                "##ActionSearch", "Search actions...",
                search.data(), search.size())) {
            assetSearch_ = search.data();
        }
        ImGui::SeparatorText("ACTION ASSETS");
        std::vector<const AssetRecord*> records =
            database.CollectByType(AssetType::CombatAction);
        std::sort(
            records.begin(), records.end(),
            [](const AssetRecord* lhs, const AssetRecord* rhs) {
                if (lhs == nullptr || rhs == nullptr) return rhs != nullptr;
                return lhs->displayName < rhs->displayName;
            });
        if (ImGui::BeginChild(
                "##ActionAssets", ImVec2(0.0f, 190.0f),
                ImGuiChildFlags_Border)) {
            for (const AssetRecord* record : records) {
                if (record == nullptr ||
                    !MatchesAssetSearch(*record, assetSearch_)) continue;
                ImGui::PushID(record->guid.value.c_str());
                std::string label = record->displayName.empty()
                    ? record->sourcePath.stem().string()
                    : record->displayName;
                if (record->guid == document_.GetAssetGuid()) label += "  •";
                if (ImGui::Selectable(
                        label.c_str(),
                        record->guid == document_.GetAssetGuid(),
                        ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    (void)OpenAsset(scene, record->guid, statusMessage_);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "%s\nGUID %s\nDouble-click to open",
                        record->sourcePath.generic_string().c_str(),
                        ShortGuid(record->guid).c_str());
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::SeparatorText("TRACKS");
        const auto drawTrack = [this](CombatActionTrackKind track,
                                      int count) {
            ImGui::PushID(static_cast<int>(track));
            const bool selected = selection_.track == track;
            if (ImGui::Selectable(
                    TrackName(track), selected,
                    ImGuiSelectableFlags_SpanAvailWidth)) {
                selection_ = { track, 0u };
            }
            ImGui::SameLine(ImGui::GetWindowWidth() - 64.0f);
            ImGui::TextDisabled("%d", count);
            ImGui::PopID();
        };
        const auto& definition = document_.Definition();
        drawTrack(CombatActionTrackKind::Animation,
            static_cast<int>(definition.animationSegments.size()));
        drawTrack(CombatActionTrackKind::Phase,
            static_cast<int>(definition.phases.size()));
        drawTrack(CombatActionTrackKind::Hit,
            static_cast<int>(definition.hitWindows.size()));
        drawTrack(CombatActionTrackKind::Cancel,
            static_cast<int>(definition.cancelWindows.size()));
        drawTrack(CombatActionTrackKind::Motion,
            static_cast<int>(definition.motionWindows.size()));
        drawTrack(CombatActionTrackKind::Defense,
            static_cast<int>(definition.defenseWindows.size()));
        drawTrack(CombatActionTrackKind::Vfx,
            static_cast<int>(definition.presentation.vfxCues.size()));
        drawTrack(CombatActionTrackKind::Presentation, 1);

        ImGui::Spacing();
        ImGui::BeginDisabled(
            selection_.track == CombatActionTrackKind::None ||
            selection_.track == CombatActionTrackKind::Presentation);
        if (ImGui::Button("+ Add to Selected Track", ImVec2(-1.0f, 0.0f))) {
            AddItem(selection_.track);
        }
        ImGui::EndDisabled();
        const bool hasSequence =
            definition.presentation.sequenceAssetGuid.IsValid();
        ImGui::BeginDisabled(!hasSequence);
        if (ImGui::Button(
                "Open Presentation in Cinematics", ImVec2(-1.0f, 0.0f))) {
            result.openSequenceAssetGuid =
                definition.presentation.sequenceAssetGuid;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                hasSequence
                    ? "Open the linked camera and presentation sequence"
                    : "Assign a Camera Sequence in the Action Inspector first");
        }
        ImGui::Separator();
        if (ImGui::Button("Back to Scene", ImVec2(-1.0f, 0.0f))) {
            result.exitToSceneRequested = true;
        }
        ImGui::End();
#else
        (void)scene;
        (void)result;
#endif
    }

    void CombatActionWorkspaceController::DrawPreview(
        DocumentSceneBase& scene,
        EditorContext& context,
        SelectionSyncService& selectionSync,
        EditorWorkspaceHost& workspaceHost) {
#if defined(HIKARI_WITH_EDITOR)
        EditorViewInstance& view = workspaceHost.GetCombatActionPreviewView();
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse;
        if (!ImGui::Begin(
                "Action Preview###CombatAction/Preview", nullptr, flags)) {
            view.extent = {};
            view.interaction = {};
            RENDER3D::EDITORVIEW::ClearRequest(view.renderViewId);
            ImGui::End();
            return;
        }
        ImGui::BeginDisabled(playing_);
        if (ImGui::Button("Use Selected Actor")) {
            RestorePreviewPose(scene);
            previewTargetObjectId_ = context.selection.GetActiveObjectId();
            statusMessage_ = previewTargetObjectId_.value != 0u
                ? "Preview actor bound to the current scene selection"
                : "Select an actor with AnimatorComponent first";
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (previewTargetObjectId_.value != 0u) {
            const GameObject* object = scene.GetWorld().FindObject(
                previewTargetObjectId_);
            ImGui::TextDisabled(
                "Preview: %s",
                object != nullptr ? object->GetName().c_str() : "Missing actor");
        } else {
            ImGui::TextDisabled("Preview: select a scene actor, then bind it");
        }

        EditorTransformGizmoState noGizmo = context.transformGizmo;
        noGizmo.enabled = false;
        const DirectorViewPanelResult viewResult = directorViewPanel_.DrawContents(
            scene,
            view,
            context.selection.GetActiveObjectId(),
            noGizmo,
            TIME::GetFrameContext().unscaledDt,
            scene.IsRuntimePlayActive());
        if (viewResult.selectionRequested) {
            if (GameObject* object = selectionSync.FindRuntimeObjectByDocumentId(
                    scene, viewResult.selectedObjectId)) {
                context.selection.SelectObject(scene.GetWorld(), object);
            }
        }

        const ACTION::CombatActionEvaluation evaluation =
            ACTION::EvaluateCombatAction(
                document_.Definition(), currentTimeSeconds_);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetWindowPos();
        const ImVec2 max = ImVec2(
            min.x + ImGui::GetWindowSize().x,
            min.y + ImGui::GetWindowSize().y);
        const ImVec2 cardMin{ min.x + 14.0f, max.y - 70.0f };
        const ImVec2 cardMax{ min.x + 310.0f, max.y - 14.0f };
        drawList->AddRectFilled(
            cardMin, cardMax, IM_COL32(12, 15, 22, 214), 7.0f);
        drawList->AddRect(
            cardMin, cardMax, IM_COL32(255, 120, 62, 190), 7.0f);
        const std::string phase = std::string(ACTION::ToString(evaluation.phase));
        drawList->AddText(
            ImVec2(cardMin.x + 12.0f, cardMin.y + 8.0f),
            IM_COL32(255, 255, 255, 255),
            (document_.Definition().displayName + "  /  " + phase).c_str());
        const std::string status =
            std::to_string(evaluation.hitWindows.size()) + " hit window(s)  •  " +
            (evaluation.invulnerable ? "Invulnerable" : "Vulnerable");
        drawList->AddText(
            ImVec2(cardMin.x + 12.0f, cardMin.y + 31.0f),
            IM_COL32(176, 188, 205, 255), status.c_str());
        ImGui::End();
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
        (void)workspaceHost;
#endif
    }


} // namespace HIKARI::EDITOR
