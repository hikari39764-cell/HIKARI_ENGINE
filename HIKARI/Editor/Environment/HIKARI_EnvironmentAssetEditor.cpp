#include "Editor/Environment/HIKARI_EnvironmentAssetEditor.h"

#include <memory>
#include <span>
#include <vector>
#include <type_traits>
#include <utility>

#include "Assets/Environment/HIKARI_EnvironmentAssetStore.h"
#include "Assets/Environment/HIKARI_EnvironmentFields.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Environment/HIKARI_CelestialModel.h"

#include "Editor/Documents/HIKARI_ValidatedAssetDocumentUi.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"
#include "Editor/Widgets/HIKARI_JsonFieldsEditor.h"
#include "Editor/Widgets/HIKARI_NumberField.h"
#include "imgui.h"

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
        } else if constexpr (std::is_same_v<T, ShadowSettings>) {
            if (key.starts_with("contactShadow") && key != "contactShadows") {
                return !object.contactShadows;
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
        // 内蔵は読み取り専用。最初の編集でその値ごとプロジェクトへ複製し、シーンを複製へ切り替える。
        ImGui::TextDisabled("%s", TEXT::Localize("ui.environment.lightingControlsCopyHint").c_str());
        DrawAssetDocumentStatus(status_);
        EnvironmentProfileAsset edited = *asset;
        if (DrawProfileSections(edited, scene, true).changed && profile_.SaveAsNew(edited, database, status_)) {
            copiedId = profile_.Guid().value;
        }
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
    return copiedId;
}

namespace {
// 布景でよく触る項目。プロファイルの表から節ごとに選ぶ（範囲と表示名は全項目の表と同じ）。
bool IsLightingControlField(std::string_view section, std::string_view key) {
    if (section == "sun") {
        return key == "mode" || key == "fixedAzimuthDegrees" || key == "fixedElevationDegrees" ||
               key == "illuminance" || key == "color";
    }
    if (section == "sky") {
        return key == "lightIntensity";
    }
    if (section == "shadows") {
        // strength は様式化した陰影だけに効くので高级へ。
        return key == "contactShadows" || key == "contactShadowLength" || key == "contactShadowStrength";
    }
    if (section == "ambientOcclusion") {
        return key == "enabled" || key == "mode" || key == "radius" || key == "strength" || key == "power";
    }
    if (section == "screenSpaceReflections") {
        // 光線追跡の反射（画質設定）もこの節の強さと粗さの上限に従う。
        return key == "enabled" || key == "quality" || key == "intensity" || key == "maxRoughness";
    }
    if (section == "fog") {
        return key == "enabled" || key == "density" || key == "heightFalloff" || key == "baseHeight" || key == "albedo";
    }
    if (section == "exposure") {
        return key == "mode" || key == "compensationEv" || key == "manualEv100";
    }
    if (section == "clouds") {
        return key == "enabled" || key == "coverageBias" || key == "baseAltitude" || key == "shadowStrength";
    }
    if (section == "bloom") {
        return key == "enabled" || key == "intensity" || key == "threshold" || key == "radius";
    }
    if (section == "toneMapping") {
        return key == "curve" || key == "whiteBalanceTemperature" || key == "whiteBalanceTint" || key == "contrast" ||
               key == "saturation";
    }
    if (section == "colorGrading") {
        return key == "shadowsTint" || key == "midtonesTint" || key == "highlightsTint" || key == "lut" ||
               key == "lutIntensity";
    }
    return false;
}

// 組（光・大気・画づくり）と、その中の節の順。
struct LightingControlGroup {
    const char* labelKey;
    std::span<const std::string_view> sections;
};
constexpr std::string_view kLightSections[] = {"sun", "sky", "shadows"};
constexpr std::string_view kDetailSections[] = {"ambientOcclusion", "screenSpaceReflections"};
constexpr std::string_view kAtmosphereSections[] = {"fog", "clouds"};
constexpr std::string_view kImageSections[] = {"exposure", "bloom", "toneMapping", "colorGrading"};
constexpr LightingControlGroup kLightingControlGroups[] = {
    {"ui.environment.lightingGroup.light", kLightSections},
    {"ui.environment.lightingGroup.detail", kDetailSections},
    {"ui.environment.lightingGroup.atmosphere", kAtmosphereSections},
    {"ui.environment.lightingGroup.image", kImageSections},
};

// common なら布景でよく触る項目だけ、そうでなければ残りの項目（「高級」の中）を描く。
struct LightingControlFields {
    const ProfileCustomFields& profile;
    std::string_view section;
    bool common = true;

    template <class T>
    bool operator()(std::string_view key, T& object, FieldEditResult& result) const {
        if constexpr (std::is_same_v<T, SunSettings>) {
            if (key == "mode") {
                return true; // 窓の上の「光照模式」で切り替える。
            }
            if (key == "illuminance") {
                if (!common) {
                    return profile(key, object, result); // 高级には物理量（lux）のまま出す。
                }
                // 常用は既定の太陽（晴天の昼）に対する倍率。
                static const double kReference = SunSettings{}.illuminance;
                InspectorPropertyRow row(TEXT::Localize("ui.environment.sun.brightness"));
                if (row.IsVisible()) {
                    double scale = object.illuminance / kReference;
                    constexpr double kDefaultScale = 1.0;
                    if (DragNumber("##brightness", scale, {.minimum = 0.0, .maximum = 4.0, .step = 0.01},
                                   &kDefaultScale)) {
                        object.illuminance = static_cast<float>(scale * kReference);
                        result.changed = true;
                    }
                    result.active |= ImGui::IsItemActive();
                }
                return true;
            }
        }
        if (IsLightingControlField(section, key) != common) {
            return true;
        }
        if (!common) {
            return profile(key, object, result);
        }
        if constexpr (std::is_same_v<T, RENDER3D::COLOR::ExposureSettings>) {
            // 手動の時は EV100、自動の時は補正だけを出す。
            const bool manual = object.mode == RENDER3D::COLOR::ExposureMode::Manual;
            if ((key == "manualEv100" && !manual) || (key == "compensationEv" && manual)) {
                return true;
            }
        }
        return profile(key, object, result);
    }
};

// 太陽を「日付と地点」から「固定」へ切り替えた時、固定の向きを今の太陽の位置から始める（光が跳ばない）。
void SeedFixedSunFromCurrent(const SunSettings& before, SunSettings& after, DocumentSceneBase& scene) {
    if (before.mode == after.mode || after.mode != SunMode::Fixed) {
        return;
    }
    const ENVIRONMENT::EnvironmentSimulation& simulation = scene.GetEnvironmentRuntime().Simulation();
    ENVIRONMENT::WorldDirectionToHorizon(simulation.GetFrameState().celestial.sunDirection,
                                         simulation.GetSnapshot().location.northYawDegrees, after.fixedAzimuthDegrees,
                                         after.fixedElevationDegrees);
}

FieldEditResult DrawLightingControlSections(EnvironmentProfileAsset& asset, DocumentSceneBase& scene) {
    FieldEditResult total{};
    const ProfileCustomFields profileFields{scene.GetAssetDatabase()};
    const SunSettings sunBefore = asset.profile.sun;
    for (const LightingControlGroup& group : kLightingControlGroups) {
        if (!ImGui::TreeNodeEx(TEXT::Localize(group.labelKey).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            continue;
        }
        for (const std::string_view section : group.sections) {
            ENVIRONMENT::FIELDS::VisitProfileSections([&](const char* key, auto member, const auto& fields) {
                if (section != key) {
                    return;
                }
                ImGui::SeparatorText(ResolveFieldLabel("ui.environment.section", key).c_str());
                ImGui::PushID(key);
                const std::string labelPrefix = std::string("ui.environment.") + key;
                FieldEditResult result = DrawJsonFields(labelPrefix, asset.profile.*member, fields,
                                                        LightingControlFields{profileFields, section, true});
                if (BeginAdvancedSection("advanced")) {
                    const FieldEditResult advanced =
                        DrawJsonFields(labelPrefix, asset.profile.*member, fields,
                                       LightingControlFields{profileFields, section, false});
                    result.changed |= advanced.changed;
                    result.active |= advanced.active;
                    EndAdvancedSection();
                }
                ImGui::PopID();
                total.changed |= result.changed;
                total.active |= result.active;
            });
        }
        ImGui::TreePop();
    }
    SeedFixedSunFromCurrent(sunBefore, asset.profile.sun, scene);
    return total;
}
} // namespace

std::string EnvironmentAssetEditor::EditProfile(const std::string& id, DocumentSceneBase& scene,
                                                const std::function<bool(EnvironmentProfileAsset&)>& edit,
                                                bool continuing) {
    AssetDatabase& database = scene.GetAssetDatabase();
    if (id.starts_with(ENVIRONMENT::kBuiltinEnvironmentPrefix)) {
        std::string error;
        const auto asset = scene.GetEnvironmentAssets().LoadProfile(id, error);
        if (!asset) {
            status_ = error;
            return {};
        }
        EnvironmentProfileAsset edited = *asset;
        if (!edit(edited) || !profile_.SaveAsNew(edited, database, status_)) {
            return {};
        }
        return profile_.Guid().value;
    }
    if (!OpenAssetDocumentForEdit(profile_, AssetGuid{id}, database, status_)) {
        return {};
    }
    EnvironmentProfileAsset edited = profile_.Profile();
    if (edit(edited)) {
        (void)profile_.Apply(std::move(edited), status_, continuing ? kProfileFieldMergeGroup : 0u);
    }
    if (!continuing) {
        profile_.SealMerge();
    }
    return {};
}

std::string EnvironmentAssetEditor::SetSunMode(const std::string& id, SunMode mode, DocumentSceneBase& scene) {
    return EditProfile(
        id, scene,
        [&](EnvironmentProfileAsset& edited) {
            if (edited.profile.sun.mode == mode) {
                return false;
            }
            const SunSettings before = edited.profile.sun;
            edited.profile.sun.mode = mode;
            SeedFixedSunFromCurrent(before, edited.profile.sun, scene);
            return true;
        },
        false);
}

bool DrawEnvironmentProfilePresets(const AssetDatabase& database, std::string& id) {
    struct Preset {
        std::string id;
        std::string label;
    };
    std::vector<Preset> presets;
    for (const ENVIRONMENT::BuiltinEnvironmentContent& content : ENVIRONMENT::GetBuiltinEnvironmentProfiles()) {
        // 内蔵の名前は翻訳があれば使う（ui.lookDev.preset.<fileStem>）。
        const std::string key = "ui.lookDev.preset." + std::string(content.fileStem);
        const std::string& localized = TEXT::Localize(key);
        presets.push_back({std::string(content.id), localized != key ? localized : std::string(content.displayName)});
    }
    for (const AssetRecord* record : database.CollectByType(AssetType::Environment)) {
        presets.push_back({record->guid.value, GetAssetRecordDisplayName(*record)});
    }
    bool changed = false;
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    for (size_t index = 0; index < presets.size(); ++index) {
        const Preset& preset = presets[index];
        const float width = ImGui::CalcTextSize(preset.label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        if (index > 0u) {
            ImGui::SameLine();
            if (ImGui::GetCursorScreenPos().x + width > right) {
                ImGui::NewLine();
            }
        }
        ImGui::PushID(static_cast<int>(index));
        if (ToggleButton(preset.label.c_str(), "##preset", preset.id == id) && preset.id != id) {
            id = preset.id;
            changed = true;
        }
        ImGui::PopID();
    }
    return changed;
}

std::string EnvironmentAssetEditor::DrawLightingControls(const std::string& id, DocumentSceneBase& scene) {
    AssetDatabase& database = scene.GetAssetDatabase();
    std::string copiedId;
    ImGui::PushID("lightingControls");
    if (id.starts_with(ENVIRONMENT::kBuiltinEnvironmentPrefix)) {
        // 内蔵のプロファイルは、最初の編集でその値ごとプロジェクトへ複製し、シーンを複製へ切り替える。
        std::string error;
        if (const auto asset = scene.GetEnvironmentAssets().LoadProfile(id, error)) {
            ImGui::TextDisabled("%s", TEXT::Localize("ui.environment.lightingControlsCopyHint").c_str());
            DrawAssetDocumentStatus(status_);
            EnvironmentProfileAsset edited = *asset;
            if (DrawLightingControlSections(edited, scene).changed && profile_.SaveAsNew(edited, database, status_)) {
                copiedId = profile_.Guid().value;
            }
        } else {
            ImGui::TextColored(kAssetDocumentErrorColor, "%s", error.c_str());
        }
    } else if (OpenAssetDocumentForEdit(profile_, AssetGuid{id}, database, status_)) {
        DrawAssetDocumentToolbar(profile_, database, status_);
        EnvironmentProfileAsset edited = profile_.Profile();
        const FieldEditResult result = DrawLightingControlSections(edited, scene);
        if (result.changed) {
            (void)profile_.Apply(std::move(edited), status_, kProfileFieldMergeGroup);
        }
        if (!result.active) {
            profile_.SealMerge();
        }
    }
    ImGui::PopID();
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
        ImGui::TextDisabled("%s", TEXT::Localize("asset.document.builtinCopyOnEdit").c_str());
        DrawAssetDocumentStatus(status_);
        WeatherPresetAsset edited = *asset;
        if (DrawJsonFields("ui.environment.weather", edited.weather, ENVIRONMENT::kWeatherFields).changed &&
            weather_.SaveAsNew(edited, database, status_)) {
            copiedId = weather_.Guid().value;
        }
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
    return copiedId;
}

} // namespace HIKARI::EDITOR
