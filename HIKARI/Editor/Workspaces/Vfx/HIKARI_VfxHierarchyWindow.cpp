#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Vfx/HIKARI_VfxPropertySchema.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        bool ContainsInsensitive(
            std::string_view text,
            std::string_view query) {

            if (query.empty()) return true;
            return std::search(
                text.begin(), text.end(),
                query.begin(), query.end(),
                [](unsigned char left, unsigned char right) {
                    return std::tolower(left) == std::tolower(right);
                }) != text.end();
        }

        bool IsEmitterSelection(
            VfxWorkspaceSelectionKind kind) noexcept {

            return kind == VfxWorkspaceSelectionKind::Emitter ||
                kind == VfxWorkspaceSelectionKind::Bindings ||
                kind == VfxWorkspaceSelectionKind::Link ||
                kind == VfxWorkspaceSelectionKind::CurveKey ||
                kind == VfxWorkspaceSelectionKind::ColorStop;
        }

        std::string LinkLabel(
            const VFX::VfxEmitterData& emitter,
            const VFX::VfxInputLinkData& link,
            const VFX::VfxExposedInputData* input) {

            const VFX::VfxPropertySchema* schema =
                VFX::ResolveVfxPropertySchema(emitter, link.target);
            const std::string target = schema != nullptr
                ? std::string(VFX::ToString(schema->moduleKind)) + "." +
                    std::string(schema->propertyId)
                : link.target.moduleId + "." + link.target.propertyId;
            return (input != nullptr ? input->name : link.inputId) +
                "  ->  " + target;
        }
#endif
    } // namespace

    void VfxWorkspaceController::DrawHierarchyWindow(
        AssetDatabase& database) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Effect Hierarchy###Vfx/Hierarchy")) {
            ImGui::End();
            return;
        }

        ImGui::TextDisabled("VFX ASSETS");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint(
            "##VfxAssetSearch",
            "Filter assets",
            assetSearch_.data(),
            assetSearch_.size());
        if (ImGui::BeginChild(
                "VfxAssetCatalog", { 0.0f, 132.0f }, true)) {
            for (const AssetRecord* record :
                 database.CollectByType(AssetType::VfxEffect)) {
                if (record == nullptr ||
                    !ContainsInsensitive(
                        GetAssetRecordDisplayName(*record),
                        assetSearch_.data())) {
                    continue;
                }
                ImGui::PushID(record->guid.value.c_str());
                const bool selected =
                    document_.GetAssetGuid() == record->guid;
                if (ImGui::Selectable(
                        GetAssetRecordDisplayName(*record).c_str(),
                        selected,
                        ImGuiSelectableFlags_AllowDoubleClick) &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    (void)OpenAsset(
                        database, record->guid, statusMessage_);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "%s\nDouble-click to open",
                        record->guid.value.c_str());
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        ImGui::SeparatorText("Effect Stack");
        if (!document_.IsOpen()) {
            ImGui::TextDisabled("Create or open a VFX asset.");
            ImGui::End();
            return;
        }

        if (ImGui::Selectable(
                document_.Asset().name.c_str(),
                selection_.kind == VfxWorkspaceSelectionKind::Asset)) {
            Select({ VfxWorkspaceSelectionKind::Asset, "asset" });
        }

        const bool inputsOpen = ImGui::TreeNodeEx(
            "Exposed Inputs",
            ImGuiTreeNodeFlags_DefaultOpen |
                ImGuiTreeNodeFlags_SpanAvailWidth);
        if (ImGui::IsItemClicked()) {
            Select({ VfxWorkspaceSelectionKind::Inputs, "asset" });
        }
        if (inputsOpen) {
            for (const VFX::VfxExposedInputData& input :
                 document_.Asset().inputs) {
                ImGui::PushID(input.id.c_str());
                const std::string label = input.name + "  :  " +
                    std::string(VFX::ToString(input.type));
                if (ImGui::Selectable(
                        label.c_str(),
                        selection_.kind ==
                            VfxWorkspaceSelectionKind::Input &&
                        selection_.targetId == input.id)) {
                    Select({ VfxWorkspaceSelectionKind::Input, input.id });
                }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("+ Input")) {
                ImGui::OpenPopup("Add VFX Input");
            }
            if (ImGui::BeginPopup("Add VFX Input")) {
                for (const VFX::VfxValueType type : {
                        VFX::VfxValueType::Bool,
                        VFX::VfxValueType::Int,
                        VFX::VfxValueType::Float,
                        VFX::VfxValueType::Float2,
                        VFX::VfxValueType::Float3,
                        VFX::VfxValueType::Float4,
                        VFX::VfxValueType::Color,
                    }) {
                    if (ImGui::MenuItem(
                            std::string(VFX::ToString(type)).c_str())) {
                        std::string id{};
                        (void)AddInput(
                            type, {}, {}, id, statusMessage_);
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(
                selection_.kind != VfxWorkspaceSelectionKind::Input);
            if (ImGui::SmallButton("Delete Input")) {
                requestDeleteInput_ = true;
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }

        ImGui::Separator();
        DrawEmitterTree(document_.Asset(), {});
        ImGui::Spacing();
        if (ImGui::Button("+ Root Emitter")) {
            std::string id{};
            (void)AddEmitter({}, {}, {}, id, statusMessage_);
        }
        ImGui::SameLine();
        const bool hasEmitterSelection =
            IsEmitterSelection(selection_.kind) &&
            FindEmitter(selection_.emitterId) != nullptr;
        ImGui::BeginDisabled(!hasEmitterSelection);
        if (ImGui::Button("+ Child")) {
            std::string id{};
            (void)AddEmitter(
                selection_.emitterId, {}, {}, id, statusMessage_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) {
            std::string id{};
            (void)DuplicateEmitter(
                selection_.emitterId, id, statusMessage_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) requestDeleteEmitter_ = true;
        ImGui::EndDisabled();

        if (requestDeleteEmitter_) {
            ImGui::OpenPopup("Delete VFX Emitter?");
            requestDeleteEmitter_ = false;
        }
        if (ImGui::BeginPopupModal(
                "Delete VFX Emitter?", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(
                "Delete the selected emitter? Child emitters are promoted.");
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button("Delete Emitter")) {
                (void)RemoveEmitter(
                    selection_.emitterId, statusMessage_);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (requestDeleteInput_) {
            ImGui::OpenPopup("Delete VFX Input?");
            requestDeleteInput_ = false;
        }
        if (ImGui::BeginPopupModal(
                "Delete VFX Input?", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(
                "Delete the input and all property links that use it?");
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button("Delete Input")) {
                (void)RemoveInput(
                    selection_.targetId, statusMessage_);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::End();
#else
        (void)database;
#endif
    }

    void VfxWorkspaceController::DrawEmitterTree(
        const VFX::VfxAssetData& asset,
        std::string_view parentId) {
#if defined(HIKARI_WITH_EDITOR)
        std::vector<const VFX::VfxEmitterData*> children{};
        for (const VFX::VfxEmitterData& emitter : asset.emitters) {
            if (emitter.parentEmitterId == parentId) {
                children.push_back(&emitter);
            }
        }
        std::sort(
            children.begin(), children.end(),
            [](const auto* left, const auto* right) {
                return left->order != right->order
                    ? left->order < right->order
                    : left->name < right->name;
            });
        for (const VFX::VfxEmitterData* emitter : children) {
            ImGui::PushID(emitter->id.c_str());
            const bool emitterSelected =
                selection_.kind == VfxWorkspaceSelectionKind::Emitter &&
                selection_.emitterId == emitter->id;
            const bool open = ImGui::TreeNodeEx(
                "Emitter",
                ImGuiTreeNodeFlags_DefaultOpen |
                    ImGuiTreeNodeFlags_SpanAvailWidth |
                    (emitterSelected
                        ? ImGuiTreeNodeFlags_Selected
                        : ImGuiTreeNodeFlags_None),
                "%s%s",
                emitter->basic.enabled ? "" : "[Disabled] ",
                emitter->name.c_str());
            if (ImGui::IsItemClicked()) {
                Select({ VfxWorkspaceSelectionKind::Emitter,
                    emitter->id, emitter->id });
            }
            if (open) {
                if (!emitter->inputLinks.empty()) {
                    const std::string bindingLabel = "Bindings (" +
                        std::to_string(emitter->inputLinks.size()) + ")";
                    const bool bindingsOpen = ImGui::TreeNodeEx(
                        bindingLabel.c_str(),
                        ImGuiTreeNodeFlags_SpanAvailWidth);
                    if (ImGui::IsItemClicked()) {
                        Select({ VfxWorkspaceSelectionKind::Bindings,
                            emitter->id, emitter->id });
                    }
                    if (bindingsOpen) {
                        for (const VFX::VfxInputLinkData& link :
                             emitter->inputLinks) {
                            const std::string label = LinkLabel(
                                *emitter, link, FindInput(link.inputId));
                            if (ImGui::Selectable(
                                    label.c_str(),
                                    selection_.kind ==
                                        VfxWorkspaceSelectionKind::Link &&
                                    selection_.targetId == link.id)) {
                                Select({
                                    VfxWorkspaceSelectionKind::Link,
                                    link.id,
                                    emitter->id,
                                    link.target.moduleId,
                                    link.target.propertyId,
                                });
                            }
                        }
                        ImGui::TreePop();
                    }
                }
                DrawEmitterTree(asset, emitter->id);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
#else
        (void)asset;
        (void)parentId;
#endif
    }

} // namespace HIKARI::EDITOR
