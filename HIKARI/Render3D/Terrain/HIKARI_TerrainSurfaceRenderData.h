#pragma once

#include <cstdint>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::TERRAIN {

constexpr std::size_t kTerrainSurfaceMaxGpuDataRecords = 1024u;
// 地形 field と水面 coverage が共有する scene surface float pool の容量。
constexpr std::size_t kSurfaceFeatureFloatPoolCapacity = 1280u * 1024u;
constexpr std::size_t kTerrainSurfaceLayerStyleFloatCount = 20u;
// layer weight は 1 float slot に unorm8 x4 で詰める。
constexpr uint32_t kTerrainSurfaceLayersPerWeightPlane = 4u;

// 1 terrain paint layer の GPU-neutral な見た目。float pool の layer payload に
// 5 float4 で格納する。
struct TerrainSurfaceLayerRenderStyle {
    MATH::Vec3 lightTint{0.95f, 0.84f, 0.60f};
    float textureStrength = 0.52f;
    MATH::Vec3 darkTint{0.62f, 0.47f, 0.29f};
    float normalStrength = 0.72f;
    // Built-in 2x2 atlas の content rect (scale.xy, bias.xy)。
    MATH::Vec4 atlasScaleBias{0.4921875f, 0.4921875f, 0.00390625f, 0.00390625f};
    // 既定 0.25 m セルで 25 m 四方に 1 tile。
    float tilesPerMeter = 1.0f / 25.0f;
    float roughness = 0.92f;
    float saturation = 1.14f;
    uint32_t materialKind = 0u;
    float shadowStrength = 1.0f;
    // sand/snow の ripple・sparkle・deformation shading だけを残す gate。
    float granularStrength = 1.0f;
    // 広い低ポリ面の法線表現。layer 0 の値を頂点焼き込みが使う。
    float facetSizeMeters = 1.6f;
    float facetStrength = 0.0f;
};

// Scene から Render3D へ渡す narrow, GPU-neutral な入力。
// Scene は GPU スロット配置や DirectX パッキングを所有しない。
struct TerrainSurfaceRenderData {
    // 全 field が共有する正方形グリッド解像度。
    uint32_t gridResolution = 64u;
    MATH::Vec2 extents{20.0f, 20.0f};
    float heightScale = 0.32f;
    float rimStrength = 0.28f;

    MATH::Vec3 lightTint{0.95f, 0.84f, 0.60f};
    float sparkleStrength = 0.35f;
    MATH::Vec3 darkTint{0.62f, 0.47f, 0.29f};
    float rippleAmount = 0.45f;
    float pitDarkening = 0.32f;

    uint32_t toonBandCount = 3u;
    float toonBandHardness = 0.14f;
    float wetness = 0.0f;
    // 境界を沈ませる深さ（メートル）。
    float edgeDepth = 1.5f;
    // 変形後の地表が取りうる local Y 範囲。meshlet 剔除の保守包絡に使う。
    float boundsMinY = 0.0f;
    float boundsMaxY = 0.0f;
    // runtime 応答 field (足跡など) を shading へ渡すか。
    bool runtimeResponse = false;

    uint32_t layerCount = 1u;
    // 層ごとの atlas / tint / normal / toon style。layerCount 個。
    std::vector<TerrainSurfaceLayerRenderStyle> layerStyles{};
};

// Scene の地形 projection が焼き込んだ field。全 field は (grid+1)^2 の
// 正方形格子で、頂点変形の高さ・勾配は CPU で確定済み。
struct TerrainSurfaceProjectedFields {
    // ripple と縁の沈降を含む最終 local 高さ。
    std::vector<float> heights{};
    // 法線は normalize(-slopeX, 1, -slopeZ)。
    std::vector<float> slopeX{};
    std::vector<float> slopeZ{};
    std::vector<float> coverage{};
    // runtime 応答 (負 = pit, 正 = rim)。空なら応答なし。
    std::vector<float> deformations{};
    // layer-major。layerCount > 1 の時だけ使う。
    std::vector<float> layerWeights{};
};

// projection revision 間で値が変わった格子範囲 (両端を含む)。
struct TerrainSurfaceFieldRect {
    uint32_t minX = 0u;
    uint32_t minZ = 0u;
    uint32_t maxX = 0u;
    uint32_t maxZ = 0u;
};

bool TerrainSurfaceRenderDataEqual(const TerrainSurfaceRenderData& lhs, const TerrainSurfaceRenderData& rhs) noexcept;

} // namespace HIKARI::RENDER3D::TERRAIN
