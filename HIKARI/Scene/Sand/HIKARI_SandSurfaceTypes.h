#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::SAND {

    constexpr uint32_t kSandSurfaceSchemaVersion = 2u;
    constexpr uint32_t kSandSurfaceMinGridResolution = 16u;
    constexpr uint32_t kSandSurfaceMaxGridResolution = 512u;

    enum class SandSurfaceStyle : uint8_t {
        AnimeDry = 0,
    };

    const char* ToString(SandSurfaceStyle style) noexcept;
    SandSurfaceStyle ParseSandSurfaceStyle(
        std::string_view value,
        SandSurfaceStyle fallback = SandSurfaceStyle::AnimeDry) noexcept;

    enum class SandSurfaceBrushMode : uint8_t {
        Raise = 0,
        Lower = 1,
        Smooth = 2,
        Flatten = 3,
    };

    const char* ToString(SandSurfaceBrushMode mode) noexcept;
    SandSurfaceBrushMode ParseSandSurfaceBrushMode(
        std::string_view value,
        SandSurfaceBrushMode fallback =
            SandSurfaceBrushMode::Raise) noexcept;

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

    SandSurfaceBrush SanitizeSandSurfaceBrush(
        SandSurfaceBrush brush) noexcept;
    SandSurfaceTerrain SanitizeSandSurfaceTerrain(
        SandSurfaceTerrain terrain) noexcept;
    bool SandSurfaceTerrainEqual(
        const SandSurfaceTerrain& lhs,
        const SandSurfaceTerrain& rhs) noexcept;

    float SampleSandSurfaceTerrain(
        const SandSurfaceTerrain& terrain,
        const MATH::Vec2& halfExtents,
        const MATH::Vec2& localXZ) noexcept;
    void SculptSandSurfaceTerrain(
        SandSurfaceTerrain& terrain,
        const MATH::Vec2& halfExtents,
        const MATH::Vec2& localXZ) noexcept;
    void ResetSandSurfaceTerrain(
        SandSurfaceTerrain& terrain) noexcept;
    void BakeSandSurfaceTerrain(
        SandSurfaceTerrain& terrain,
        const MATH::Vec2& halfExtents,
        float rippleAmount,
        float heightScale) noexcept;

    struct SandSurfaceSettings {
        SandSurfaceStyle style = SandSurfaceStyle::AnimeDry;

        // 高さグリッドは (gridResolution + 1)^2 ノードを保持する。
        // ProceduralMeshComponent 側のセグメント数とは独立した
        // インタラクション解像度であり、法線もこのグリッドから求める。
        // 球面の凹みを十分に解像するため既定は 256（200m で約 0.78m/cell）。
        // より細かくする場合は Inspector で 512 まで上げられる。
        uint32_t gridResolution = 256u;

        // 全体的な凹み深さのスケール。単位はローカルメートル。
        float heightScale = 0.32f;
        float maxDepth = 0.30f;
        float stampRadius = 0.42f;
        float rimStrength = 0.28f;
        float refillPerSecond = 0.018f;
        float diffusionPerSecond = 1.2f;

        // アニメ調の二階調トゥーン色と砂粒表現。
        MATH::Vec3 lightTint{ 0.95f, 0.84f, 0.60f };
        MATH::Vec3 darkTint{ 0.62f, 0.47f, 0.29f };
        float sparkleStrength = 0.35f;
        float rippleAmount = 0.45f;
        float pitDarkening = 0.32f;

        // アニメ調トゥーン：明暗を 2..4 段のセル分帯に量子化する。
        uint32_t toonBandCount = 3u;
        // 0.0 は滑らかなグラデーション、0.5 に近づくほどハードなセル境界。
        float toonBandHardness = 0.14f;
    };

    SandSurfaceSettings SanitizeSandSurfaceSettings(
        SandSurfaceSettings settings) noexcept;
    bool SandSurfaceSettingsEqual(
        const SandSurfaceSettings& lhs,
        const SandSurfaceSettings& rhs) noexcept;

    // CPU クエリと GPU シェーダが同一の砂丘底形を共有するための
    // 決定論的ノイズ関数。interaction グリッドはこの上に重なる凹みだけを持つ。
    float EvaluateSandSurfaceRippleHeight(
        const MATH::Vec2& localXZ,
        float rippleAmount,
        float heightScale) noexcept;

    float EvaluateSandSurfaceDuneHeight(
        const MATH::Vec2& localXZ,
        float heightScale) noexcept;

} // namespace HIKARI::SAND
