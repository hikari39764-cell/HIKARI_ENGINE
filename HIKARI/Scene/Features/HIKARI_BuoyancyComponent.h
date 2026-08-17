#pragma once

#include <algorithm>

#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

    // 浮力マーカー。球状の排水体積から浮力を BuoyancySystem が適用する。
    class BuoyancyComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "BuoyancyComponent";
        }

        float GetRadius() const noexcept {
            return radius_;
        }

        float GetDensity() const noexcept {
            return density_;
        }

        void SetRadius(float radius) noexcept {
            radius_ = (std::max)(radius, 0.01f);
        }

        void SetDensity(float density) noexcept {
            density_ = (std::max)(density, 0.0f);
        }

    private:
        float radius_ = 0.5f;
        float density_ = 1000.0f;
    };

} // namespace HIKARI
