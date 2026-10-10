#include "Assets/Text/HIKARI_TextStyleAsset.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace HIKARI {
nlohmann::json WriteTextStyleAsset(const TextStyleAsset& style) {
    nlohmann::json data;
    style.effects.Write(data);
    // 内容の表示率と折り返しは利用側の責務。
    data.erase("reveal");
    data.erase("wrap");
    data["pixelHeight"] = style.pixelHeight;
    data["color"] = {style.color.r, style.color.g, style.color.b, style.color.a};
    return {{"version", 1}, {"name", style.name}, {"fontAssetGuid", style.font.value}, {"appearance", data}};
}
bool ReadTextStyleAsset(const nlohmann::json& root, TextStyleAsset& out, std::string& error) {
    try {
        if (!root.is_object() || root.value("version", 0) != 1 || !root.contains("appearance") ||
            !root.at("appearance").is_object()) {
            error = "Invalid text style or unsupported version.";
            return false;
        }
        TextStyleAsset value;
        value.name = root.value("name", std::string("Text Style"));
        value.font.value = root.value("fontAssetGuid", std::string{});
        const auto& a = root.at("appearance");
        value.pixelHeight = a.value("pixelHeight", 48.f);
        if (!std::isfinite(value.pixelHeight) || value.pixelHeight < 4 || value.pixelHeight > 512 ||
            value.name.size() > 256) {
            error = "Text style size or name is out of range.";
            return false;
        }
        const auto c = a.value("color", std::array<float, 4>{1, 1, 1, 1});
        for (float v : c)
            if (!std::isfinite(v) || v < 0 || v > 16) {
                error = "Invalid text color.";
                return false;
            }
        value.color = {c[0], c[1], c[2], std::clamp(c[3], 0.f, 1.f)};
        value.effects.Read(a);
        out = std::move(value);
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
bool LoadTextStyleAsset(const std::filesystem::path& path, TextStyleAsset& value, std::string& error) {
    nlohmann::json data;
    return SERIALIZATION::JSON::ReadJsonFile(path, data, &error) && ReadTextStyleAsset(data, value, error);
}
bool SaveTextStyleAsset(const std::filesystem::path& path, const TextStyleAsset& value, std::string& error) {
    TextStyleAsset checked;
    const auto data = WriteTextStyleAsset(value);
    return ReadTextStyleAsset(data, checked, error) && SERIALIZATION::JSON::WriteJsonFile(path, data, &error);
}
std::vector<AssetDependencyDesc> CollectTextStyleDependencies(const TextStyleAsset& style) {
    return style.font.IsValid() ? std::vector<AssetDependencyDesc>{{style.font, {}, "TextStyle.Font"}}
                                : std::vector<AssetDependencyDesc>{};
}
void InspectTextStyleAsset(IInspectorBuilder& b, TextStyleAsset& value) {
    const auto L = [](const char* key) { return TEXT::Localize(key).c_str(); };
    b.String(AuthoringFieldDescriptor{"name", L("textstyle.name")}, value.name);
    b.AssetIdPicker(AuthoringFieldDescriptor{"fontAssetGuid", L("textstyle.font")}, AssetType::Font, value.font.value);
    b.FloatRange(AuthoringFieldDescriptor{"pixelHeight", L("Pixel Height")}, value.pixelHeight, 4, 512, 1);
    b.Color3(AuthoringFieldDescriptor{"color", L("Color")}, value.color.r, value.color.g, value.color.b);
    b.FloatRange(AuthoringFieldDescriptor{.id = "alpha", .label = L("uix.opacity"), .slider = true}, value.color.a, 0,
                 1);
    value.effects.Inspect(b, false);
}
void TextAssetStore::Configure(const AssetDatabase* database) {
    if (database_ == database)
        return;
    database_ = database;
    revision_ = 0;
    styles_.clear();
    fonts_.clear();
}
void TextAssetStore::Refresh() const {
    if (database_ && revision_ != database_->GetContentRevision()) {
        revision_ = database_->GetContentRevision();
        styles_.clear();
        fonts_.clear();
    }
}
uint64_t TextAssetStore::FontRevision(const std::filesystem::path& source) const {
    if (!database_)
        return 0;
    Refresh();
    const auto key = TEXT::PathToUtf8(source.lexically_normal());
    if (const auto it = fonts_.find(key); it != fonts_.end())
        return it->second;
    uint64_t hash = 0;
    if (const auto* record = database_->FindByPath(source);
        record && record->type == AssetType::Font && record->lastImportSucceeded) {
        const auto data = nlohmann::json::parse(record->artifactManifest.diagnosticsJson, nullptr, false);
        if (data.is_object() && data.contains("contentHash") && data["contentHash"].is_number_unsigned())
            hash = data["contentHash"].get<uint64_t>();
    }
    fonts_.emplace(key, hash);
    return hash;
}
std::shared_ptr<const TextStyleAsset> TextAssetStore::LoadStyle(const AssetGuid& guid, std::string& error) const {
    if (!database_ || !guid.IsValid()) {
        error = "No text style is bound.";
        return {};
    }
    Refresh();
    if (const auto it = styles_.find(guid.value); it != styles_.end()) {
        error = it->second.error;
        return it->second.style;
    }
    Entry entry;
    const auto* record = database_->FindByGuid(guid);
    if (!record || record->type != AssetType::TextStyle)
        entry.error = "Text style asset is missing: " + guid.value;
    else {
        auto value = std::make_shared<TextStyleAsset>();
        const auto path =
            record->sourcePath.is_absolute() ? record->sourcePath : database_->GetProjectRoot() / record->sourcePath;
        if (LoadTextStyleAsset(path, *value, entry.error))
            entry.style = std::move(value);
    }
    error = entry.error;
    auto result = entry.style;
    styles_.emplace(guid.value, std::move(entry));
    return result;
}
std::string TextAssetStore::ResolveFont(const AssetGuid& guid) const {
    const auto* record = database_ ? database_->FindByGuid(guid) : nullptr;
    if (!record || record->type != AssetType::Font || !record->lastImportSucceeded)
        return {};
    const auto path =
        record->sourcePath.is_absolute() ? record->sourcePath : database_->GetProjectRoot() / record->sourcePath;
    // 参照先は GUID で追跡し、描画 API には Assets 相対パスを渡す。
    auto relative = path.lexically_relative(database_->GetAssetsRoot());
    if (relative.empty() || *relative.begin() == "..")
        relative = path.lexically_relative(database_->GetProjectRoot() / "RuntimePayload/Assets");
    if (relative.empty() || *relative.begin() == "..")
        return {};
    return TEXT::PathToUtf8(relative);
}
} // namespace HIKARI
