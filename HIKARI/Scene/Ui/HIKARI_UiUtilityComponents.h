#pragma once

#include "Render3D/HIKARI_Math3D.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Ui/HIKARI_UiDataBindingComponent.h"
#include <string>

namespace HIKARI {
namespace BINDING {
class RuntimeBindingStore;
}

// 表示・入力の継承は UI レイアウトと同じ階層で評価する。
class UiGroupComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "UiGroupComponent"; }
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void BuildInspector(IInspectorBuilder& builder) override;
    bool IsVisible() const noexcept { return visible_; }
    bool IsInteractable() const noexcept { return interactable_; }
    bool IsModal() const noexcept { return modal_; }
    float GetOpacity() const noexcept { return opacity_; }
    void SetVisible(bool value) noexcept { visible_ = value; }
    void SetOpacity(float value) noexcept;

private:
    bool visible_ = true, interactable_ = true;
    bool modal_ = false;
    float opacity_ = 1;
};

enum class UiLayoutDirection : uint8_t { Horizontal, Vertical, Grid };
class UiLayoutGroupComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "UiLayoutGroupComponent"; }
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void BuildInspector(IInspectorBuilder& builder) override;
    bool IsEnabled() const noexcept { return enabled_; }
    UiLayoutDirection GetDirection() const noexcept { return direction_; }
    MATH::Vec2 GetCellSize() const noexcept { return cellSize_; }
    float GetSpacing() const noexcept { return spacing_; }
    float GetPadding() const noexcept { return padding_; }
    int GetColumns() const noexcept { return columns_; }

private:
    bool enabled_ = true;
    UiLayoutDirection direction_ = UiLayoutDirection::Vertical;
    MATH::Vec2 cellSize_{240, 56};
    float spacing_ = 12, padding_ = 16;
    int columns_ = 3;
};

enum class UiFillDirection : uint8_t { Right, Left, Up, Down };
class UiProgressComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "UiProgressComponent"; }
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void BuildInspector(IInspectorBuilder& builder) override;
    bool IsEnabled() const noexcept { return enabled_; }
    UiFillDirection GetDirection() const noexcept { return direction_; }
    float ResolveFraction(const BINDING::RuntimeBindingStore* bindings,
                          UiBindingReadMode mode = UiBindingReadMode::Runtime) const noexcept;
    void SetValue(float value) noexcept;

private:
    bool enabled_ = true;
    float value_ = .75f, minimum_ = 0, maximum_ = 1;
    UiFillDirection direction_ = UiFillDirection::Right;
    std::string variableId_;
};
} // namespace HIKARI
