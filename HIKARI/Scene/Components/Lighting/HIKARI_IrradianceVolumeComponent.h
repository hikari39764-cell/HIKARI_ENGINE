#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Authoring/HIKARI_FieldDraw.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

// 作者値（シーン文書の properties と同じ形）。項目表（kIrradianceVolumeFields）が保存・読み取り・検査器・説明を決める。
// 焼き込みも使う LightProbeVolumeSettings に、配置の優先度と焼き込みの振り分けを足したもの。origin は保存しない
// （物体の位置と size から決まる）。
struct IrradianceVolumeComponentSettings : LightProbeVolumeSettings {
    int priority = 0;
    std::string bakeSet{"Default"};
    std::string scenario{"Default"};
};

inline constexpr auto kIrradianceVolumeFields = [](auto&& field) {
    using S = IrradianceVolumeComponentSettings;
    using B = LightProbeVolumeSettings;
    using namespace JSONFIELDS;
    field("enabled", &B::enabled, Label{"Enabled"}, Common{});
    field("mode", &B::mode, kLightProbeVolumeModeNames, Label{"Mode"},
          Help{"Realtime volumes are updated by ray-traced dynamic GI and need no bake."}, Common{});
    field("size", &B::size, Range{0.1, 100000.0}, Label{"Size"}, Common{});
    field("intensity", &B::intensity, Range{0.0, kMaximumLightProbeVolumeIntensity}, Label{"Intensity"}, Common{});
    field("blendDistance", &B::blendDistance, Range{0.0, 100000.0}, Label{"Blend Distance"}, Common{});
    field("normalBias", &B::normalBias, Range{0.0, kMaximumLightProbeVolumeNormalBias}, Label{"Surface Normal Bias"},
          Help{"Normal bias samples GI slightly away from walls to reduce leaks."});
    field("priority", &S::priority, Label{"Priority"});

    constexpr const char* kGrid = "Probe Grid";
    field("countX", &B::countX, Label{"Count X"}, Group{kGrid});
    field("countY", &B::countY, Label{"Count Y"}, Group{kGrid});
    field("countZ", &B::countZ, Label{"Count Z"}, Group{kGrid});
    field("captureResolution", &B::captureResolution, Label{"Capture Quality"},
          Draw{FIELD_DRAW::NumberChoice3{{16u, 32u, 64u},
                                         {AuthoringChoice{"16", "16 - Preview"}, AuthoringChoice{"32", "32 - Standard"},
                                          AuthoringChoice{"64", "64 - High"}}}},
          Group{kGrid});
    // 球面調和の次数は常に 3（ClampLightProbeVolumeSettings）。保存だけする。
    field("shOrder", &B::shOrder, Label{"SH Order"}, Hidden{});

    constexpr const char* kRouting = "Bake Routing";
    field("bakeSet", &S::bakeSet, Label{"Bake Set"}, Group{kRouting});
    field("scenario", &S::scenario, Label{"Scenario"}, Group{kRouting});
};

// 格子の数・撮影解像度・強さなどを範囲へ丸め（ClampLightProbeVolumeSettings）、空の振り分け名を既定へ戻す。
IrradianceVolumeComponentSettings NormalizeIrradianceVolumeSettings(IrradianceVolumeComponentSettings settings);

class IrradianceVolumeComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "IrradianceVolumeComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    bool IsEnabled() const noexcept { return settings_.enabled; }
    LightProbeVolumeMode GetMode() const noexcept { return settings_.mode; }
    const MATH::Vec3& GetSize() const noexcept { return settings_.size; }
    bool SetSize(const MATH::Vec3& size) noexcept;
    uint32_t GetCountX() const noexcept { return settings_.countX; }
    uint32_t GetCountY() const noexcept { return settings_.countY; }
    uint32_t GetCountZ() const noexcept { return settings_.countZ; }
    float GetIntensity() const noexcept { return settings_.intensity; }
    float GetBlendDistance() const noexcept { return settings_.blendDistance; }
    uint32_t GetCaptureResolution() const noexcept { return settings_.captureResolution; }
    int GetPriority() const noexcept { return settings_.priority; }
    const std::string& GetBakeSet() const noexcept { return settings_.bakeSet; }
    const std::string& GetScenario() const noexcept { return settings_.scenario; }

private:
    IrradianceVolumeComponentSettings settings_{};
};

} // namespace HIKARI
