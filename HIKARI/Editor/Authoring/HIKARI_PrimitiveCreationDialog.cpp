#include "Editor/Authoring/HIKARI_PrimitiveCreationDialog.h"

#include <algorithm>
#include <cstring>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        void DrawPositiveFloat(const char* label, float& value) {
            ImGui::DragFloat(label, &value, 0.05f, 0.001f, 100000.0f, "%.3f");
            value = std::clamp(value, 0.001f, 100000.0f);
        }

        void DrawCount(
            const char* label,
            uint32_t& value,
            int minimum,
            int maximum) {
            int edited = static_cast<int>(value);
            if (ImGui::DragInt(label, &edited, 1.0f, minimum, maximum)) {
                value = static_cast<uint32_t>(std::clamp(edited, minimum, maximum));
            }
        }
#endif
    }

    void PrimitiveCreationDialog::Open(ProceduralMeshKind kind) {
        settings_ = ProceduralMeshSettings{};
        settings_.kind = kind;
        settings_.doubleSided =
            kind == ProceduralMeshKind::Plane ||
            kind == ProceduralMeshKind::GridPlane;
        settings_ = SanitizeProceduralMeshSettings(settings_);
        nameBuffer_.fill('\0');
        std::strncpy(
            nameBuffer_.data(),
            GetProceduralMeshDisplayName(kind),
            nameBuffer_.size() - 1u);
        addCollider_ = true;
        openRequested_ = true;
    }

    bool PrimitiveCreationDialog::Draw(
        CreatePrimitiveRequest& outRequest) {
#if defined(HIKARI_WITH_EDITOR)
        if (openRequested_) {
            ImGui::OpenPopup("Create Primitive");
            openRequested_ = false;
        }

        bool created = false;
        ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(
                "Create Primitive",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {

            static const char* shapeNames[] = {
                "Plane", "Grid Plane", "Box", "Sphere", "Cylinder", "Capsule"
            };
            ImGui::InputText("Name", nameBuffer_.data(), nameBuffer_.size());
            int kind = static_cast<int>(settings_.kind);
            if (ImGui::Combo("Shape", &kind, shapeNames, IM_ARRAYSIZE(shapeNames))) {
                settings_.kind = static_cast<ProceduralMeshKind>(
                    std::clamp(kind, 0, IM_ARRAYSIZE(shapeNames) - 1));
                settings_.doubleSided =
                    settings_.kind == ProceduralMeshKind::Plane ||
                    settings_.kind == ProceduralMeshKind::GridPlane;
                std::strncpy(
                    nameBuffer_.data(),
                    GetProceduralMeshDisplayName(settings_.kind),
                    nameBuffer_.size() - 1u);
            }

            ImGui::SeparatorText("Dimensions");
            switch (settings_.kind) {
            case ProceduralMeshKind::Plane:
            case ProceduralMeshKind::GridPlane:
                DrawPositiveFloat("Width", settings_.width);
                DrawPositiveFloat("Depth", settings_.depth);
                break;
            case ProceduralMeshKind::Box:
                DrawPositiveFloat("Width", settings_.width);
                DrawPositiveFloat("Height", settings_.height);
                DrawPositiveFloat("Depth", settings_.depth);
                break;
            case ProceduralMeshKind::Sphere:
                DrawPositiveFloat("Diameter", settings_.width);
                break;
            case ProceduralMeshKind::Cylinder:
            case ProceduralMeshKind::Capsule:
                DrawPositiveFloat("Diameter", settings_.width);
                DrawPositiveFloat("Height", settings_.height);
                break;
            }

            if (settings_.kind == ProceduralMeshKind::GridPlane) {
                ImGui::SeparatorText("Topology");
                DrawCount("Columns", settings_.segmentsX, 1, 512);
                DrawCount("Rows", settings_.segmentsY, 1, 512);
            } else if (settings_.kind == ProceduralMeshKind::Sphere) {
                ImGui::SeparatorText("Topology");
                DrawCount("Radial Segments", settings_.sphereSlices, 3, 512);
                DrawCount("Vertical Segments", settings_.sphereStacks, 2, 256);
            } else if (settings_.kind == ProceduralMeshKind::Cylinder) {
                ImGui::SeparatorText("Topology");
                DrawCount("Radial Segments", settings_.sphereSlices, 3, 512);
                DrawCount("Height Segments", settings_.segmentsY, 1, 512);
            } else if (settings_.kind == ProceduralMeshKind::Capsule) {
                ImGui::SeparatorText("Topology");
                DrawCount("Radial Segments", settings_.sphereSlices, 3, 512);
                DrawCount("Hemisphere Segments", settings_.sphereStacks, 2, 256);
            }

            ImGui::SeparatorText("Rendering and Physics");
            ImGui::Checkbox("Double Sided", &settings_.doubleSided);
            ImGui::Checkbox("Add Collider", &addCollider_);
            if (addCollider_ && settings_.kind == ProceduralMeshKind::Cylinder) {
                ImGui::TextDisabled("Cylinder uses the current capsule collider approximation.");
            }
            if (addCollider_ &&
                (settings_.kind == ProceduralMeshKind::Plane ||
                 settings_.kind == ProceduralMeshKind::GridPlane)) {
                ImGui::TextDisabled("Plane collider thickness: 0.02 units.");
            }
            ImGui::TextDisabled("Materials and Material FX remain editable on the Model component.");

            ImGui::Spacing();
            if (ImGui::Button("Create", ImVec2(120.0f, 0.0f))) {
                settings_ = SanitizeProceduralMeshSettings(settings_);
                outRequest = {};
                outRequest.object.name = nameBuffer_.data();
                outRequest.mesh = settings_;
                outRequest.addCollider = addCollider_;
                created = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        return created;
#else
        (void)outRequest;
        return false;
#endif
    }

} // namespace HIKARI::EDITOR
