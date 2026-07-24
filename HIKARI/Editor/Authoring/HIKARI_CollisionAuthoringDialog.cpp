#include "Editor/Authoring/HIKARI_CollisionAuthoringDialog.h"

#include <algorithm>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    void CollisionAuthoringDialog::Open(
        SceneObjectId selectedObject) {
        selectedObject_ = selectedObject;
        scope_ = selectedObject.value != 0u ? 0 : 1;
        shapeSource_ = 0;
        bodyMode_ = 0;
        existingColliderPolicy_ = 0;
        makeTrigger_ = false;
        openRequested_ = true;
    }

    bool CollisionAuthoringDialog::Draw(
        CollisionAuthoringRequest& outRequest) {
#if defined(HIKARI_WITH_EDITOR)
        if (openRequested_) {
            ImGui::OpenPopup("Collision Setup");
            openRequested_ = false;
        }

        bool applied = false;
        ImGui::SetNextWindowSize(
            ImVec2(500.0f, 0.0f),
            ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(
                "Collision Setup",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            static const char* scopes[]{
                "Selected Object",
                "Scene Geometry"
            };
            static const char* bodyModes[]{
                "Static",
                "Kinematic",
                "Dynamic"
            };
            static const char* shapeSources[]{
                "Fit Primitive to Geometry",
                "Model Default Collision"
            };
            static const char* existingPolicies[]{
                "Keep Existing Colliders",
                "Configure First Collider"
            };

            if (selectedObject_.value == 0u) {
                scope_ = 1;
                ImGui::BeginDisabled();
            }
            ImGui::Combo(
                "Target",
                &scope_,
                scopes,
                IM_ARRAYSIZE(scopes));
            if (selectedObject_.value == 0u) {
                ImGui::EndDisabled();
            }
            ImGui::Combo(
                "Shape",
                &shapeSource_,
                shapeSources,
                IM_ARRAYSIZE(shapeSources));
            ImGui::Combo(
                "Body",
                &bodyMode_,
                bodyModes,
                IM_ARRAYSIZE(bodyModes));
            ImGui::Combo(
                "Existing",
                &existingColliderPolicy_,
                existingPolicies,
                IM_ARRAYSIZE(existingPolicies));
            ImGui::Checkbox(
                "New / Fitted Collider Is Trigger",
                &makeTrigger_);

            ImGui::Spacing();
            if (ImGui::Button(
                    "Apply",
                    ImVec2(120.0f, 0.0f))) {
                outRequest = {};
                outRequest.scope = scope_ == 0
                    ? CollisionAuthoringScope::SelectedObject
                    : CollisionAuthoringScope::SceneGeometry;
                outRequest.selectedObject = selectedObject_;
                outRequest.bodyMode =
                    static_cast<CollisionBodyMode>(
                        std::clamp(bodyMode_, 0, 2));
                outRequest.shapeSource = shapeSource_ == 1
                    ? CollisionAuthoringShapeSource::ModelDefaultCollision
                    : CollisionAuthoringShapeSource::GeometryFit;
                outRequest.existingColliderPolicy =
                    existingColliderPolicy_ == 0
                        ? ExistingColliderPolicy::Keep
                        : ExistingColliderPolicy::ConfigureFirst;
                outRequest.makeTrigger = makeTrigger_;
                applied = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(
                    "Cancel",
                    ImVec2(120.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        return applied;
#else
        (void)outRequest;
        return false;
#endif
    }

} // namespace HIKARI::EDITOR
