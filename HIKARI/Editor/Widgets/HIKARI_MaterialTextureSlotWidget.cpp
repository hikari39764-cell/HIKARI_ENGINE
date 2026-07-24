#include "HIKARI_MaterialTextureSlotWidget.h"

#include <string>
#include <unordered_map>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorAssetIcons.h"
#include "Render2D/HIKARI_DxTexture.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    namespace {
        std::string ShortGuid(const std::string& guid) {
            if (guid.size() <= 8) {
                return guid;
            }
            return guid.substr(0, 8);
        }

        const AssetRecord* ResolveTextureRecord(
            const MaterialTextureSlotData& slot,
            const AssetDatabase& assetDatabase) {

            if (!slot.textureAssetGuid.IsValid()) {
                return nullptr;
            }
            return assetDatabase.FindByGuid(slot.textureAssetGuid);
        }

#if defined(HIKARI_WITH_EDITOR)
        std::unordered_map<std::string, int> gPreviewTextureHandles{};

        DXTEX::TextureColorSpace ToRuntimeColorSpace(TextureAssetColorSpace colorSpace) {
            switch (colorSpace) {
            case TextureAssetColorSpace::Linear:
                return DXTEX::TextureColorSpace::Linear;
            case TextureAssetColorSpace::Srgb:
                return DXTEX::TextureColorSpace::Srgb;
            case TextureAssetColorSpace::Auto:
            default:
                return DXTEX::TextureColorSpace::Auto;
            }
        }

        int ResolvePreviewHandle(
            const MaterialTextureSlotData& slot,
            const TextureAssetDescriptor* descriptor) {

            if (!slot.useTexture || !slot.textureAssetGuid.IsValid() ||
                descriptor == nullptr || descriptor->sourcePath.empty()) {
                return -1;
            }

            const std::string cacheKey = slot.textureAssetGuid.value + "|" + descriptor->sourcePath;
            auto found = gPreviewTextureHandles.find(cacheKey);
            if (found != gPreviewTextureHandles.end()) {
                return found->second;
            }

            // Use the HTEX/DDS path resolved by the asset registry for previews.
            const int handle = DXTEX::DxTextureManager::LoadTextureWithColorSpace(
                "editor/material_slot_preview/" + slot.textureAssetGuid.value,
                descriptor->sourcePath,
                ToRuntimeColorSpace(descriptor->colorSpace));
            gPreviewTextureHandles.emplace(cacheKey, handle);
            return handle;
        }

        bool DrawTexturePreviewButton(
            const char* id,
            const MaterialTextureSlotData& slot,
            const TextureAssetDescriptor* descriptor,
            const ImVec2& size) {

            const int previewHandle = ResolvePreviewHandle(slot, descriptor);
            if (previewHandle >= 0) {
                const D3D12_GPU_DESCRIPTOR_HANDLE srv =
                    DXTEX::DxTextureManager::GetSrvGpuHandle(previewHandle);
                if (srv.ptr != 0) {
                    const ImTextureID textureId =
                        reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr));
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
                    const bool clicked = ImGui::ImageButton(
                        id,
                        textureId,
                        size,
                        ImVec2(0.0f, 0.0f),
                        ImVec2(1.0f, 1.0f),
                        ImVec4(0.06f, 0.08f, 0.10f, 1.0f),
                        ImVec4(1.0f, 1.0f, 1.0f, slot.useTexture ? 1.0f : 0.42f));
                    ImGui::PopStyleVar();
                    return clicked;
                }
            }

            return ImGui::Button(id, size);
        }
#endif
    }

    bool DrawMaterialTextureSlot(
        const char* label,
        MaterialTextureSlotData& slot,
        AssetDatabase& assetDatabase,
        AssetRegistry& assetRegistry,
        EditorSelection* selection) {
#if defined(HIKARI_WITH_EDITOR)
        bool changed = false;
        const AssetRecord* record = ResolveTextureRecord(slot, assetDatabase);
        const auto* descriptor = slot.textureAssetGuid.IsValid()
            ? assetRegistry.FindAs<TextureAssetDescriptor>(AssetId{ slot.textureAssetGuid.value })
            : nullptr;

        ImGui::PushID(label);
        ImGui::BeginGroup();
        ImGui::TextUnformatted(label);

        const ImVec2 slotSize{ 64.0f, 64.0f };
        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        DrawTexturePreviewButton("##TextureSlotBox", slot, descriptor, slotSize);
        ImGui::PopStyleVar();
        DroppedAssetPayload slotPayload{};
        if (AcceptAssetDropOfType(assetDatabase, AssetType::Texture, slotPayload) && slotPayload.record) {
            slot.useTexture = true;
            slot.textureAssetGuid = slotPayload.guid;
            changed = true;
        }

        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRect(min, max, IM_COL32(78, 94, 112, 180), 6.0f);
        if (!descriptor || descriptor->sourcePath.empty()) {
            drawList->AddRectFilled(min, max, IM_COL32(18, 24, 31, 255), 6.0f);
            ImGui::SetCursorScreenPos(ImVec2(min.x + 16.0f, min.y + 16.0f));
            DrawAssetTypeGlyph(AssetType::Texture, ImVec2(32.0f, 32.0f));
        }
        ImGui::SetCursorScreenPos(ImVec2(max.x + 8.0f, min.y));
        ImGui::EndGroup();

        ImGui::SameLine();
        ImGui::BeginGroup();
        if (ImGui::Checkbox("Use Texture", &slot.useTexture)) {
            changed = true;
        }

        const std::string name = record
            ? (record->displayName.empty() ? record->sourcePath.stem().string() : record->displayName)
            : std::string("<none>");
        ImGui::Text("Texture: %s", name.c_str());
        if (slot.textureAssetGuid.IsValid()) {
            ImGui::TextDisabled("GUID: %s", ShortGuid(slot.textureAssetGuid.value).c_str());
        }
        if (descriptor && !descriptor->sourcePath.empty()) {
            ImGui::TextDisabled("Runtime: %s", descriptor->sourcePath.c_str());
        }

        if (ImGui::SmallButton("Select") && record && selection) {
            selection->ClearObjects();
            selection->selectedAsset = nullptr;
            selection->selectedAssetGuid = record->guid.value;
            selection->selectedAssetPath = record->sourcePath.generic_string();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            slot.useTexture = false;
            slot.textureAssetGuid = {};
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy GUID") && slot.textureAssetGuid.IsValid()) {
            ImGui::SetClipboardText(slot.textureAssetGuid.value.c_str());
        }
        ImGui::EndGroup();

        // Non-texture assets are not accepted by this slot.
        ImGui::EndGroup();
        ImGui::PopID();
        return changed;
#else
        (void)label;
        (void)slot;
        (void)assetDatabase;
        (void)assetRegistry;
        (void)selection;
        return false;
#endif
    }

    void ClearMaterialTextureSlotPreviewCache() {
#if defined(HIKARI_WITH_EDITOR)
        gPreviewTextureHandles.clear();
#endif
    }

    void InvalidateMaterialTextureSlotPreviewByGuid(const AssetGuid& guid) {
#if defined(HIKARI_WITH_EDITOR)
        if (!guid.IsValid()) {
            return;
        }

        const std::string prefix = guid.value + "|";
        for (auto it = gPreviewTextureHandles.begin(); it != gPreviewTextureHandles.end();) {
            if (it->first.rfind(prefix, 0) == 0) {
                it = gPreviewTextureHandles.erase(it);
            } else {
                ++it;
            }
        }
#else
        (void)guid;
#endif
    }

    void InvalidateMaterialTextureSlotPreviewByPath(const std::string& path) {
#if defined(HIKARI_WITH_EDITOR)
        if (path.empty()) {
            return;
        }

        for (auto it = gPreviewTextureHandles.begin(); it != gPreviewTextureHandles.end();) {
            if (it->first.find("|" + path) != std::string::npos) {
                it = gPreviewTextureHandles.erase(it);
            } else {
                ++it;
            }
        }
#else
        (void)path;
#endif
    }

} // namespace HIKARI::EDITOR
