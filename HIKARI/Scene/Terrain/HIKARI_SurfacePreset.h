#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::TERRAIN {

    enum class ShadingModelId : uint8_t {
        GranularAnime,
        Flat,
    };

    enum class DeformBehaviorId : uint8_t {
        SphereCap,
        SoftSnow,
        None,
    };

    struct GranularMaterialParams {
        MATH::Vec3 lightTint{ 0.95f, 0.84f, 0.60f };
        MATH::Vec3 darkTint{ 0.62f, 0.47f, 0.29f };
        float sparkleStrength = 0.35f;
        float rippleAmount = 0.45f;
        float pitDarkening = 0.32f;
        uint32_t toonBandCount = 3u;
        float toonBandHardness = 0.14f;
    };

    // 層ごとの着色 tint。ペイント時にこの色で混合される。
    // 既定値は従来シェーダーにハードコードされていた snow/dirt と同一で、
    // これにより組み込み層の見た目は一切変わらない。
    struct SurfaceLayerTint {
        MATH::Vec3 light{ 1.0f, 1.0f, 1.0f };
        MATH::Vec3 dark{ 1.0f, 1.0f, 1.0f };
    };

    struct DeformParams {
        DeformBehaviorId behavior = DeformBehaviorId::SphereCap;
        float maxDepth = 0.30f;
        float stampRadius = 0.58f;
        float rimStrength = 0.32f;
        // runtime response は authored terrain より細かい疎格子で保持する。
        // 2 倍なら既定 0.25 m cell が 0.125 m になり、小さな足跡も潰れない。
        uint32_t responseResolutionScale = 2u;
        // 接触は変形の gate にだけ使い、深さはアート調整可能な profile で決める。
        // 物理 solver の侵入量や質量が 0 に近くても一定の見える痕跡を作れる。
        float restDepthScale = 0.36f;
        float minimumDepth = 0.055f;
        float settleStartRatio = 0.72f;
        float settleSeconds = 0.18f;
        float impactDepthScale = 0.03f;
        float rimWidthScale = 0.42f;
        // elastic は緩やかに戻り、plastic は長時間痕跡として残る。
        // diffusion は elastic/loose sand にだけ作用させる。
        float refillPerSecond = 0.008f;
        float diffusionPerSecond = 0.04f;
        float plasticity = 0.93f;
        float plasticRefillPerSecond = 0.0003f;
        float recoveryDelaySeconds = 8.0f;
    };

    struct ShorelineParams {
        float edgeFalloff = 4.0f;
        float edgeDepth = 1.5f;
        float wetness = 0.0f;
    };

    // 実心地表の preset。砂 / 雪 / 泥 / 草 / 岩はこの構造を差し替える。
    struct SurfacePreset {
        std::string id{};
        ShadingModelId shading = ShadingModelId::GranularAnime;
        GranularMaterialParams material{};
        DeformParams deform{};
        ShorelineParams shoreline{};
        SurfaceLayerTint tint{}; // この層をペイントしたときの色
    };

    inline SurfacePreset MakeSandPreset() {
        SurfacePreset preset{};
        preset.id = "sand";
        preset.shading = ShadingModelId::GranularAnime;
        preset.material = {};
        preset.deform.behavior = DeformBehaviorId::SphereCap;
        preset.shoreline = {};
        // 基底層 tint はマテリアルの tint と同一にする。シェーダーは
        // 全層を層 tint で一様に混合するため、単層時に w0=1 で既存の
        // p.lightTint/p.darkTint と一致し、見た目が変わらない。
        preset.tint.light = preset.material.lightTint;
        preset.tint.dark = preset.material.darkTint;
        return preset;
    }

    inline SurfacePreset MakeSnowPreset() {
        SurfacePreset preset = MakeSandPreset();
        preset.id = "snow";
        preset.material.lightTint = { 1.0f, 1.0f, 1.0f };
        preset.material.darkTint = { 0.68f, 0.78f, 0.90f };
        preset.deform.behavior = DeformBehaviorId::SoftSnow;
        preset.deform.maxDepth = 0.45f;
        preset.deform.restDepthScale = 0.38f;
        preset.deform.minimumDepth = 0.05f;
        preset.deform.settleStartRatio = 0.40f;
        preset.deform.settleSeconds = 0.18f;
        preset.deform.rimWidthScale = 0.40f;
        preset.deform.refillPerSecond = 0.004f;
        preset.deform.diffusionPerSecond = 0.025f;
        preset.deform.plasticity = 0.97f;
        preset.deform.plasticRefillPerSecond = 0.00015f;
        preset.deform.recoveryDelaySeconds = 12.0f;
        preset.tint.light = { 1.0f, 1.0f, 1.0f };
        preset.tint.dark = { 0.68f, 0.78f, 0.90f };
        return preset;
    }

    inline SurfacePreset MakeDirtPreset() {
        SurfacePreset preset = MakeSandPreset();
        preset.id = "dirt";
        preset.shading = ShadingModelId::Flat;
        preset.material.lightTint = { 0.52f, 0.38f, 0.24f };
        preset.material.darkTint = { 0.28f, 0.20f, 0.12f };
        preset.material.sparkleStrength = 0.0f;
        preset.deform.behavior = DeformBehaviorId::None;
        preset.tint.light = { 0.52f, 0.38f, 0.24f };
        preset.tint.dark = { 0.28f, 0.20f, 0.12f };
        return preset;
    }

    // ビルトイン preset のフォールバック解決。registry が主経路で、
    // これは registry 未初期化時の安全網として残す。
    inline std::optional<SurfacePreset> ResolveSurfacePreset(
        std::string_view id) {
        if (id == "sand") {
            return MakeSandPreset();
        }
        if (id == "snow") {
            return MakeSnowPreset();
        }
        if (id == "dirt") {
            return MakeDirtPreset();
        }
        return std::nullopt;
    }
} // namespace HIKARI::TERRAIN
