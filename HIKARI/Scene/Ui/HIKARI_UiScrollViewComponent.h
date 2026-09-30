#pragma once

#include <cstdint>
#include <string_view>

#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

enum class UiScrollAxis : uint8_t {
    Vertical,
    Horizontal,
};

struct UiScrollViewSettings {
    bool enabled = true;
    UiScrollAxis axis = UiScrollAxis::Vertical;
    float contentExtent = 0.0f;
    float scrollSpeed = 48.0f;
};

class UiScrollViewComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "UiScrollViewComponent"; }
    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
    void BuildInspector(IInspectorBuilder& builder) override;

    bool IsEnabled() const noexcept { return settings_.enabled; }
    UiScrollAxis GetAxis() const noexcept { return settings_.axis; }
    float GetContentExtent() const noexcept { return settings_.contentExtent; }
    float GetEffectiveContentExtent() const noexcept {
        return settings_.contentExtent > 0.0f ? settings_.contentExtent : resolvedContentExtent_;
    }
    float GetScrollSpeed() const noexcept { return settings_.scrollSpeed; }
    float GetScrollOffset(float viewportExtent) const noexcept;
    void SetScrollOffset(float value, float viewportExtent) noexcept;
    void SetResolvedContentExtent(float value) noexcept;
    bool ScrollBy(float delta, float viewportExtent) noexcept;

private:
    UiScrollViewSettings settings_{};
    float resolvedContentExtent_ = 0.0f;
    float scrollOffset_ = 0.0f;
};

} // namespace HIKARI
