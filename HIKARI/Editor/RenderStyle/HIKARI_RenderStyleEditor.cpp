#include "Editor/RenderStyle/HIKARI_RenderStyleEditor.h"

#include <memory>
#include <optional>
#include <utility>

#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Post/HIKARI_PostAssetStore.h"
#include "Assets/RenderStyle/HIKARI_RenderStyleAssetStore.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_World.h"

#include "Editor/Documents/HIKARI_ValidatedAssetDocumentUi.h"
#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"
#include "Editor/Widgets/HIKARI_JsonFieldsEditor.h"
#include "imgui.h"

namespace HIKARI::EDITOR {

using RENDERSTYLE::RenderStyleAsset;
using RENDERSTYLE::RenderStylePostLayer;

void RenderStyleEditor::PublishDrafts(DocumentSceneBase& scene) {
    const bool draft = document_.IsOpen() && document_.IsDirty() && document_.Guid().IsValid();
    const uint64_t revision = draft ? document_.Revision() : 0u;
    if (revision != publishedRevision_) {
        scene.GetRenderStyleAssets().SetDraft(
            document_.Guid().value, draft ? std::make_shared<const RenderStyleAsset>(document_.Profile()) : nullptr);
        publishedRevision_ = revision;
    }
    if (auto* postStore = scene.GetWorld().Services().Find<POST::PostAssetStore>()) {
        globalPost_.PublishDraft(*postStore);
    }
}

namespace {
constexpr uint64_t kFieldMergeGroup = 0x525354u;

FieldEditResult DrawShading(RENDERSTYLE::RenderStyleShading& shading) {
    ImGui::SeparatorText(TEXT::Localize("ui.renderStyle.shading").c_str());
    return DrawJsonFields("ui.renderStyle.shading", shading, RENDERSTYLE::kRenderStyleShadingFields);
}

std::string BuiltinLabel(const RENDERSTYLE::BuiltinRenderStyleContent& content) {
    return std::string(content.displayName) + TEXT::Localize("asset.content.engineSuffix");
}
} // namespace

std::string ResolveRenderStyleLabel(std::string_view id, const AssetDatabase& database) {
    for (const RENDERSTYLE::BuiltinRenderStyleContent& content : RENDERSTYLE::GetBuiltinRenderStyles()) {
        if (content.id == id) {
            return BuiltinLabel(content);
        }
    }
    const AssetRecord* record = database.FindByGuid(AssetGuid{std::string(id)});
    return record ? GetAssetRecordDisplayName(*record) : std::string(id);
}

bool DrawRenderStylePicker(const AssetDatabase& database, std::string& id) {
    if (!ImGui::BeginCombo("##renderStyle", ResolveRenderStyleLabel(id, database).c_str())) {
        return false;
    }
    const std::string before = id;
    for (const RENDERSTYLE::BuiltinRenderStyleContent& content : RENDERSTYLE::GetBuiltinRenderStyles()) {
        if (ImGui::Selectable(BuiltinLabel(content).c_str(), id == content.id)) {
            id = content.id;
        }
    }
    ImGui::Separator();
    for (const AssetRecord* record : database.CollectByType(AssetType::RenderStyle)) {
        if (ImGui::Selectable((GetAssetRecordDisplayName(*record) + "##" + record->guid.value).c_str(),
                              id == record->guid.value)) {
            id = record->guid.value;
        }
    }
    ImGui::EndCombo();
    return id != before;
}

bool RenderStyleEditor::DrawPostLayers(RenderStyleAsset& style, DocumentSceneBase& scene) {
    const AssetDatabase& database = scene.GetAssetDatabase();
    bool changed = false;
    ImGui::SeparatorText(TEXT::Localize("ui.renderStyle.postLayers").c_str());
    if (style.post.empty()) {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.renderStyle.noPostLayers").c_str());
    }
    std::optional<size_t> removed;
    std::optional<size_t> movedUp;
    const float buttons = ImGui::GetFrameHeight() * 3.0f + ImGui::GetStyle().ItemSpacing.x * 3.0f;
    for (size_t index = 0; index < style.post.size(); ++index) {
        RenderStylePostLayer& layer = style.post[index];
        ImGui::PushID(static_cast<int>(index));
        changed |= ImGui::Checkbox("##enabled", &layer.enabled);
        ImGui::SameLine();
        const float width = ImGui::GetContentRegionAvail().x - buttons;
        if (ImGui::Selectable(ResolvePostProfileLabel(layer.profileId, database).c_str(), selectedLayer_ == index, 0,
                              ImVec2(width, 0.0f))) {
            selectedLayer_ = index;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(index == 0u);
        if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
            movedUp = index;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(index + 1u == style.post.size());
        if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
            movedUp = index + 1u;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("X", ImVec2(ImGui::GetFrameHeight(), 0.0f))) {
            removed = index;
        }
        ImGui::PopID();
    }
    if (movedUp) {
        std::swap(style.post[*movedUp - 1u], style.post[*movedUp]);
        selectedLayer_ = selectedLayer_ == *movedUp ? *movedUp - 1u : selectedLayer_;
        changed = true;
    }
    if (removed) {
        style.post.erase(style.post.begin() + static_cast<std::ptrdiff_t>(*removed));
        selectedLayer_ = selectedLayer_ > 0u && selectedLayer_ >= *removed ? selectedLayer_ - 1u : selectedLayer_;
        changed = true;
    }
    if (style.post.size() < RENDERSTYLE::kMaxRenderStylePostLayers) {
        std::string added;
        ImGui::SetNextItemWidth(-1.0f);
        if (globalPost_.DrawProfilePicker(added, database, TEXT::Localize("ui.renderStyle.addPostLayer").c_str()) &&
            !added.empty()) {
            style.post.push_back({std::move(added), true});
            selectedLayer_ = style.post.size() - 1u;
            changed = true;
        }
    }
    if (selectedLayer_ < style.post.size()) {
        RenderStylePostLayer& layer = style.post[selectedLayer_];
        ImGui::SeparatorText(ResolvePostProfileLabel(layer.profileId, database).c_str());
        {
            InspectorPropertyRow row(TEXT::Localize("ui.renderStyle.postProfile"));
            if (row.IsVisible()) {
                changed |= globalPost_.DrawProfilePicker(layer.profileId, database);
            }
        }
        ImGui::PushID("layerProfile");
        if (const std::string copied = globalPost_.DrawSelectedProfile(layer.profileId, scene); !copied.empty()) {
            layer.profileId = copied;
            changed = true;
        }
        ImGui::PopID();
    }
    return changed;
}

std::string RenderStyleEditor::DrawStyle(const std::string& id, DocumentSceneBase& scene) {
    AssetDatabase& database = scene.GetAssetDatabase();
    std::string copiedId;
    if (id.starts_with(RENDERSTYLE::kBuiltinRenderStylePrefix)) {
        std::string error;
        const auto asset = scene.GetRenderStyleAssets().Load(id, error);
        if (!asset) {
            ImGui::TextColored(kAssetDocumentErrorColor, "%s", error.c_str());
            return {};
        }
        // 内蔵は読み取り専用。陰影か後処理の層の最初の編集でその値ごとプロジェクトへ複製し、シーンを複製へ切り替える。
        ImGui::TextDisabled("%s", TEXT::Localize("asset.document.builtinCopyOnEdit").c_str());
        DrawAssetDocumentStatus(status_);
        bool copy = ImGui::Button(TEXT::Localize("asset.document.createCopy").c_str());
        RenderStyleAsset edited = *asset;
        copy |= DrawShading(edited.shading).changed;
        copy |= DrawPostLayers(edited, scene);
        if (copy && document_.SaveAsNew(std::move(edited), database, status_)) {
            copiedId = document_.Guid().value;
        }
    } else if (OpenAssetDocumentForEdit(document_, AssetGuid{id}, database, status_)) {
        DrawAssetDocumentToolbar(document_, database, status_);
        RenderStyleAsset edited = document_.Profile();
        FieldEditResult fields{};
        {
            InspectorPropertyRow row(TEXT::Localize("ui.renderStyle.name"));
            if (row.IsVisible()) {
                char buffer[128]{};
                edited.name.copy(buffer, sizeof(buffer) - 1u);
                if (ImGui::InputText("##name", buffer, sizeof(buffer))) {
                    edited.name = buffer;
                    fields.changed = true;
                }
                fields.active |= ImGui::IsItemActive();
            }
        }
        const FieldEditResult shading = DrawShading(edited.shading);
        fields.changed |= shading.changed;
        fields.active |= shading.active;
        if (fields.changed) {
            (void)document_.Apply(edited, status_, kFieldMergeGroup);
        } else if (!fields.active) {
            document_.SealMerge();
        }
        if (DrawPostLayers(edited, scene)) {
            (void)document_.Apply(std::move(edited), status_);
        }
    }
    return copiedId;
}

} // namespace HIKARI::EDITOR
