#include "HIKARI_EditorAssetDragDrop.h"

#include <cstring>
#include <string>

#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Editor/Style/HIKARI_EditorAssetIcons.h"

#include "imgui.h"

namespace HIKARI::EDITOR {

bool BeginAssetDragSource(const AssetRecord& record) {
    if (!record.guid.IsValid()) {
        return false;
    }
    if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        return false;
    }

    ImGui::SetDragDropPayload(kAssetGuidPayload, record.guid.value.c_str(), record.guid.value.size() + 1u);

    DrawAssetTypeGlyph(record.type, ImVec2(18.0f, 18.0f));
    ImGui::SameLine();
    const std::string displayName =
        record.displayName.empty() ? record.sourcePath.filename().string() : record.displayName;
    ImGui::Text("%s", displayName.c_str());
    ImGui::TextDisabled("%s", record.sourcePath.generic_string().c_str());
    ImGui::EndDragDropSource();
    return true;
}

bool AcceptAssetDrop(const AssetDatabase& assetDatabase, DroppedAssetPayload& outPayload) {
    if (!ImGui::BeginDragDropTarget()) {
        return false;
    }

    bool accepted = false;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetGuidPayload)) {
        const char* payloadText = static_cast<const char*>(payload->Data);
        if (payloadText && payload->DataSize > 0) {
            const std::string guid(payloadText, payloadText + std::strlen(payloadText));
            outPayload.guid = AssetGuid{guid};
            outPayload.record = assetDatabase.FindByGuid(outPayload.guid);
            accepted = outPayload.record != nullptr;
        }
    }
    ImGui::EndDragDropTarget();
    return accepted;
}

bool AcceptAssetDropOfType(const AssetDatabase& assetDatabase, AssetType requiredType, DroppedAssetPayload& outPayload,
                           std::string_view moduleAssetTypeId) {
    DroppedAssetPayload payload{};
    if (!AcceptAssetDrop(assetDatabase, payload)) {
        return false;
    }
    if (!payload.record || !ASSETS::SEMANTICS::MatchesAssetType(payload.record->type, payload.record->sourcePath,
                                                                requiredType, moduleAssetTypeId)) {
        return false;
    }
    outPayload = payload;
    return true;
}

} // namespace HIKARI::EDITOR
