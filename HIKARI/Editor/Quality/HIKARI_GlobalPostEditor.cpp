#include "Editor/Quality/HIKARI_GlobalPostEditor.h"

#include <functional>
#include <memory>
#include <utility>

#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Post/HIKARI_PostAssetStore.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Widgets/HIKARI_FxParameterWidget.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_World.h"
#include "Vfx/Parameters/HIKARI_VfxParameterValues.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

#if defined(HIKARI_WITH_EDITOR)
namespace {
constexpr ImVec4 kErrorColor{1.0f, 0.45f, 0.45f, 1.0f};
constexpr ImVec4 kWarningColor{1.0f, 0.72f, 0.25f, 1.0f};

std::string BuiltinLabel(const POST::BuiltinPostContent& content) {
    return std::string(content.displayName) + TEXT::Localize("post.content.engineSuffix");
}

std::string ProfileLabel(std::string_view id, const AssetDatabase& database) {
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

void GlobalPostEditor::Draw(ScenePostSettings& post, DocumentSceneBase& scene) {
    AssetDatabase& database = scene.GetAssetDatabase();
    const bool wasEnabled = post.enabled;
    if (ImGui::Checkbox(TEXT::Localize("ui.quality.postEnabled").c_str(), &post.enabled) && post.enabled &&
        !wasEnabled && post.profileId.empty()) {
        post.profileId = POST::kDefaultPostProfileId;
    }
    DrawProfilePicker(post, database);
    auto* store = scene.GetWorld().Services().Find<POST::PostAssetStore>();
    if (store && !post.profileId.empty()) {
        if (post.profileId.starts_with(POST::kBuiltinPostProfilePrefix)) {
            DrawBuiltinProfile(post, database, *store);
        } else {
            DrawProjectProfile(AssetGuid{post.profileId}, database, *store);
        }
    }
    if (store) {
        PublishDraft(*store);
    }
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
}

void GlobalPostEditor::DrawProfilePicker(ScenePostSettings& post, const AssetDatabase& database) const {
    if (!ImGui::BeginCombo(TEXT::Localize("ui.quality.globalPostProfile").c_str(),
                           ProfileLabel(post.profileId, database).c_str())) {
        return;
    }
    if (ImGui::Selectable(TEXT::Localize("ui.common.none").c_str(), post.profileId.empty())) {
        post.profileId.clear();
    }
    for (const POST::BuiltinPostContent& content : POST::GetBuiltinPostContent()) {
        const bool selected = post.profileId == content.profileId;
        if (ImGui::Selectable(BuiltinLabel(content).c_str(), selected)) {
            post.profileId = content.profileId;
        }
    }
    ImGui::Separator();
    for (const AssetRecord* record : database.CollectByType(AssetType::PostProfile)) {
        const bool selected = post.profileId == record->guid.value;
        if (ImGui::Selectable((GetAssetRecordDisplayName(*record) + "##" + record->guid.value).c_str(), selected)) {
            post.profileId = record->guid.value;
        }
    }
    ImGui::EndCombo();
}

void GlobalPostEditor::DrawBuiltinProfile(ScenePostSettings& post, AssetDatabase& database,
                                          const POST::PostAssetStore& store) {
    std::string error;
    const auto profile = store.LoadProfile(post.profileId, error);
    const auto effect = profile ? store.LoadEffect(profile->effectId, error) : nullptr;
    if (!effect) {
        ImGui::TextColored(kErrorColor, "%s", error.c_str());
        return;
    }
    ImGui::TextDisabled("%s", TEXT::Localize("post.profile.builtinReadOnly").c_str());
    if (ImGui::Button(TEXT::Localize("post.profile.createCopy").c_str())) {
        std::string message;
        if (CreateProjectCopy(*profile, database, message)) {
            post.profileId = document_.Guid().value;
            status_.clear();
        } else {
            status_ = message;
        }
    }
    ImGui::BeginDisabled();
    DrawParameters(*effect, *profile, false);
    ImGui::EndDisabled();
}

bool GlobalPostEditor::OpenProjectProfile(const AssetGuid& guid, AssetDatabase& database) {
    if (document_.IsOpen() && document_.Guid() == guid) {
        return true;
    }
    std::string message;
    if (document_.Open(database, guid, message)) {
        status_.clear();
        return true;
    }
    if (!document_.IsOpen() || !document_.IsDirty()) {
        ImGui::TextColored(kErrorColor, "%s", message.c_str());
        return false;
    }
    // 別のプロファイルに未保存の変更がある。保存するか破棄してから切り替える。
    ImGui::TextColored(kWarningColor, "%s", TEXT::Localize("post.profile.pendingOther").c_str());
    if (ImGui::Button(TEXT::Localize("menu.edit.save").c_str()) && !document_.Save(database, message)) {
        status_ = message;
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("post.profile.discardAndOpen").c_str())) {
        if (document_.Open(database, guid, message, true)) {
            status_.clear();
            return true;
        }
        status_ = message;
    }
    return false;
}

void GlobalPostEditor::DrawProjectProfile(const AssetGuid& guid, AssetDatabase& database, POST::PostAssetStore& store) {
    if (!OpenProjectProfile(guid, database)) {
        return;
    }
    DrawDocumentToolbar(database);
    DrawEffectPicker(database);
    std::string error;
    const auto effect = store.LoadEffect(document_.Profile().effectId, error);
    if (!effect) {
        ImGui::TextColored(kErrorColor, "%s", error.c_str());
        return;
    }
    if (!POST::ValidatePostProfileValues(document_.Profile(), *effect, error)) {
        ImGui::TextColored(kWarningColor, "%s", error.c_str());
    }
    DrawParameters(*effect, document_.Profile(), true);
}

void GlobalPostEditor::DrawDocumentToolbar(AssetDatabase& database) {
    std::string message;
    ImGui::BeginDisabled(!document_.CanUndo());
    if (ImGui::Button(TEXT::Localize("menu.edit.undo").c_str())) {
        (void)document_.Undo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!document_.CanRedo());
    if (ImGui::Button(TEXT::Localize("menu.edit.redo").c_str())) {
        (void)document_.Redo();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!document_.IsDirty());
    if (ImGui::Button(TEXT::Localize("menu.edit.save").c_str())) {
        status_ = document_.Save(database, message) ? std::string{} : message;
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("ui.assetInspector.revert").c_str())) {
        status_ = document_.Open(database, document_.Guid(), message, true) ? std::string{} : message;
    }
    ImGui::EndDisabled();
    if (document_.IsDirty()) {
        ImGui::SameLine();
        ImGui::TextColored(kWarningColor, "%s", TEXT::Localize("post.profile.unsaved").c_str());
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

void GlobalPostEditor::DrawParameters(const POST::PostEffectDefinition& effect, const POST::PostProfileAsset& profile,
                                      bool editable) {
    if (!ImGui::TreeNodeEx(TEXT::Localize("ui.quality.profileParameters").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    const POST::PostParameterBlock block = POST::ResolvePostParameters(effect, &profile);
    for (const VFX::ParamDesc& param : effect.params) {
        ImGui::PushID(param.key.c_str());
        DirectX::XMFLOAT4 slot = block[param.ref.slot];
        if (DrawFxParameter(param, slot) && editable) {
            const float* channels = &slot.x;
            POST::PostParameterValue value{};
            const size_t count = VFX::GetParameterComponentCount(param.type);
            for (size_t index = 0; index < count && param.ref.channel + index < 4u; ++index) {
                value[index] = channels[param.ref.channel + index];
            }
            POST::PostProfileAsset edited = document_.Profile();
            POST::SetPostParameterValue(param, edited, value);
            (void)document_.Apply(std::move(edited), status_, ParameterMergeGroup(param.key));
        }
        if (editable && ImGui::IsItemDeactivatedAfterEdit()) {
            document_.SealMerge();
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
}

bool GlobalPostEditor::CreateProjectCopy(POST::PostProfileAsset source, AssetDatabase& database, std::string& message) {
    return document_.New(std::move(source), message) && document_.Save(database, message);
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
#else
void GlobalPostEditor::Draw(ScenePostSettings&, DocumentSceneBase&) {}
#endif

} // namespace HIKARI::EDITOR
