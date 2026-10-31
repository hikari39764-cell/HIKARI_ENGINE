#include "Scene/Water/HIKARI_WaterSurfaceComponent.h"

#include <string>

#include "Core/HIKARI_JsonRead.h"
#include "Scene/HIKARI_GameObject.h"

#if defined(HIKARI_WITH_EDITOR)
#include "Authoring/HIKARI_FieldInspector.h"
#endif

namespace HIKARI {

const WATER::WaterSurfaceSettings& WaterSurfaceComponent::GetSettings() const noexcept { return settings_; }

void WaterSurfaceComponent::SetSettings(const WATER::WaterSurfaceSettings& settings) {
    const WATER::WaterSurfaceSettings sanitized = WATER::SanitizeWaterSurfaceSettings(settings);
    if (WATER::WaterSurfaceSettingsEqual(settings_, sanitized)) {
        return;
    }
    settings_ = sanitized;
    NotifyRenderStateDirty();
}

void WaterSurfaceComponent::ApplyPreset(WATER::WaterSurfacePreset preset) {
    const WATER::WaterSurfaceGeometryMode geometryMode = settings_.geometryMode;
    WATER::WaterSurfaceSettings next = WATER::MakeWaterSurfacePresetSettings(preset);
    next.geometryMode = geometryMode;
    SetSettings(next);
}

void WaterSurfaceComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kWaterSurfaceFields);
}

void WaterSurfaceComponent::Deserialize(const nlohmann::json& in) {
    // 文書で指定のない項目はプリセットの値になるので、先にプリセットだけを読んで土台にする。
    WATER::WaterSurfaceSettings decoded{};
    if (const nlohmann::json* preset = JSONREAD::FindMember(in, "preset"); preset != nullptr && preset->is_string()) {
        TEXT::TryParseEnum(WATER::kWaterSurfacePresetNames, preset->get<std::string>(), decoded.preset);
    }
    decoded = WATER::MakeWaterSurfacePresetSettings(decoded.preset);
    JSONFIELDS::Read(in, decoded, kWaterSurfaceFields);
    SetSettings(decoded);
}

#if defined(HIKARI_WITH_EDITOR)
void WaterSurfaceComponent::BuildInspector(IInspectorBuilder& builder) {
    WATER::WaterSurfaceSettings next = settings_;
    if (!InspectFields(builder, next, kWaterSurfaceFields)) {
        return;
    }
    if (next.preset != settings_.preset) {
        // プリセットは見た目と動きの一式。形状の持ち方は別の選択なので、切り替えても保つ。
        const WATER::WaterSurfaceGeometryMode geometryMode = next.geometryMode;
        next = WATER::MakeWaterSurfacePresetSettings(next.preset);
        next.geometryMode = geometryMode;
    }
    SetSettings(next);
}
#endif

} // namespace HIKARI
