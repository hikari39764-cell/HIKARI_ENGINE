#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::SAND {

constexpr uint32_t kSandSurfaceSchemaVersion = 2u;
constexpr uint32_t kSandSurfaceMinGridResolution = 16u;
constexpr uint32_t kSandSurfaceMaxGridResolution = 512u;
// smoothstep 後に edge sink が 99% 以上完了してから clip する。
// 0.5 で切ると平滑斜面の半分を失い、既存配置も地形外になってしまう。
constexpr float kSandSurfaceVisibleCoverageThreshold = 0.05f;

enum class SandSurfaceStyle : uint8_t {
    AnimeDry = 0,
};

const char* ToString(SandSurfaceStyle style) noexcept;

enum class SandSurfaceBrushMode : uint8_t {
    Raise = 0,
    Lower = 1,
    Smooth = 2,
    Flatten = 3,
};

const char* ToString(SandSurfaceBrushMode mode) noexcept;
SandSurfaceBrushMode ParseSandSurfaceBrushMode(std::string_view value,
                                               SandSurfaceBrushMode fallback = SandSurfaceBrushMode::Raise) noexcept;

struct SandSurfaceBrush {
    SandSurfaceBrushMode mode = SandSurfaceBrushMode::Raise;
    float radius = 1.0f;
    float strength = 0.12f;
    float hardness = 0.5f;
    float flattenHeight = 0.0f;
};

constexpr uint32_t kSandSurfaceMinTerrainResolution = 16u;
constexpr uint32_t kSandSurfaceMaxTerrainResolution = 128u;
constexpr uint32_t kSandSurfaceDefaultTerrainResolution = 64u;

// コンポーネントが保持する authoring 用の地形高さ。単位はローカルメートル。
// heights は (authoringResolution + 1)^2 個で、グリッドは halfExtents 相当の
// ローカル XZ 空間を正規化した [0,1]^2 にマップされる。
struct SandSurfaceTerrain {
    uint32_t authoringResolution = kSandSurfaceDefaultTerrainResolution;
    std::vector<float> heights{};
    SandSurfaceBrush brush{};
};

bool DecodeSandSurfaceTerrainHeights(SandSurfaceTerrain& terrain, std::string_view encoded);

struct SandSurfaceSettings {
    SandSurfaceStyle style = SandSurfaceStyle::AnimeDry;
    std::string presetId = "sand";
    bool deformationEnabled = true;

    // 高さグリッドは (gridResolution + 1)^2 ノードを保持する。
    // ProceduralMeshComponent 側のセグメント数とは独立した
    // インタラクション解像度であり、法線もこのグリッドから求める。
    // 球面の凹みを十分に解像するため既定は 256（200m で約 0.78m/cell）。
    // より細かくする場合は Inspector で 512 まで上げられる。
    uint32_t gridResolution = 256u;

    // 全体的な凹み深さのスケール。単位はローカルメートル。
    float heightScale = 0.32f;
    float maxDepth = 0.30f;
    float stampRadius = 0.58f;
    float rimStrength = 0.32f;
    uint32_t responseResolutionScale = 2u;
    float restDepthScale = 0.36f;
    float minimumDepth = 0.055f;
    float settleStartRatio = 0.72f;
    float settleSeconds = 0.18f;
    float impactDepthScale = 0.03f;
    float rimWidthScale = 0.42f;
    float refillPerSecond = 0.008f;
    float diffusionPerSecond = 0.04f;
    float plasticity = 0.93f;
    float plasticRefillPerSecond = 0.0003f;
    float recoveryDelaySeconds = 8.0f;

    // 矩形の砂地境界を水面下へ滑らかに沈め、硬いエッジを消す。
    // edgeFalloff は境界からの遷移幅（メートル）、edgeDepth は
    // 境界で沈ませる量（メートル、水面下になる値を指定する）。
    float edgeFalloff = 4.0f;
    float edgeDepth = 1.5f;
    // アニメ調の二階調トゥーン色と砂粒表現。
    MATH::Vec3 lightTint{0.95f, 0.84f, 0.60f};
    MATH::Vec3 darkTint{0.62f, 0.47f, 0.29f};
    float sparkleStrength = 0.35f;
    float rippleAmount = 0.45f;
    float pitDarkening = 0.32f;

    // アニメ調トゥーン：明暗を 2..4 段のセル分帯に量子化する。
    uint32_t toonBandCount = 3u;
    // 0.0 は滑らかなグラデーション、0.5 に近づくほどハードなセル境界。
    float toonBandHardness = 0.14f;
};

SandSurfaceSettings SanitizeSandSurfaceSettings(SandSurfaceSettings settings) noexcept;
bool SandSurfaceSettingsEqual(const SandSurfaceSettings& lhs, const SandSurfaceSettings& rhs) noexcept;

// coverage が作る海岸勾配を、GPU の SandSurfaceDeform と同じ
// smoothstep 曲線で実際のローカル地表高へ反映する。
float EvaluateSandSurfaceEffectiveHeight(float baseHeight, float coverage, float edgeDepth,
                                         float fallbackDepth) noexcept;

} // namespace HIKARI::SAND
