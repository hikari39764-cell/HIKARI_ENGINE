#include "HIKARI_EditorAssetDragDrop.h"

#include <cstring>
#include <string>

#include "Editor/Style/HIKARI_EditorAssetIcons.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    
    bool BeginAssetDragSource(const AssetRecord& record) {
#if defined(HIKARI_WITH_EDITOR)
        if (!record.guid.IsValid()) {
            return false;
        }
        if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            return false;
        }

        ImGui::SetDragDropPayload(
            kAssetGuidPayload,
            record.guid.value.c_str(),
            record.guid.value.size() + 1u);

        DrawAssetTypeGlyph(record.type, ImVec2(18.0f, 18.0f));
        ImGui::SameLine();
        const std::string displayName = record.displayName.empty()
            ? record.sourcePath.filename().string()
            : record.displayName;
        ImGui::Text("%s", displayName.c_str());
        ImGui::TextDisabled("%s", record.sourcePath.generic_string().c_str());
        ImGui::EndDragDropSource();
        return true;
#else
        (void)record;
        return false;
#endif
    }

    bool AcceptAssetDrop(const AssetDatabase& assetDatabase, DroppedAssetPayload& outPayload) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::BeginDragDropTarget()) {
            return false;
        }

        bool accepted = false;
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetGuidPayload)) {
            const char* payloadText = static_cast<const char*>(payload->Data);
            if (payloadText && payload->DataSize > 0) {
                const std::string guid(payloadText, payloadText + std::strlen(payloadText));
                outPayload.guid = AssetGuid{ guid };
                outPayload.record = assetDatabase.FindByGuid(outPayload.guid);
                accepted = outPayload.record != nullptr;
            }
        }
        ImGui::EndDragDropTarget();
        return accepted;
#else
        (void)assetDatabase;
        (void)outPayload;
        return false;
#endif
    }

    bool AcceptAssetDropOfType(
        const AssetDatabase& assetDatabase,
        AssetType requiredType,
        DroppedAssetPayload& outPayload) {

        DroppedAssetPayload payload{};
        if (!AcceptAssetDrop(assetDatabase, payload)) {
            return false;
        }
        if (!payload.record || payload.record->type != requiredType) {
            return false;
        }
        outPayload = payload;
        return true;
    }

} // namespace HIKARI::EDITOR
