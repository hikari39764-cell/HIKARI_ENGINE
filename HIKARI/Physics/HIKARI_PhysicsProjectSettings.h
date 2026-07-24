#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace HIKARI::PHYSICS {

    struct PhysicsCollisionLayerSetting {
        std::string name{};
        uint32_t bit = 1u;
        uint32_t defaultMask = 0xFFFFFFFFu;
    };

    struct PhysicsMaterialPreset {
        std::string name{};
        float friction = 0.5f;
        float restitution = 0.0f;
        float density = 1.0f;
    };

    // Project-level authoring vocabulary. Runtime collision remains driven by
    // the resolved numeric layer, mask, and material values on each collider.
    class PhysicsProjectSettings {
    public:
        void ResetToDefaults();
        bool Load(
            const std::filesystem::path& path,
            std::string& outMessage);

        const std::vector<PhysicsCollisionLayerSetting>& GetLayers()
            const noexcept;
        const std::vector<PhysicsMaterialPreset>& GetMaterialPresets()
            const noexcept;
        int FindLayerIndex(uint32_t bit) const noexcept;
        int FindMaterialPreset(
            float friction,
            float restitution,
            float density) const noexcept;

    private:
        std::vector<PhysicsCollisionLayerSetting> layers_{};
        std::vector<PhysicsMaterialPreset> materialPresets_{};
    };

} // namespace HIKARI::PHYSICS
