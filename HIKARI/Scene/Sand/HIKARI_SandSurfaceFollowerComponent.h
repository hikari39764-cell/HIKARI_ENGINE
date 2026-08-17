#pragma once

#include <string_view>

#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

    class SandSurfaceFollowerComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override {
            return "SandSurfaceFollowerComponent";
        }

        float GetRadius() const noexcept;
        void SetRadius(float radius);
        float GetHeightOffset() const noexcept;
        void SetHeightOffset(float offset);
        float GetSmoothingTime() const noexcept;
        void SetSmoothingTime(float seconds);

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;

        void OnDetach() override;
        void Update(float dt) override;

        // for imprint system: resolved radius (Collider/mesh sphere fit).
        float ResolveRadius() const noexcept;

    private:

        // 0 は Collider/ProceduralMesh の sphere fit から自動解決する。
        float radius_ = 0.0f;
        float heightOffset_ = 0.0f;
        float smoothingTime_ = 0.08f;
        float smoothedHeight_ = 0.0f;
        bool smoothedHeightValid_ = false;
    };

} // namespace HIKARI
