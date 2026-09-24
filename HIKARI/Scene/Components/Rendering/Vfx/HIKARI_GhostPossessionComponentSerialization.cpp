#include "Scene/Components/Rendering/Vfx/HIKARI_GhostPossessionComponent.h"

#include <type_traits>
#include <utility>

#include "Core/Serialization/Json/HIKARI_JsonStrict.h"

namespace HIKARI {
namespace {
constexpr uint32_t kGhostPossessionSchemaVersion = 2u;
constexpr std::string_view kPath = "GhostPossessionComponent";

// 保存順と読み込み（診断）順を兼ねる
constexpr auto kGhostPossessionFields = [](auto&& field) {
    using S = GhostPossessionSettings;
    field("enabled", &S::enabled);
    field("possessed", &S::possessed);
    field("previewInEditor", &S::previewInEditor);
    field("materialFxProfileId", &S::materialFxProfileId);
    field("intensity", &S::intensity);
    field("blendInSeconds", &S::blendInSeconds);
    field("blendOutSeconds", &S::blendOutSeconds);
    field("rimStrength", &S::rimStrength);
    field("rimPower", &S::rimPower);
    field("dissolveAmount", &S::dissolveAmount);
    field("edgeWidth", &S::edgeWidth);
    field("pulseSpeed", &S::pulseSpeed);
    field("edgeBoost", &S::edgeBoost);
    field("noiseScale", &S::noiseScale);
    field("flowSpeed", &S::flowSpeed);
    field("flowStrength", &S::flowStrength);
    field("distortionStrength", &S::distortionStrength);
    field("sceneColorMix", &S::sceneColorMix);
    field("sceneColorNoiseScale", &S::sceneColorNoiseScale);
    field("enterSlotId", &S::enterSlotId);
    field("loopSlotId", &S::loopSlotId);
    field("exitSlotId", &S::exitSlotId);
};

template <class T>
bool ReadField(const nlohmann::json& in, const char* key, const T& fallback, T& outValue, std::string& outDiagnostic) {
    if constexpr (std::is_same_v<T, bool>) {
        return JSONSTRICT::ReadBool(in, key, fallback, outValue, outDiagnostic, kPath);
    } else if constexpr (std::is_same_v<T, float>) {
        return JSONSTRICT::ReadFloat(in, key, fallback, outValue, outDiagnostic, kPath);
    } else if constexpr (std::is_same_v<T, std::string>) {
        return JSONSTRICT::ReadString(in, key, fallback, outValue, outDiagnostic, kPath);
    } else {
        return JSONSTRICT::ReadUint64(in, key, fallback.value, outValue.value, outDiagnostic, kPath);
    }
}
} // namespace

void GhostPossessionComponent::Serialize(nlohmann::json& out) const {
    out = {{"_schemaVersion", kGhostPossessionSchemaVersion}};
    kGhostPossessionFields([&](const char* key, auto member) {
        const auto& value = settings_.*member;
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, VfxSlotId>) {
            out[key] = value.value;
        } else {
            out[key] = value;
        }
    });
}

void GhostPossessionComponent::Deserialize(const nlohmann::json& in) {
    std::string ignoredDiagnostic{};
    (void)TryDeserialize(in, ignoredDiagnostic);
}

bool GhostPossessionComponent::TryDeserialize(const nlohmann::json& in, std::string& outDiagnostic) {
    try {
        if (!in.is_object()) {
            return JSONSTRICT::Fail(outDiagnostic, "GhostPossessionComponent must be a JSON object");
        }
        const uint32_t schemaVersion = in.value("_schemaVersion", kGhostPossessionSchemaVersion);
        if (schemaVersion > kGhostPossessionSchemaVersion) {
            return JSONSTRICT::Fail(outDiagnostic,
                                    "GhostPossessionComponent schema version is newer than this runtime");
        }

        const GhostPossessionSettings defaults{};
        GhostPossessionSettings decoded = defaults;
        bool ok = true;
        kGhostPossessionFields([&](const char* key, auto member) {
            ok = ok && ReadField(in, key, defaults.*member, decoded.*member, outDiagnostic);
        });
        if (!ok) {
            return false;
        }

        settings_ = SanitizeGhostPossessionSettings(std::move(decoded));
        outDiagnostic.clear();
        return true;
    } catch (const nlohmann::json::exception& exception) {
        return JSONSTRICT::Fail(outDiagnostic,
                                std::string("GhostPossessionComponent JSON decode failed: ") + exception.what());
    }
}

} // namespace HIKARI
