#include "Scene/Serialization/Lighting/HIKARI_SceneLightingBakeJson.h"

#include <cstdint>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::SCENE::SERIALIZATION {

    namespace JsonMath = ::HIKARI::SERIALIZATION::JSON::MATH;

    namespace {

        uint32_t ReadProbeCount(const nlohmann::json& value, uint32_t fallback) {

            if (!value.is_number_integer()) {
                return fallback;
            }
            const int64_t signedValue = value.get<int64_t>();
            return signedValue >= 0 ? static_cast<uint32_t>(signedValue) : fallback;
        }

    } // namespace

    void SerializeSceneLightingBakeJson(const SceneLightingBakeSettings& settings, nlohmann::json& out) {

        out = nlohmann::json::object();
        LightProbeVolumeSettings lightProbe = settings.lightProbeVolume;
        ClampLightProbeVolumeSettings(lightProbe);

        // ベイク設定は authoring 状態として SceneDocument に保持する。
        out["lightProbeVolume"]["enabled"] = lightProbe.enabled;
        out["lightProbeVolume"]["origin"] = JsonMath::ToJsonArray(lightProbe.origin);
        out["lightProbeVolume"]["size"] = JsonMath::ToJsonArray(lightProbe.size);
        out["lightProbeVolume"]["count"] = nlohmann::json::array({ lightProbe.countX, lightProbe.countY, lightProbe.countZ });
        out["lightProbeVolume"]["intensity"] = lightProbe.intensity;
        out["lightProbeVolume"]["captureResolution"] = lightProbe.captureResolution;
        out["lightProbeVolume"]["shOrder"] = lightProbe.shOrder;
    }

    void DeserializeSceneLightingBakeJson(const nlohmann::json& node, SceneLightingBakeSettings& outSettings) {

        if (!node.is_object()) {
            ClampLightProbeVolumeSettings(outSettings.lightProbeVolume);
            return;
        }

        if (node.contains("lightProbeVolume") && node["lightProbeVolume"].is_object()) {
            const nlohmann::json& lightProbe = node["lightProbeVolume"];
            LightProbeVolumeSettings& out = outSettings.lightProbeVolume;
            out.enabled = lightProbe.value("enabled", out.enabled);
            out.origin = JSONREAD::Vec3Or(lightProbe.value("origin", nlohmann::json::array()), out.origin);
            out.size = JSONREAD::Vec3Or(lightProbe.value("size", nlohmann::json::array()), out.size);

            if (lightProbe.contains("count") && lightProbe["count"].is_array() && lightProbe["count"].size() >= 3) {
                const nlohmann::json& count = lightProbe["count"];
                out.countX = ReadProbeCount(count[0], out.countX);
                out.countY = ReadProbeCount(count[1], out.countY);
                out.countZ = ReadProbeCount(count[2], out.countZ);
            } else {
                out.countX = lightProbe.value("countX", out.countX);
                out.countY = lightProbe.value("countY", out.countY);
                out.countZ = lightProbe.value("countZ", out.countZ);
            }

            out.intensity = lightProbe.value("intensity", out.intensity);
            out.captureResolution = lightProbe.value("captureResolution", out.captureResolution);
            out.shOrder = lightProbe.value("shOrder", out.shOrder);
        }

        ClampLightProbeVolumeSettings(outSettings.lightProbeVolume);
    }

} // namespace HIKARI::SCENE::SERIALIZATION
