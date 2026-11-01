#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxParticleMaterialField.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Assets/Vfx/HIKARI_VfxBuiltinTextureCatalog.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/Widgets/HIKARI_EditorTexturePreview.h"
#include "Render2D/HIKARI_DxTexture.h"

#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {

std::vector<VfxParticleMaterialChoice> CollectParticleMaterials(AssetDatabase& database) {
    std::vector<VfxParticleMaterialChoice> choices{};
    ASSETS::MATERIAL::MaterialAssetAuthoringService service(database);
    for (const AssetRecord* record : database.CollectByType(AssetType::Material)) {
        if (record == nullptr || !record->guid.IsValid())
            continue;
        ASSETS::MATERIAL::MaterialAuthoringDocument material{};
        std::string message{};
        if (!service.Load(record->guid, material, message) || !IsRenderableMaterialAssetKind(material.source.kind)) {
            continue;
        }
        choices.push_back({
            record->guid,
            GetAssetRecordDisplayName(*record) + " [" + std::string(ToString(material.source.domain)) + "]",
        });
    }
    return choices;
}

bool IsVfxRenderableMaterial(AssetDatabase& database, const AssetGuid& guid, std::string& outMessage) {
    ASSETS::MATERIAL::MaterialAssetAuthoringService service(database);
    ASSETS::MATERIAL::MaterialAuthoringDocument material{};
    if (!service.Load(guid, material, outMessage)) {
        return false;
    }
    if (!IsRenderableMaterialAssetKind(material.source.kind)) {
        outMessage = "KIND_MISMATCH: VFX renderers require a renderable Material asset";
        return false;
    }
    return true;
}

std::vector<VfxParticleTextureChoice> CollectParticleTextures(AssetDatabase& database) {
    std::vector<VfxParticleTextureChoice> choices{};
    for (const VFX::VfxBuiltinTextureDescriptor& descriptor : VFX::GetVfxBuiltinTextureCatalog()) {
        choices.push_back({
            AssetGuid{std::string(descriptor.assetId)},
            std::string(descriptor.displayName),
            true,
        });
    }
    for (const AssetRecord* record : database.CollectByType(AssetType::Texture)) {
        if (record == nullptr || !record->guid.IsValid())
            continue;
        choices.push_back({
            record->guid,
            GetAssetRecordDisplayName(*record),
            false,
        });
    }
    return choices;
}

} // namespace

bool DrawVfxParticleMaterialField(AssetDatabase& assetDatabase, VfxParticleMaterialCatalog& catalog,
                                  const AssetGuid& current, AssetGuid& outSelected, AssetGuid& outCreatedAssetGuid,
                                  AssetGuid& outEditAssetGuid, std::string& outMessage) {
    outCreatedAssetGuid = {};
    outEditAssetGuid = {};
    const uint64_t contentRevision = assetDatabase.GetContentRevision();
    if (catalog.contentRevision != contentRevision) {
        catalog.choices = CollectParticleMaterials(assetDatabase);
        catalog.contentRevision = contentRevision;
    }
    const VfxParticleMaterialChoice* selected = nullptr;
    for (const VfxParticleMaterialChoice& choice : catalog.choices) {
        if (choice.guid == current) {
            selected = &choice;
            break;
        }
    }

    bool changed = false;
    const char* preview = !current.IsValid()    ? "Built-in VFX Unlit"
                          : selected != nullptr ? selected->label.c_str()
                                                : "Missing or non-renderable material";
    ImGui::BeginGroup();
    if (ImGui::BeginCombo(TEXT::LocalizeWindow("ui.inspector.model.material", "Material").c_str(), preview)) {
        if (ImGui::Selectable(TEXT::LocalizeWindow("editor.ui.built.in.vfx.unlit.d589ac", "Built-in VFX Unlit").c_str(),
                              !current.IsValid())) {
            outSelected = {};
            changed = true;
        }
        for (const VfxParticleMaterialChoice& choice : catalog.choices) {
            ImGui::PushID(choice.guid.value.c_str());
            if (ImGui::Selectable(choice.label.c_str(), choice.guid == current)) {
                outSelected = choice.guid;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", choice.guid.value.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::EndGroup();
    DroppedAssetPayload dropped{};
    if (AcceptAssetDropOfType(assetDatabase, AssetType::Material, dropped) && dropped.record != nullptr) {
        std::string validationMessage{};
        if (IsVfxRenderableMaterial(assetDatabase, dropped.guid, validationMessage)) {
            outSelected = dropped.guid;
            outMessage = "VFX Material assigned by drag and drop";
            changed = true;
        } else {
            outMessage =
                validationMessage.empty() ? "Dropped Material could not be loaded" : std::move(validationMessage);
        }
    }

    if (current.IsValid() && selected == nullptr) {
        ImGui::TextColored(
            {1.0f, 0.48f, 0.30f, 1.0f},
            TEXT::Localize("editor.ui.the.assigned.asset.is.missing.or.is.not.a.renderable.material.9f739d").c_str());
    } else if (catalog.choices.empty()) {
        ImGui::TextDisabled(VfxUiText("No Material exists yet. The built-in VFX fallback remains usable.").c_str());
    }
    if (ImGui::Button(VfxUiText("Create VFX Material").c_str())) {
        ASSETS::MATERIAL::MaterialAssetAuthoringService service(assetDatabase);
        const ASSETS::MATERIAL::MaterialAuthoringResult created =
            service.Create(MaterialTemplateId::ParticleUnlit, "New VFX Material");
        outMessage = created.message;
        if (created.success && created.assetGuid.IsValid()) {
            outSelected = created.assetGuid;
            outCreatedAssetGuid = created.assetGuid;
            catalog.contentRevision = 0xffffffffffffffffull;
            changed = true;
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!current.IsValid() || selected == nullptr);
    if (ImGui::Button(VfxUiText("Edit Material").c_str())) {
        outEditAssetGuid = current;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(
        VfxUiText("Surface and Particle materials share the main runtime; particle geometry supplies UV and Tint.")
            .c_str());
    return changed;
}

bool DrawVfxParticleTextureField(AssetDatabase& assetDatabase, const AssetRegistry* assetRegistry,
                                 VfxParticleTextureCatalog& catalog, const AssetGuid& current, AssetGuid& outSelected,
                                 std::string& outMessage) {
    // 貼図の見た目を選ぶ時点で確認できるよう、一覧と選択中をサムネイル表示する。
    const auto resolvePreviewHandle = [assetRegistry](const AssetGuid& guid) {
        if (assetRegistry == nullptr || !guid.IsValid()) {
            return -1;
        }
        const auto* descriptor = assetRegistry->FindAs<TextureAssetDescriptor>(guid);
        if (descriptor == nullptr) {
            return -1;
        }
        return ResolveEditorTexturePreviewHandle(guid, *descriptor);
    };
    const auto resolvePreviewTextureId = [&resolvePreviewHandle](const AssetGuid& guid) {
        const int handle = resolvePreviewHandle(guid);
        if (handle < 0) {
            return static_cast<ImTextureID>(0);
        }
        const D3D12_GPU_DESCRIPTOR_HANDLE srv = DXTEX::DxTextureManager::GetSrvGpuHandle(handle);
        if (srv.ptr == 0) {
            return static_cast<ImTextureID>(0);
        }
        return reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr));
    };
    const auto drawThumbnail = [&resolvePreviewTextureId](const AssetGuid& guid, float size) {
        const ImTextureID textureId = resolvePreviewTextureId(guid);
        if (textureId == 0) {
            return false;
        }
        ImGui::Image(textureId, ImVec2(size, size));
        return true;
    };
    const uint64_t contentRevision = assetDatabase.GetContentRevision();
    if (catalog.contentRevision != contentRevision) {
        catalog.choices = CollectParticleTextures(assetDatabase);
        catalog.contentRevision = contentRevision;
    }

    const VfxParticleTextureChoice* selected = nullptr;
    for (const VfxParticleTextureChoice& choice : catalog.choices) {
        if (choice.guid == current) {
            selected = &choice;
            break;
        }
    }
    const std::string preview = !current.IsValid()    ? VfxUiText("None (white particle)")
                                : selected != nullptr ? selected->label
                                                      : VfxUiText("Missing texture");
    bool changed = false;
    ImGui::BeginGroup();
    if (current.IsValid() && drawThumbnail(current, 48.0f)) {
        ImGui::SameLine();
    }
    // 一覧はポップアップ内のサムネイル格子で選ぶ。貼図が増えても
    // 見た目で選べるよう、リストではなく格子で表示する。
    if (ImGui::Button(preview.c_str(), ImVec2(220.0f, 0.0f))) {
        ImGui::OpenPopup("VfxParticleTexturePicker");
    }
    if (ImGui::BeginPopup("VfxParticleTexturePicker")) {
        constexpr int kGridColumns = 6;
        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputTextWithHint("##vfx_texture_search", VfxUiText("Search").c_str(), catalog.search.data(),
                                 catalog.search.size());
        std::string filter = catalog.search.data();
        std::transform(filter.begin(), filter.end(), filter.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        const auto matchesFilter = [&filter](const VfxParticleTextureChoice& choice) {
            if (filter.empty()) {
                return true;
            }
            std::string haystack = choice.label + " " + choice.guid.value;
            std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            return haystack.find(filter) != std::string::npos;
        };
        if (ImGui::BeginChild("VfxParticleTextureGrid", ImVec2(0.0f, 420.0f), ImGuiChildFlags_Border)) {
            int column = 0;
            const auto beginCell = [&column]() {
                if (column != 0) {
                    ImGui::SameLine();
                }
            };
            const auto endCell = [&column]() { column = (column + 1) % kGridColumns; };
            const auto drawCell = [&](const AssetGuid& guid, const std::string& label) {
                ImGui::PushID(guid.IsValid() ? guid.value.c_str() : "none");
                ImGui::BeginGroup();
                bool clicked = false;
                const ImTextureID textureId =
                    guid.IsValid() ? resolvePreviewTextureId(guid) : static_cast<ImTextureID>(0);
                if (textureId != 0) {
                    ImGui::PushStyleColor(ImGuiCol_Button, guid == current ? ImVec4(0.20f, 0.48f, 0.90f, 1.0f)
                                                                           : ImVec4(0.10f, 0.12f, 0.15f, 1.0f));
                    clicked = ImGui::ImageButton("cell", textureId, ImVec2(72.0f, 72.0f));
                    ImGui::PopStyleColor();
                } else {
                    clicked = ImGui::Button(label.c_str(), ImVec2(72.0f, 72.0f));
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n%s", label.c_str(), guid.IsValid() ? guid.value.c_str() : "");
                }
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 72.0f);
                ImGui::TextWrapped("%s", label.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                ImGui::PopID();
                return clicked;
            };
            beginCell();
            if (drawCell({}, VfxUiText("None (white particle)"))) {
                outSelected = {};
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            endCell();
            bool separatorDrawn = false;
            bool builtInHeaderDrawn = false;
            for (const VfxParticleTextureChoice& choice : catalog.choices) {
                if (!matchesFilter(choice)) {
                    continue;
                }
                if (!choice.builtIn && !separatorDrawn) {
                    if (column != 0) {
                        ImGui::NewLine();
                        column = 0;
                    }
                    ImGui::SeparatorText(VfxUiText("Project Textures").c_str());
                    separatorDrawn = true;
                }
                if (choice.builtIn && !builtInHeaderDrawn) {
                    if (column != 0) {
                        ImGui::NewLine();
                        column = 0;
                    }
                    ImGui::SeparatorText(VfxUiText("Engine VFX Textures").c_str());
                    builtInHeaderDrawn = true;
                }
                beginCell();
                if (drawCell(choice.guid, choice.label)) {
                    outSelected = choice.guid;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                endCell();
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::EndGroup();

    DroppedAssetPayload dropped{};
    if (AcceptAssetDropOfType(assetDatabase, AssetType::Texture, dropped) && dropped.record != nullptr) {
        outSelected = dropped.guid;
        outMessage = "Particle color texture assigned by drag and drop";
        changed = true;
    }
    if (current.IsValid() && selected == nullptr) {
        ImGui::TextColored({1.0f, 0.48f, 0.30f, 1.0f},
                           TEXT::Localize("editor.ui.the.assigned.particle.texture.is.missing.f68d5f").c_str());
    }
    ImGui::TextDisabled(
        VfxUiText("Engine VFX textures are shared; project Texture assets can be dropped here.").c_str());
    return changed;
}

} // namespace HIKARI::EDITOR
