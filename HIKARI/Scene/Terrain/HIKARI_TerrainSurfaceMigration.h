#pragma once

#include <algorithm>
#include <string>

#include "Core/HIKARI_JsonRead.h"
#include "Scene/Sand/HIKARI_SandSurfaceTypes.h"
#include "Scene/Water/HIKARI_WaterSurfaceTypes.h"

namespace HIKARI::TERRAIN {

    // 旧 SandSurfaceComponent の properties を、疎 chunk 化した
    // TerrainSurfaceComponent 形式へ移す。着色パラメータはそのまま温存し、
    // 旧の単一グリッド高さは原点の 1 chunk へ再サンプリングして載せる。
    inline nlohmann::json BuildTerrainSurfaceFromLegacy(
        const nlohmann::json& sand,
        const nlohmann::json* water) {
        (void)water; // 水は独立 WaterSurfaceComponent として温存する。

        SAND::SandSurfaceSettings solid{};
        solid.presetId = sand.value("presetId", std::string("sand"));
        solid.heightScale = JSONREAD::NumberMemberOr(
            sand, "heightScale", solid.heightScale);
        solid.maxDepth = JSONREAD::NumberMemberOr(
            sand, "maxDepth", solid.maxDepth);
        solid.stampRadius = JSONREAD::NumberMemberOr(
            sand, "stampRadius", solid.stampRadius);
        solid.rimStrength = JSONREAD::NumberMemberOr(
            sand, "rimStrength", solid.rimStrength);
        solid.responseResolutionScale = static_cast<uint32_t>(
            sand.value(
                "responseResolutionScale",
                solid.responseResolutionScale));
        solid.restDepthScale = JSONREAD::NumberMemberOr(
            sand, "restDepthScale", solid.restDepthScale);
        solid.minimumDepth = JSONREAD::NumberMemberOr(
            sand, "minimumDepth", solid.minimumDepth);
        solid.settleStartRatio = JSONREAD::NumberMemberOr(
            sand, "settleStartRatio", solid.settleStartRatio);
        solid.settleSeconds = JSONREAD::NumberMemberOr(
            sand, "settleSeconds", solid.settleSeconds);
        solid.impactDepthScale = JSONREAD::NumberMemberOr(
            sand, "impactDepthScale", solid.impactDepthScale);
        solid.rimWidthScale = JSONREAD::NumberMemberOr(
            sand, "rimWidthScale", solid.rimWidthScale);
        solid.refillPerSecond = JSONREAD::NumberMemberOr(
            sand, "refillPerSecond", solid.refillPerSecond);
        solid.diffusionPerSecond = JSONREAD::NumberMemberOr(
            sand, "diffusionPerSecond", solid.diffusionPerSecond);
        solid.plasticity = JSONREAD::NumberMemberOr(
            sand, "plasticity", solid.plasticity);
        solid.plasticRefillPerSecond = JSONREAD::NumberMemberOr(
            sand,
            "plasticRefillPerSecond",
            solid.plasticRefillPerSecond);
        solid.recoveryDelaySeconds = JSONREAD::NumberMemberOr(
            sand,
            "recoveryDelaySeconds",
            solid.recoveryDelaySeconds);
        solid.edgeFalloff = JSONREAD::NumberMemberOr(
            sand, "edgeFalloff", solid.edgeFalloff);
        solid.edgeDepth = JSONREAD::NumberMemberOr(
            sand, "edgeDepth", solid.edgeDepth);
        if (sand.contains("lightTint")) {
            solid.lightTint = JSONREAD::Vec3Or(
                sand["lightTint"], solid.lightTint);
        }
        if (sand.contains("darkTint")) {
            solid.darkTint = JSONREAD::Vec3Or(
                sand["darkTint"], solid.darkTint);
        }
        solid.sparkleStrength = JSONREAD::NumberMemberOr(
            sand, "sparkleStrength", solid.sparkleStrength);
        solid.rippleAmount = JSONREAD::NumberMemberOr(
            sand, "rippleAmount", solid.rippleAmount);
        solid.pitDarkening = JSONREAD::NumberMemberOr(
            sand, "pitDarkening", solid.pitDarkening);
        solid.toonBandCount = static_cast<uint32_t>(
            sand.value("toonBandCount", solid.toonBandCount));
        solid.toonBandHardness = JSONREAD::NumberMemberOr(
            sand, "toonBandHardness", solid.toonBandHardness);

        // 旧の単一グリッド高さをデコードする。
        SAND::SandSurfaceTerrain terrain{};
        terrain.authoringResolution = static_cast<uint32_t>(
            sand.value("terrainResolution", terrain.authoringResolution));
        if (sand.contains("terrainHeights") &&
            sand["terrainHeights"].is_string()) {
            (void)SAND::DecodeSandSurfaceTerrainHeights(
                terrain,
                sand["terrainHeights"].get<std::string>());
        }
        SAND::SandSurfaceBrush brush{};
        brush.mode = SAND::ParseSandSurfaceBrushMode(
            sand.value("brushMode", std::string("raise")), brush.mode);
        brush.radius = JSONREAD::NumberMemberOr(
            sand, "brushRadius", brush.radius);
        brush.strength = JSONREAD::NumberMemberOr(
            sand, "brushStrength", brush.strength);
        brush.hardness = JSONREAD::NumberMemberOr(
            sand, "brushHardness", brush.hardness);
        brush.flattenHeight = JSONREAD::NumberMemberOr(
            sand, "brushFlattenHeight", brush.flattenHeight);

        // 1 chunk (原点) に旧高さを再サンプリングして載せる。
        constexpr uint32_t kCells = 32u;
        constexpr uint32_t kStride = kCells + 1u;
        nlohmann::json heights = nlohmann::json::array();
        const uint32_t oldRes = (std::max)(terrain.authoringResolution, 1u);
        const uint32_t oldStride = oldRes + 1u;
        const bool hasOld =
            terrain.heights.size() ==
                static_cast<std::size_t>(oldStride) * oldStride;
        for (uint32_t z = 0u; z <= kCells; ++z) {
            for (uint32_t x = 0u; x <= kCells; ++x) {
                float h = 0.0f;
                if (hasOld) {
                    const float u =
                        static_cast<float>(x) / kCells * oldRes;
                    const float v =
                        static_cast<float>(z) / kCells * oldRes;
                    const uint32_t x0 = (std::min)(
                        static_cast<uint32_t>(u), oldRes);
                    const uint32_t z0 = (std::min)(
                        static_cast<uint32_t>(v), oldRes);
                    h = terrain.heights[
                        static_cast<std::size_t>(z0) * oldStride + x0];
                }
                heights.push_back(h);
            }
        }
        nlohmann::json coverage = nlohmann::json::array();
        nlohmann::json layerWeights = nlohmann::json::array();
        for (uint32_t i = 0u; i < kStride * kStride; ++i) {
            coverage.push_back(1.0f);
            layerWeights.push_back(1.0f);
        }

        nlohmann::json chunk = {
            { "x", 0 },
            { "z", 0 },
            { "cells", kCells },
            { "layerCount", 1u },
            { "heights", heights },
            { "coverage", coverage },
            { "layerWeights", layerWeights },
        };

        return nlohmann::json{
            { "_schemaVersion", 5u },
            { "chunked", true },
            { "presetId", solid.presetId },
            { "heightScale", solid.heightScale },
            { "chunkCells", kCells },
            { "chunkCellSize", 0.25f },
            { "material", {
                { "lightTint", { solid.lightTint.x, solid.lightTint.y, solid.lightTint.z } },
                { "darkTint", { solid.darkTint.x, solid.darkTint.y, solid.darkTint.z } },
                { "sparkleStrength", solid.sparkleStrength },
                { "rippleAmount", solid.rippleAmount },
                { "pitDarkening", solid.pitDarkening },
                { "toonBandCount", solid.toonBandCount },
                { "toonBandHardness", solid.toonBandHardness },
            } },
            { "deform", {
                { "behavior", 0u },
                { "maxDepth", solid.maxDepth },
                { "stampRadius", solid.stampRadius },
                { "rimStrength", solid.rimStrength },
                { "responseResolutionScale",
                    solid.responseResolutionScale },
                { "restDepthScale", solid.restDepthScale },
                { "minimumDepth", solid.minimumDepth },
                { "settleStartRatio", solid.settleStartRatio },
                { "settleSeconds", solid.settleSeconds },
                { "impactDepthScale", solid.impactDepthScale },
                { "rimWidthScale", solid.rimWidthScale },
                { "refillPerSecond", solid.refillPerSecond },
                { "diffusionPerSecond", solid.diffusionPerSecond },
                { "plasticity", solid.plasticity },
                { "plasticRefillPerSecond",
                    solid.plasticRefillPerSecond },
                { "recoveryDelaySeconds",
                    solid.recoveryDelaySeconds },
            } },
            { "shoreline", {
                { "edgeFalloff", solid.edgeFalloff },
                { "edgeDepth", solid.edgeDepth },
                { "wetness", 0.0f },
            } },
            { "layerPresetIds", nlohmann::json::array({ solid.presetId }) },
            { "brush", {
                { "mode", SAND::ToString(brush.mode) },
                { "radius", brush.radius },
                { "strength", brush.strength },
                { "hardness", brush.hardness },
                { "falloff", 0.5f },
                { "flattenHeight", brush.flattenHeight },
            } },
            { "chunks", nlohmann::json::array({ chunk }) },
        };
    }

} // namespace HIKARI::TERRAIN
