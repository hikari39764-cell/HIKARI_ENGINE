#include "Editor/Quality/HIKARI_GlobalPostEditor.h"

#include <functional>
#include <memory>
#include <utility>

#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Post/HIKARI_PostAssetStore.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Documents/HIKARI_ValidatedAssetDocumentUi.h"
#include "Editor/Widgets/HIKARI_FxParameterWidget.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_World.h"
#include "Vfx/Parameters/HIKARI_VfxParameterValues.h"

#include "imgui.h"

namespace HIKARI::EDITOR {

namespace {
std::string BuiltinLabel(const POST::BuiltinPostContent& content) {
    return std::string(content.displayName) + TEXT::Localize("asset.content.engineSuffix");
}

std::string EffectLabel(std::string_view id, const AssetDatabase& database) {
    for (const POST::BuiltinPostContent& content : POST::GetBuiltinPostContent()) {
        if (content.effectId == id) {
            return BuiltinLabel(content);
        }
    }
    const AssetRecord* record = database.FindByGuid(AssetGuid{std::string(id)});
    return record ? GetAssetRecordDisplayName(*record) : std::string(id);
}

uint64_t ParameterMergeGroup(std::string_view key) { return std::hash<std::string_view>{}(key) | 1u; }
} // namespace

std::string ResolvePostProfileLabel(std::string_view id, const AssetDatabase& database) {
    if (id.empty()) {
        return TEXT::Localize("ui.common.none");
    }
    for (const POST::BuiltinPostContent& content : POST::GetBuiltinPostContent()) {
        if (content.profileId == id) {
            return BuiltinLabel(content);
        }
    }
    const AssetRecord* record = database.FindByGuid(AssetGuid{std::string(id)});
    return record ? GetAssetRecordDisplayName(*record) : std::string(id);
}

bool GlobalPostEditor::DrawProfilePicker(std::string& profileId, const AssetDatabase& database,
                                         const char* preview) const {
    const std::string label = preview != nullptr ? std::string(preview) : ResolvePostProfileLabel(profileId, database);
    if (!ImGui::BeginCombo("##postProfile", label.c_str())) {
        return false;
    }
    const std::string before = profileId;
    for (const POST::BuiltinPostContent& content : POST::GetBuiltinPostContent()) {
        if (ImGui::Selectable(BuiltinLabel(content).c_str(), profileId == content.profileId)) {
            profileId = content.profileId;
        }
    }
    ImGui::Separator();
    for (const AssetRecord* record : database.CollectByType(AssetType::PostProfile)) {
        if (ImGui::Selectable((GetAssetRecordDisplayName(*record) + "##" + record->guid.value).c_str(),
                              profileId == record->guid.value)) {
            profileId = record->guid.value;
        }
    }
    ImGui::EndCombo();
    return profileId != before;
}

std::string GlobalPostEditor::DrawSelectedProfile(const std::string& profileId, DocumentSceneBase& scene) {
    auto* store = scene.GetWorld().Services().Find<POST::PostAssetStore>();
    std::string copiedId;
    if (store && !profileId.empty()) {
        if (profileId.starts_with(POST::kBuiltinPostProfilePrefix)) {
            copiedId = DrawBuiltinProfile(profileId, scene.GetAssetDatabase(), *store);
        } else {
            DrawProjectProfile(AssetGuid{profileId}, scene.GetAssetDatabase(), *store);
        }
    }
    if (store) {
        PublishDraft(*store);
    }
    return copiedId;
}

std::string GlobalPostEditor::DrawBuiltinProfile(const std::string& profileId, AssetDatabase& database,
                                                 const POST::PostAssetStore& store) {
    std::string error;
    const auto profile = store.LoadProfile(profileId, error);
    const auto effect = profile ? store.LoadEffect(profile->effectId, error) : nullptr;
    if (!effect) {
        ImGui::TextColored(kAssetDocumentErrorColor, "%s", error.c_str());
        return {};
    }
    // 最初の編集でその値ごとプロジェクトへ複製し、呼び出し側が複製へ切り替える。
    ImGui::TextDisabled("%s", TEXT::Localize("asset.document.builtinCopyOnEdit").c_str());
    DrawAssetDocumentStatus(status_);
    bool copy = ImGui::Button(TEXT::Localize("asset.document.createCopy").c_str());
    POST::PostProfileAsset edited = *profile;
    copy |= DrawParameters(*effect, edited) != nullptr;
    if (!copy) {
        return {};
    }
    std::string message;
    if (!document_.SaveAsNew(std::move(edited), database, message)) {
        status_ = message;
        return {};
    }
    status_.clear();
    return document_.Guid().value;
}

void GlobalPostEditor::DrawProjectProfile(const AssetGuid& guid, AssetDatabase& database, POST::PostAssetStore& store) {
    if (!OpenAssetDocumentForEdit(document_, guid, database, status_)) {
        return;
    }
    DrawAssetDocumentToolbar(document_, database, status_);
    DrawEffectPicker(database);
    std::string error;
    const auto effect = store.LoadEffect(document_.Profile().effectId, error);
    if (!effect) {
        ImGui::TextColored(kAssetDocumentErrorColor, "%s", error.c_str());
        return;
    }
    if (!POST::ValidatePostProfileValues(document_.Profile(), *effect, error)) {
        ImGui::TextColored(kAssetDocumentWarningColor, "%s", error.c_str());
    }
    POST::PostProfileAsset edited = document_.Profile();
    if (const VFX::ParamDesc* changed = DrawParameters(*effect, edited)) {
        (void)document_.Apply(std::move(edited), status_, ParameterMergeGroup(changed->key));
    }
}

void GlobalPostEditor::DrawEffectPicker(const AssetDatabase& database) {
    const std::string& currentId = document_.Profile().effectId;
    std::string selectedId = currentId;
    if (ImGui::BeginCombo(TEXT::Localize("post.profile.effect").c_str(), EffectLabel(currentId, database).c_str())) {
        for (const POST::BuiltinPostContent& content : POST::GetBuiltinPostContent()) {
            if (ImGui::Selectable(BuiltinLabel(content).c_str(), currentId == content.effectId)) {
                selectedId = content.effectId;
            }
        }
        ImGui::Separator();
        for (const AssetRecord* record : database.CollectByType(AssetType::PostEffect)) {
            if (ImGui::Selectable((GetAssetRecordDisplayName(*record) + "##" + record->guid.value).c_str(),
                                  currentId == record->guid.value)) {
                selectedId = record->guid.value;
            }
        }
        ImGui::EndCombo();
    }
    if (selectedId != currentId) {
        // パラメーターは効果ごとに違うので、効果を替えたら既定値から始める。
        POST::PostProfileAsset edited = document_.Profile();
        edited.effectId = std::move(selectedId);
        edited.values.clear();
        (void)document_.Apply(std::move(edited), status_);
    }
    if (ImGui::Button(TEXT::Localize("post.profile.resetDefaults").c_str()) && !document_.Profile().values.empty()) {
        POST::PostProfileAsset edited = document_.Profile();
        edited.values.clear();
        (void)document_.Apply(std::move(edited), status_);
    }
}

const VFX::ParamDesc* GlobalPostEditor::DrawParameters(const POST::PostEffectDefinition& effect,
                                                       POST::PostProfileAsset& profile) {
    if (!ImGui::TreeNodeEx(TEXT::Localize("ui.quality.profileParameters").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return nullptr;
    }
    const VFX::ParamDesc* changed = nullptr;
    const POST::PostParameterBlock block = POST::ResolvePostParameters(effect, &profile);
    for (const VFX::ParamDesc& param : effect.params) {
        ImGui::PushID(param.key.c_str());
        DirectX::XMFLOAT4 slot = block[param.ref.slot];
        if (DrawFxParameter(param, slot)) {
            const float* channels = &slot.x;
            POST::PostParameterValue value{};
            const size_t count = VFX::GetParameterComponentCount(param.type);
            for (size_t index = 0; index < count && param.ref.channel + index < 4u; ++index) {
                value[index] = channels[param.ref.channel + index];
            }
            POST::SetPostParameterValue(param, profile, value);
            changed = &param;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            document_.SealMerge();
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
    return changed;
}

void GlobalPostEditor::PublishDraft(POST::PostAssetStore& store) {
    if (!document_.IsOpen() || !document_.IsDirty() || !document_.Guid().IsValid()) {
        if (publishedRevision_ != 0) {
            store.ClearProfileDraft();
            publishedRevision_ = 0;
        }
        return;
    }
    if (publishedRevision_ != document_.Revision()) {
        store.SetProfileDraft(document_.Guid().value,
                              std::make_shared<const POST::PostProfileAsset>(document_.Profile()));
        publishedRevision_ = document_.Revision();
    }
}

} // namespace HIKARI::EDITOR
