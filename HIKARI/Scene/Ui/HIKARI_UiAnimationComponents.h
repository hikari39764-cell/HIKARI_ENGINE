#pragma once
#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {
class UiAnimatorComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "UiAnimatorComponent"; }
    void Serialize(nlohmann::json&) const override;
    void Deserialize(const nlohmann::json&) override;
    void BuildInspector(IInspectorBuilder&) override;
    const AssetGuid& GetAnimation() const { return animation_; }
    bool IsEnabled() const { return enabled_; }
    bool IsUnscaled() const { return unscaled_; }
    float GetSpeed() const { return speed_; }

private:
    AssetGuid animation_;
    bool enabled_ = true, unscaled_ = true;
    float speed_ = 1;
};

// スロットは Animator の子階層内だけで解決する。Prefab 複製で ID が衝突しない。
class UiAnimationTargetComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "UiAnimationTargetComponent"; }
    void Serialize(nlohmann::json&) const override;
    void Deserialize(const nlohmann::json&) override;
    void BuildInspector(IInspectorBuilder&) override;
    const std::string& GetSlot() const { return slot_; }

private:
    std::string slot_;
};
} // namespace HIKARI
