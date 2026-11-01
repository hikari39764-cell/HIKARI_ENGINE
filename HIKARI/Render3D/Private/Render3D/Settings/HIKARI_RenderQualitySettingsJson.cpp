#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"

#include <exception>
#include <string>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Render3D/Settings/HIKARI_RenderQualityFields.h"

namespace HIKARI::RENDER3D {

nlohmann::json SerializeRenderQualitySettings(const RenderQualitySettings& source) {
    nlohmann::json out = nlohmann::json::object();
    JSONFIELDS::Write(out, NormalizeRenderQualitySettings(source), kRenderQualitySettingsFields);
    return out;
}

bool DeserializeRenderQualitySettings(const nlohmann::json& node, RenderQualitySettings& inOutSettings,
                                      std::string* errorMessage) {
    // 厳密な読み取りは失敗した時に一部だけ書き換わるので、作業用の複製へ読む。
    RenderQualitySettings parsed = inOutSettings;
    try {
        JSONFIELDS::Read(node, parsed, kRenderQualitySettingsFields);
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = std::string("Render quality settings: ") + error.what() + ".";
        }
        return false;
    }
    inOutSettings = NormalizeRenderQualitySettings(parsed);
    return true;
}

} // namespace HIKARI::RENDER3D
