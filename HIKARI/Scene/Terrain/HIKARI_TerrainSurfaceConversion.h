#pragma once

#include "Scene/Sand/HIKARI_SandSurfaceTypes.h"
#include "Scene/Terrain/HIKARI_TerrainSurface.h"

namespace HIKARI::TERRAIN {

    // TerrainSurface の solid 部分を既存 SandSurfaceSettings へ写像する。
    // 描画・変形パイプラインはこの設定を共有し、chunk ごとの高さ場は
    // 別途 query service 経由で渡す。着色パラメータは変えない。
    inline SAND::SandSurfaceSettings ToSandSurfaceSettings(
        const TerrainSurface& surface) {

        SAND::SandSurfaceSettings settings{};
        settings.presetId = surface.presetId;
        settings.gridResolution = surface.chunkCells;
        settings.heightScale = surface.heightScale;
        settings.maxDepth = surface.deform.maxDepth;
        settings.stampRadius = surface.deform.stampRadius;
        settings.rimStrength = surface.deform.rimStrength;
        settings.refillPerSecond = surface.deform.refillPerSecond;
        settings.diffusionPerSecond = surface.deform.diffusionPerSecond;
        settings.edgeFalloff = surface.shoreline.edgeFalloff;
        settings.edgeDepth = surface.shoreline.edgeDepth;
        settings.lightTint = surface.material.lightTint;
        settings.darkTint = surface.material.darkTint;
        settings.sparkleStrength = surface.material.sparkleStrength;
        settings.rippleAmount = surface.material.rippleAmount;
        settings.pitDarkening = surface.material.pitDarkening;
        settings.toonBandCount = surface.material.toonBandCount;
        settings.toonBandHardness = surface.material.toonBandHardness;
        return settings;
    }

} // namespace HIKARI::TERRAIN
