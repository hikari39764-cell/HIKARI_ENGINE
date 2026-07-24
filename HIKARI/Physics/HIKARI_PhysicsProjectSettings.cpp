#include "Physics/HIKARI_PhysicsProjectSettings.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include <json.hpp>
#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI::PHYSICS {
    namespace {

        bool IsSingleBit(uint32_t value) noexcept {
            return value != 0u && (value & (value - 1u)) == 0u;
        }

        bool NearlyEqual(float left, float right) noexcept {
            return std::abs(left - right) <= 1.0e-4f;
        }

    } // namespace

    void PhysicsProjectSettings::ResetToDefaults() {
        layers_ = {
            { "Default", 1u << 0u, 0xFFFFFFFFu },
            { "World", 1u << 1u, 0xFFFFFFFFu },
            { "Player", 1u << 2u, 0xFFFFFFFFu },
            { "Character", 1u << 3u, 0xFFFFFFFFu },
            { "Projectile", 1u << 4u, 0xFFFFFFFFu },
            { "Trigger", 1u << 5u, 0xFFFFFFFFu },
            { "Interaction", 1u << 6u, 0xFFFFFFFFu },
            { "Navigation", 1u << 7u, 0xFFFFFFFFu },
        };
        materialPresets_ = {
            { "Default", 0.5f, 0.0f, 1.0f },
            { "Ice", 0.02f, 0.0f, 0.92f },
            { "Rubber", 0.9f, 0.65f, 1.1f },
            { "Metal", 0.35f, 0.08f, 7.8f },
            { "Wood", 0.55f, 0.12f, 0.7f },
        };
    }

    bool PhysicsProjectSettings::Load(
        const std::filesystem::path& path,
        std::string& outMessage) {
        ResetToDefaults();
        std::error_code ec{};
        if (!std::filesystem::exists(path, ec)) {
            outMessage = "using built-in physics project settings";
            return true;
        }
        nlohmann::json root{};
        if (!SERIALIZATION::JSON::ReadJsonFile(
                path,
                root,
                &outMessage)) {
            return false;
        }
        if (!root.is_object()) {
            outMessage = "physics project settings JSON is invalid";
            return false;
        }

        std::vector<PhysicsCollisionLayerSetting> layers{};
        std::unordered_set<uint32_t> usedBits{};
        std::unordered_set<std::string> usedNames{};
        if (const auto found = root.find("layers");
            found != root.end() && found->is_array()) {
            for (const nlohmann::json& source : *found) {
                if (!source.is_object() || layers.size() >= 32u) {
                    continue;
                }
                PhysicsCollisionLayerSetting layer{};
                layer.name = source.value("name", std::string{});
                layer.bit = source.value("bit", 0u);
                layer.defaultMask = source.value(
                    "defaultMask",
                    0xFFFFFFFFu);
                if (layer.name.empty() || !IsSingleBit(layer.bit) ||
                    !usedNames.insert(layer.name).second ||
                    !usedBits.insert(layer.bit).second) {
                    outMessage =
                        "physics layers require unique names and single-bit values";
                    return false;
                }
                layers.push_back(std::move(layer));
            }
        }

        std::vector<PhysicsMaterialPreset> presets{};
        if (const auto found = root.find("materials");
            found != root.end() && found->is_array()) {
            for (const nlohmann::json& source : *found) {
                if (!source.is_object() || presets.size() >= 64u) {
                    continue;
                }
                PhysicsMaterialPreset preset{};
                preset.name = source.value("name", std::string{});
                preset.friction = source.value("friction", 0.5f);
                preset.restitution = source.value("restitution", 0.0f);
                preset.density = source.value("density", 1.0f);
                if (preset.name.empty() ||
                    !std::isfinite(preset.friction) ||
                    !std::isfinite(preset.restitution) ||
                    !std::isfinite(preset.density) ||
                    preset.friction < 0.0f ||
                    preset.restitution < 0.0f ||
                    preset.restitution > 1.0f ||
                    preset.density <= 0.0f) {
                    outMessage = "physics material preset is invalid";
                    return false;
                }
                presets.push_back(std::move(preset));
            }
        }
        if (!layers.empty()) {
            layers_ = std::move(layers);
        }
        if (!presets.empty()) {
            materialPresets_ = std::move(presets);
        }
        outMessage = "physics project settings loaded";
        return true;
    }

    const std::vector<PhysicsCollisionLayerSetting>&
        PhysicsProjectSettings::GetLayers() const noexcept {
        return layers_;
    }

    const std::vector<PhysicsMaterialPreset>&
        PhysicsProjectSettings::GetMaterialPresets() const noexcept {
        return materialPresets_;
    }

    int PhysicsProjectSettings::FindLayerIndex(uint32_t bit) const noexcept {
        const auto found = std::find_if(
            layers_.begin(),
            layers_.end(),
            [bit](const PhysicsCollisionLayerSetting& layer) {
                return layer.bit == bit;
            });
        return found == layers_.end()
            ? -1
            : static_cast<int>(std::distance(layers_.begin(), found));
    }

    int PhysicsProjectSettings::FindMaterialPreset(
        float friction,
        float restitution,
        float density) const noexcept {
        const auto found = std::find_if(
            materialPresets_.begin(),
            materialPresets_.end(),
            [&](const PhysicsMaterialPreset& preset) {
                return NearlyEqual(preset.friction, friction) &&
                    NearlyEqual(preset.restitution, restitution) &&
                    NearlyEqual(preset.density, density);
            });
        return found == materialPresets_.end()
            ? -1
            : static_cast<int>(std::distance(
                materialPresets_.begin(),
                found));
    }

} // namespace HIKARI::PHYSICS
