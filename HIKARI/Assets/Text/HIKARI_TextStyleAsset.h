#pragma once
#include "Core/HIKARI_AssetGuid.h"
#include "Assets/HIKARI_AssetArtifactManifest.h"
#include "Scene/Ui/HIKARI_UiTextStyle.h"
#include <filesystem>
#include <memory>
#include <unordered_map>
namespace HIKARI {
class AssetDatabase;
class IInspectorBuilder;
struct TextStyleAsset {
    std::string name = "Text Style";
    AssetGuid font;
    float pixelHeight = 48;
    GameProjectUiColor color{1, 1, 1, 1};
    UiTextStyle effects;
};
nlohmann::json WriteTextStyleAsset(const TextStyleAsset&);
bool ReadTextStyleAsset(const nlohmann::json&, TextStyleAsset&, std::string& error);
bool LoadTextStyleAsset(const std::filesystem::path&, TextStyleAsset&, std::string& error);
bool SaveTextStyleAsset(const std::filesystem::path&, const TextStyleAsset&, std::string& error);
std::vector<AssetDependencyDesc> CollectTextStyleDependencies(const TextStyleAsset&);
void InspectTextStyleAsset(IInspectorBuilder&, TextStyleAsset&);
class TextAssetStore {
public:
    void Configure(const AssetDatabase* database);
    std::shared_ptr<const TextStyleAsset> LoadStyle(const AssetGuid&, std::string& error) const;
    std::string ResolveFont(const AssetGuid&) const;
    uint64_t FontRevision(const std::filesystem::path& source) const;

private:
    void Refresh() const;
    mutable std::unordered_map<std::string, uint64_t> fonts_;
    // 描画は毎フレーム文字ごとに書体を引くので、GUID → 描画用パスを内容版数の間だけ覚える。
    mutable std::unordered_map<std::string, std::string> fontPaths_;
    const AssetDatabase* database_ = nullptr;
    mutable uint64_t revision_ = 0;
    struct Entry {
        std::shared_ptr<const TextStyleAsset> style;
        std::string error;
    };
    mutable std::unordered_map<std::string, Entry> styles_;
};
} // namespace HIKARI
