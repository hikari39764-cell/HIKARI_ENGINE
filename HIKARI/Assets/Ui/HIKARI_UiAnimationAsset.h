#pragma once
#include "Assets/HIKARI_AssetGuid.h"
#include "Core/Animation/HIKARI_ScalarCurve.h"
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <json.hpp>

namespace HIKARI {
class AssetDatabase;
enum class UiMotionProperty : uint8_t {
    OffsetX,
    OffsetY,
    ScaleX,
    ScaleY,
    Rotation,
    Opacity,
    Red,
    Green,
    Blue,
    Reveal,
    Count
};
struct UiMotionPropertyDescriptor {
    UiMotionProperty property;
    const char *id, *label;
    float initial, minimum, maximum;
};
std::span<const UiMotionPropertyDescriptor> UiMotionProperties();
const UiMotionPropertyDescriptor* FindUiMotionProperty(std::string_view id);
enum class UiMotionTrigger : uint8_t { Manual, Open, Close, Normal, Hover, Press, Disabled, Count };
std::span<const char* const> UiMotionTriggerNames();
struct UiAnimationTrack {
    uint64_t id = 1;
    std::string slot = "self";
    UiMotionProperty property = UiMotionProperty::Opacity;
    ScalarCurve curve{1};
};
struct UiAnimationClip {
    uint64_t id = 1;
    std::string name = "Open";
    UiMotionTrigger trigger = UiMotionTrigger::Manual;
    float duration = .3f;
    bool loop = false;
    std::vector<UiAnimationTrack> tracks;
};
struct UiAnimationAsset {
    std::string name = "UI Animation";
    std::vector<UiAnimationClip> clips;
};
nlohmann::json WriteUiAnimationAsset(const UiAnimationAsset&);
bool ReadUiAnimationAsset(const nlohmann::json&, UiAnimationAsset&, std::string& error);
bool LoadUiAnimationAsset(const std::filesystem::path&, UiAnimationAsset&, std::string& error);
bool SaveUiAnimationAsset(const std::filesystem::path&, const UiAnimationAsset&, std::string& error);
UiAnimationAsset MakeUiAnimationPreset(std::string_view preset);

class UiAnimationAssetStore {
public:
    void Configure(const AssetDatabase* database);
    std::shared_ptr<const UiAnimationAsset> Load(const AssetGuid&, std::string& error);

private:
    const AssetDatabase* database_ = nullptr;
    uint64_t revision_ = ~uint64_t(0);
    struct Entry {
        std::shared_ptr<const UiAnimationAsset> asset;
        std::string error;
    };
    std::unordered_map<std::string, Entry> cache_;
};
} // namespace HIKARI
