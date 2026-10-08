#include "Editor/Environment/HIKARI_EnvironmentAssetEditor.h"

#include <memory>
#include <span>
#include <type_traits>
#include <utility>

#include "Assets/Environment/HIKARI_EnvironmentAssetStore.h"
#include "Assets/Environment/HIKARI_EnvironmentFields.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "Editor/Documents/HIKARI_ValidatedAssetDocumentUi.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"
#include "Editor/Widgets/HIKARI_JsonFieldsEditor.h"
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

namespace {
using ENVIRONMENT::EnvironmentProfileAsset;
using ENVIRONMENT::WeatherPresetAsset;

std::span<const ENVIRONMENT::BuiltinEnvironmentContent> BuiltinContent(AssetType type) {
    return type == AssetType::Weather ? ENVIRONMENT::GetBuiltinWeatherPresets()
                                      : ENVIRONMENT::GetBuiltinEnvironmentProfiles();
}

std::string BuiltinLabel(const ENVIRONMENT::BuiltinEnvironmentContent& content) {
    return std::string(content.displayName) + TEXT::Localize("asset.content.engineSuffix");
}
} // namespace

std::string ResolveEnvironmentAssetLabel(AssetType type, std::string_view id, const AssetDatabase& database) {
    if (id.empty()) {
        return TEXT::Localize("ui.common.none");
    }
    for (const ENVIRONMENT::BuiltinEnvironmentContent& content : BuiltinContent(type)) {
        if (content.id == id) {
            return BuiltinLabel(content);
        }
    }
    const AssetRecord* record = database.FindByGuid(AssetGuid{std::string(id)});
    return record ? GetAssetRecordDisplayName(*record) : std::string(id);
}

bool EnvironmentAssetEditor::CreateProfileCopy(EnvironmentProfileAsset source, AssetDatabase& database,
                                               std::string& message) {
    return profile_.New(std::move(source), message) && profile_.Save(database, message);
}

bool EnvironmentAssetEditor::CreateWeatherCopy(WeatherPresetAsset source, AssetDatabase& database,
                                               std::string& message) {
    return weather_.New(std::move(source), message) && weather_.Save(database, message);
}

void EnvironmentAssetEditor::PublishDrafts(DocumentSceneBase& scene) {
    ENVIRONMENT::EnvironmentAssetStore& store = scene.GetEnvironmentAssets();
    const bool profileDraft = profile_.IsOpen() && profile_.IsDirty() && profile_.Guid().IsValid();
    const uint64_t profileRevision = profileDraft ? profile_.Revision() : 0u;
    if (profileRevision != publishedProfileRevision_) {
        store.SetProfileDraft(profile_.Guid().value,
                              profileDraft ? std::make_shared<const EnvironmentProfileAsset>(profile_.Profile())
                                           : nullptr);
        publishedProfileRevision_ = profileRevision;
        (void)scene.RefreshEnvironmentProfile();
    }
    const bool weatherDraft = weather_.IsOpen() && weather_.IsDirty() && weather_.Guid().IsValid();
    const uint64_t weatherRevision = weatherDraft ? weather_.Revision() : 0u;
    if (weatherRevision != publishedWeatherRevision_) {
        store.SetWeatherDraft(weather_.Guid().value,
                              weatherDraft ? std::make_shared<const WeatherPresetAsset>(weather_.Profile()) : nullptr);
        publishedWeatherRevision_ = weatherRevision;
        // 今の天気を編集しているなら、遷移なしで新しい値へ切り替える。
        ENVIRONMENT::EnvironmentRuntime& runtime = scene.GetEnvironmentRuntime();
        if (runtime.GetWeatherId() == weather_.Guid().value) {
            std::string error;
            (void)runtime.RequestWeather(runtime.GetWeatherId(), 0.0f, error);
        }
    }
}

#if defined(HIKARI_WITH_EDITOR)
namespace {
constexpr uint64_t kProfileFieldMergeGroup = 0x454E56u;
constexpr uint64_t kWeatherFieldMergeGroup = 0x575448u;

bool DrawNameField(std::string& name) {
    InspectorPropertyRow row(TEXT::Localize("ui.environment.assetName"));
    if (!row.IsVisible()) {
        return false;
    }
    char buffer[128]{};
    name.copy(buffer, sizeof(buffer) - 1u);
    if (!ImGui::InputText("##name", buffer, sizeof(buffer))) {
        return false;
    }
    name = buffer;
    return true;
}

// 表だけでは描けない項目: 空と色調整の資産の参照、設定で意味の無い項目。
struct ProfileCustomFields {
    const AssetDatabase& database;

    template <class T>
    bool operator()(std::string_view key, T& object, FieldEditResult& result) const {
        if constexpr (std::is_same_v<T, SkySettings>) {
            const bool texture = object.source != SkySource::Atmosphere;
            const bool panorama = object.source == SkySource::Panorama;
            if (key == "asset" || key == "panoramaCloudAsset") {
                const bool background = key == "asset";
                if (background ? texture : panorama) {
                    std::string& value = background ? object.asset : object.panoramaCloudAsset;
                    AssetFieldOptions options{};
                    options.label = background ? "ui.environment.sky.asset" : "ui.environment.sky.panoramaCloudAsset";
                    options.requiredType = background && !panorama ? AssetType::Sky : AssetType::Texture;
                    options.drawLabel = false;
                    InspectorPropertyRow row(ResolveFieldLabel("ui.environment.sky", key));
                    if (row.IsVisible()) {
                        result.changed |= DrawAssetField(&database, options, value);
                    }
                }
                return true;
            }
            if (key == "textureIlluminance") {
                return !texture;
            }
            if (key.starts_with("panoramaCloud")) {
                return !panorama;
            }
        } else if constexpr (std::is_same_v<T, SunSettings>) {
            if (key.starts_with("fixed")) {
                return object.mode != SunMode::Fixed;
            }
        } else if constexpr (std::is_same_v<T, ColorGradingSettings>) {
            if (key == "lut") {
                AssetFieldOptions options{};
                options.label = "ui.environment.colorGrading.lut";
                options.requiredType = AssetType::ColorLut;
                options.drawLabel = false;
                InspectorPropertyRow row(ResolveFieldLabel("ui.environment.colorGrading", key));
                if (row.IsVisible()) {
                    result.changed |= DrawAssetField(&database, options, object.lut);
                }
                return true;
            }
            if (key == "lutIntensity") {
                return object.lut.empty();
            }
        }
        return false;
    }
};

// プロファイルの全節。editable でなければ値を描くだけで、節の開閉はできる。
FieldEditResult DrawProfileSections(EnvironmentProfileAsset& asset, DocumentSceneBase& scene, bool editable) {
    FieldEditResult total{};
    const AssetDatabase& database = scene.GetAssetDatabase();
    ImGui::BeginDisabled(!editable);
    total.changed |= DrawNameField(asset.name);
    {
        InspectorPropertyRow row(TEXT::Localize("ui.environment.defaultWeather"));
        if (row.IsVisible()) {
            total.changed |= DrawEnvironmentAssetPicker(AssetType::Weather, database, asset.profile.defaultWeather);
        }
    }
    ImGui::EndDisabled();
    const ProfileCustomFields custom{database};
    ENVIRONMENT::FIELDS::VisitProfileSections([&](const char* key, auto member, const auto& fields) {
        const std::string label = ResolveFieldLabel("ui.environment.section", key) + "##" + key;
        if (!ImGui::TreeNode(label.c_str())) {
            return;
        }
        auto& section = asset.profile.*member;
        ImGui::BeginDisabled(!editable);
        const FieldEditResult result = DrawJsonFields(std::string("ui.environment.") + key, section, fields, custom);
        ImGui::EndDisabled();
        total.changed |= result.changed;
        total.active |= result.active;
        ImGui::TreePop();
    });
    return total;
}
} // namespace

bool DrawEnvironmentAssetPicker(AssetType type, const AssetDatabase& database, std::string& id,
                                const char* emptyLabelKey) {
    const std::string preview = id.empty() && emptyLabelKey != nullptr
                                    ? TEXT::Localize(emptyLabelKey)
                                    : ResolveEnvironmentAssetLabel(type, id, database);
    if (!ImGui::BeginCombo("##environmentAsset", preview.c_str())) {
        return false;
    }
    const std::string before = id;
    if (emptyLabelKey != nullptr && ImGui::Selectable(TEXT::Localize(emptyLabelKey).c_str(), id.empty())) {
        id.clear();
    }
    for (const ENVIRONMENT::BuiltinEnvironmentContent& content : BuiltinContent(type)) {
        if (ImGui::Selectable(BuiltinLabel(content).c_str(), id == content.id)) {
            id = content.id;
        }
    }
    ImGui::Separator();
    for (const AssetRecord* record : database.CollectByType(type)) {
        if (ImGui::Selectable((GetAssetRecordDisplayName(*record) + "##" + record->guid.value).c_str(),
                              id == record->guid.value)) {
            id = record->guid.value;
        }
    }
    ImGui::EndCombo();
    return id != before;
}

std::string EnvironmentAssetEditor::DrawProfile(const std::string& id, DocumentSceneBase& scene) {
    AssetDatabase& database = scene.GetAssetDatabase();
    std::string copiedId;
    if (id.starts_with(ENVIRONMENT::kBuiltinEnvironmentPrefix)) {
        std::string error;
        const auto asset = scene.GetEnvironmentAssets().LoadProfile(id, error);
        if (!asset) {
            ImGui::TextColored(kAssetDocumentErrorColor, "%s", error.c_str());
            return {};
        }
        ImGui::TextDisabled("%s", TEXT::Localize("asset.document.builtinReadOnly").c_str());
        if (ImGui::Button(TEXT::Localize("asset.document.createCopy").c_str()) &&
            CreateProfileCopy(*asset, database, status_)) {
            copiedId = profile_.Guid().value;
        }
        EnvironmentProfileAsset view = *asset;
        (void)DrawProfileSections(view, scene, false);
    } else if (OpenAssetDocumentForEdit(profile_, AssetGuid{id}, database, status_)) {
        DrawAssetDocumentToolbar(profile_, database, status_);
        EnvironmentProfileAsset edited = profile_.Profile();
        const FieldEditResult result = DrawProfileSections(edited, scene, true);
        if (result.changed) {
            (void)profile_.Apply(std::move(edited), status_, kProfileFieldMergeGroup);
        }
        if (!result.active) {
            profile_.SealMerge();
        }
    }
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
    return copiedId;
}

std::string EnvironmentAssetEditor::DrawWeather(const std::string& id, DocumentSceneBase& scene) {
    AssetDatabase& database = scene.GetAssetDatabase();
    std::string copiedId;
    if (id.starts_with(ENVIRONMENT::kBuiltinWeatherPrefix)) {
        std::string error;
        const auto asset = scene.GetEnvironmentAssets().LoadWeather(id, error);
        if (!asset) {
            ImGui::TextColored(kAssetDocumentErrorColor, "%s", error.c_str());
            return {};
        }
        ImGui::TextDisabled("%s", TEXT::Localize("asset.document.builtinReadOnly").c_str());
        if (ImGui::Button(TEXT::Localize("asset.document.createCopy").c_str()) &&
            CreateWeatherCopy(*asset, database, status_)) {
            copiedId = weather_.Guid().value;
        }
        WeatherPresetAsset view = *asset;
        ImGui::BeginDisabled();
        (void)DrawJsonFields("ui.environment.weather", view.weather, ENVIRONMENT::kWeatherFields);
        ImGui::EndDisabled();
    } else if (OpenAssetDocumentForEdit(weather_, AssetGuid{id}, database, status_)) {
        DrawAssetDocumentToolbar(weather_, database, status_);
        WeatherPresetAsset edited = weather_.Profile();
        bool changed = DrawNameField(edited.name);
        const FieldEditResult result =
            DrawJsonFields("ui.environment.weather", edited.weather, ENVIRONMENT::kWeatherFields);
        changed |= result.changed;
        if (changed) {
            (void)weather_.Apply(std::move(edited), status_, kWeatherFieldMergeGroup);
        }
        if (!result.active) {
            weather_.SealMerge();
        }
    }
    if (!status_.empty()) {
        ImGui::TextWrapped("%s", status_.c_str());
    }
    return copiedId;
}
#else
bool DrawEnvironmentAssetPicker(AssetType, const AssetDatabase&, std::string&, const char*) { return false; }
std::string EnvironmentAssetEditor::DrawProfile(const std::string&, DocumentSceneBase&) { return {}; }
std::string EnvironmentAssetEditor::DrawWeather(const std::string&, DocumentSceneBase&) { return {}; }
#endif

} // namespace HIKARI::EDITOR
