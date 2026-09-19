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
    inline std::vector<const VfxEmitterData*> CollectVfxLayerTree(const VfxAssetData& asset) {
        std::vector<const VfxEmitterData*> result;
        const auto visit = [&](const auto& self, std::string_view parent) -> void {
            for (const auto* layer : CollectVfxLayerChildren(asset, parent)) {
                if (result.size() >= asset.emitters.size()) return;
                result.push_back(layer);
                self(self, layer->id);
            }
        };
        visit(visit, {});
        return result;
    }
}
