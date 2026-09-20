#pragma once
#include <algorithm>
#include <string_view>
#include <vector>
#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::VFX {
    inline bool VfxLayerPrecedes(const VfxEmitterData& left, const VfxEmitterData& right) noexcept {
        return left.order != right.order ? left.order < right.order : left.id < right.id;
    }
    inline std::vector<const VfxEmitterData*> CollectVfxLayerChildren(
        const VfxAssetData& asset, std::string_view parent) {
        std::vector<const VfxEmitterData*> children;
        for (const auto& layer : asset.emitters)
            if (layer.parentEmitterId == parent) children.push_back(&layer);
        std::sort(children.begin(), children.end(), [](const auto* a, const auto* b) { return VfxLayerPrecedes(*a, *b); });
        return children;
    }
    inline std::vector<const VfxEmitterData*> CollectVfxLayerTree(const VfxAssetData& asset, std::string_view parent = {}) {
        std::vector<const VfxEmitterData*> result;
        const auto visit = [&](const auto& self, std::string_view parent) -> void {
            for (const auto* layer : CollectVfxLayerChildren(asset, parent)) {
                if (result.size() >= asset.emitters.size()) return;
                result.push_back(layer);
                self(self, layer->id);
            }
        };
        visit(visit, parent);
        return result;
    }
    // 内側の下段調整から外側の上段調整へ。隣接グループへは漏らさない。
    inline std::vector<const VfxEmitterData*> CollectVfxAdjustments(
        std::span<const VfxEmitterData> layers, const VfxEmitterData& target) {
        std::vector<const VfxEmitterData*> result;
        if (target.kind == VfxLayerKind::Group || target.kind == VfxLayerKind::Adjustment) return result;
        const auto* scope = &target;
        for (size_t depth = 0; depth < layers.size(); ++depth) {
            std::vector<const VfxEmitterData*> adjustments;
            for (const auto& layer : layers)
                if (layer.kind == VfxLayerKind::Adjustment && layer.parentEmitterId == scope->parentEmitterId &&
                    VfxLayerPrecedes(layer, *scope)) adjustments.push_back(&layer);
            std::sort(adjustments.begin(), adjustments.end(), [](const auto* a, const auto* b) { return VfxLayerPrecedes(*b, *a); });
            result.insert(result.end(), adjustments.begin(), adjustments.end());
            const auto parent = std::find_if(layers.begin(), layers.end(), [&](const auto& layer) { return layer.id == scope->parentEmitterId; });
            if (parent == layers.end()) break;
            scope = &*parent;
        }
        return result;
    }
}
