#pragma once

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

// 画質設定の項目表。設定ファイルの読み書き・値の正規化・画質パネルの項目表示が共有する。
namespace HIKARI::RENDER3D {

inline constexpr auto kDirectionalShadowFields = [](auto&& field) {
    using S = DirectionalShadowQualitySettings;
    using JSONFIELDS::Range;
    field("resolution", &S::resolution, Range{256.0, 8192.0});
    field("cascadeCount", &S::cascadeCount, Range{1.0, 4.0});
    field("cascadeSplitLambda", &S::cascadeSplitLambda, Range{0.0, 1.0});
    field("cascadeBlend", &S::cascadeBlend, Range{0.0, 0.3});
    field("nearPlane", &S::nearPlane, Range{0.01, 100.0});
    field("farPlane", &S::farPlane, Range{1.0, 100000.0});
    field("depthBias", &S::depthBias, Range{0.0, 0.1});
    field("normalBias", &S::normalBias, Range{0.0, 1.0});
    field("pcfEnabled", &S::pcfEnabled);
    field("pcfRadius", &S::pcfRadius, Range{0.0, 8.0});
    field("penumbraScale", &S::penumbraScale, Range{0.0, 16.0});
    field("contactShadowLength", &S::contactShadowLength, Range{0.0, 2.0});
    field("stabilize", &S::stabilize);
};

} // namespace HIKARI::RENDER3D
