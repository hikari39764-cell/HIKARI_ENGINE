#include "Scene/Components/Rendering/Vfx/HIKARI_GhostPossessionComponent.h"

#include <cmath>
#include <limits>
#include <utility>

namespace HIKARI {
    namespace {
        constexpr uint32_t kGhostPossessionSchemaVersion = 2u;

        bool Fail(
            std::string& outDiagnostic,
            std::string message) {

            outDiagnostic = std::move(message);
            return false;
        }

        bool ReadBool(
            const nlohmann::json& object,
            const char* key,
            bool fallback,
            bool& outValue,
            std::string& outDiagnostic) {

            const auto member = object.find(key);
            if (member == object.end()) {
                outValue = fallback;
                return true;
            }
            if (!member->is_boolean()) {
                return Fail(
                    outDiagnostic,
                    std::string("GhostPossessionComponent.") + key +
                        " must be a boolean");
            }
            outValue = member->get<bool>();
            return true;
        }

        bool ReadString(
            const nlohmann::json& object,
            const char* key,
            const std::string& fallback,
            std::string& outValue,
            std::string& outDiagnostic) {

            const auto member = object.find(key);
            if (member == object.end()) {
                outValue = fallback;
                return true;
            }
            if (!member->is_string()) {
                return Fail(
                    outDiagnostic,
                    std::string("GhostPossessionComponent.") + key +
                        " must be a string");
            }
            outValue = member->get<std::string>();
            return true;
        }

        bool ReadFloat(
            const nlohmann::json& object,
            const char* key,
            float fallback,
            float& outValue,
            std::string& outDiagnostic) {

            const auto member = object.find(key);
            if (member == object.end()) {
                outValue = fallback;
                return true;
            }
            if (!member->is_number()) {
                return Fail(
                    outDiagnostic,
                    std::string("GhostPossessionComponent.") + key +
                        " must be a finite number");
            }
            const double value = member->get<double>();
            constexpr double kMaximumFloat = static_cast<double>(
                (std::numeric_limits<float>::max)());
            if (!std::isfinite(value) || std::abs(value) > kMaximumFloat) {
                return Fail(
                    outDiagnostic,
                    std::string("GhostPossessionComponent.") + key +
                        " must be a finite float");
            }
            outValue = static_cast<float>(value);
            return true;
        }

        bool ReadSlotId(
            const nlohmann::json& object,
            const char* key,
            VfxSlotId fallback,
            VfxSlotId& outValue,
            std::string& outDiagnostic) {

            const auto member = object.find(key);
            if (member == object.end()) {
                outValue = fallback;
                return true;
            }
            if (member->is_number_unsigned()) {
                outValue.value = member->get<uint64_t>();
                return true;
            }
            if (member->is_number_integer()) {
                const int64_t value = member->get<int64_t>();
                if (value >= 0) {
                    outValue.value = static_cast<uint64_t>(value);
                    return true;
                }
            }
            return Fail(
                outDiagnostic,
                std::string("GhostPossessionComponent.") + key +
                    " must be a non-negative integer");
        }
    }

    void GhostPossessionComponent::Serialize(
        nlohmann::json& out) const {

        out = {
            { "_schemaVersion", kGhostPossessionSchemaVersion },
            { "enabled", settings_.enabled },
            { "possessed", settings_.possessed },
            { "previewInEditor", settings_.previewInEditor },
            { "materialFxProfileId", settings_.materialFxProfileId },
            { "intensity", settings_.intensity },
            { "blendInSeconds", settings_.blendInSeconds },
            { "blendOutSeconds", settings_.blendOutSeconds },
            { "rimStrength", settings_.rimStrength },
            { "rimPower", settings_.rimPower },
            { "dissolveAmount", settings_.dissolveAmount },
            { "edgeWidth", settings_.edgeWidth },
            { "pulseSpeed", settings_.pulseSpeed },
            { "edgeBoost", settings_.edgeBoost },
            { "noiseScale", settings_.noiseScale },
            { "flowSpeed", settings_.flowSpeed },
            { "flowStrength", settings_.flowStrength },
            { "distortionStrength", settings_.distortionStrength },
            { "sceneColorMix", settings_.sceneColorMix },
            { "sceneColorNoiseScale", settings_.sceneColorNoiseScale },
            { "enterSlotId", settings_.enterSlotId.value },
            { "loopSlotId", settings_.loopSlotId.value },
            { "exitSlotId", settings_.exitSlotId.value },
        };
    }

    void GhostPossessionComponent::Deserialize(
        const nlohmann::json& in) {

        std::string ignoredDiagnostic{};
        (void)TryDeserialize(in, ignoredDiagnostic);
    }

    bool GhostPossessionComponent::TryDeserialize(
        const nlohmann::json& in,
        std::string& outDiagnostic) {

        try {
            if (!in.is_object()) {
                return Fail(
                    outDiagnostic,
                    "GhostPossessionComponent must be a JSON object");
            }

            const GhostPossessionSettings defaults{};
            GhostPossessionSettings decoded = defaults;
            const uint32_t schemaVersion = in.value(
                "_schemaVersion",
                kGhostPossessionSchemaVersion);
            if (schemaVersion > kGhostPossessionSchemaVersion) {
                return Fail(
                    outDiagnostic,
                    "GhostPossessionComponent schema version is newer than this runtime");
            }

            if (!ReadBool(
                    in,
                    "enabled",
                    defaults.enabled,
                    decoded.enabled,
                    outDiagnostic) ||
                !ReadBool(
                    in,
                    "possessed",
                    defaults.possessed,
                    decoded.possessed,
                    outDiagnostic) ||
                !ReadBool(
                    in,
                    "previewInEditor",
                    defaults.previewInEditor,
                    decoded.previewInEditor,
                    outDiagnostic) ||
                !ReadString(
                    in,
                    "materialFxProfileId",
                    defaults.materialFxProfileId,
                    decoded.materialFxProfileId,
                    outDiagnostic) ||
                !ReadFloat(
                    in,
                    "intensity",
                    defaults.intensity,
                    decoded.intensity,
                    outDiagnostic) ||
                !ReadFloat(
                    in,
                    "blendInSeconds",
                    defaults.blendInSeconds,
                    decoded.blendInSeconds,
                    outDiagnostic) ||
                !ReadFloat(
                    in,
                    "blendOutSeconds",
                    defaults.blendOutSeconds,
                    decoded.blendOutSeconds,
                    outDiagnostic) ||
                !ReadFloat(
                    in,
                    "rimStrength",
                    defaults.rimStrength,
                    decoded.rimStrength,
                    outDiagnostic) ||
                !ReadFloat(
                    in,
                    "rimPower",
                    defaults.rimPower,
                    decoded.rimPower,
                    outDiagnostic) ||
                !ReadFloat(
                    in,
                    "dissolveAmount",
                    defaults.dissolveAmount,
                    decoded.dissolveAmount,
                    outDiagnostic) ||
                !ReadFloat(
                    in,
                    "edgeWidth",
                    defaults.edgeWidth,
                    decoded.edgeWidth,
                    outDiagnostic) ||
                !ReadFloat(
                    in,
                    "pulseSpeed",
                    defaults.pulseSpeed,
                    decoded.pulseSpeed,
                    outDiagnostic) ||
                !ReadFloat(
                    in,
                    "edgeBoost",
                    defaults.edgeBoost,
                    decoded.edgeBoost,
                    outDiagnostic) ||
                !ReadFloat(
                    in,
                    "noiseScale",
                    defaults.noiseScale,
                    decoded.noiseScale,
                    outDiagnostic) ||
                !ReadFloat(
                    in,
                    "flowSpeed",
                    defaults.flowSpeed,
                    decoded.flowSpeed,
                    outDiagnostic) ||
                !ReadFloat(
                    in,
                    "flowStrength",
                    defaults.flowStrength,
                    decoded.flowStrength,
                    outDiagnostic) ||
                !ReadFloat(
                    in,
                    "distortionStrength",
                    defaults.distortionStrength,
                    decoded.distortionStrength,
                    outDiagnostic) ||
                !ReadFloat(
                    in,
                    "sceneColorMix",
                    defaults.sceneColorMix,
                    decoded.sceneColorMix,
                    outDiagnostic) ||
                !ReadFloat(
                    in,
                    "sceneColorNoiseScale",
                    defaults.sceneColorNoiseScale,
                    decoded.sceneColorNoiseScale,
                    outDiagnostic) ||
                !ReadSlotId(
                    in,
                    "enterSlotId",
                    defaults.enterSlotId,
                    decoded.enterSlotId,
                    outDiagnostic) ||
                !ReadSlotId(
                    in,
                    "loopSlotId",
                    defaults.loopSlotId,
                    decoded.loopSlotId,
                    outDiagnostic) ||
                !ReadSlotId(
                    in,
                    "exitSlotId",
                    defaults.exitSlotId,
                    decoded.exitSlotId,
                    outDiagnostic)) {
                return false;
            }

            settings_ = SanitizeGhostPossessionSettings(
                std::move(decoded));
            outDiagnostic.clear();
            return true;
        } catch (const nlohmann::json::exception& exception) {
            return Fail(
                outDiagnostic,
                std::string("GhostPossessionComponent JSON decode failed: ") +
                    exception.what());
        }
    }

} // namespace HIKARI
