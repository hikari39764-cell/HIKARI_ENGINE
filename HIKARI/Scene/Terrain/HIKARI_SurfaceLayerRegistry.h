#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Scene/Terrain/HIKARI_SurfacePreset.h"

namespace HIKARI::TERRAIN {

// 実心地表レイヤーの preset を登録・検索する registry。
// 新しい地表（草 / 岩 / 泥…）はここに 1 件登録するだけで、
// 地形コンポーネント・筆刷・UI が同じ契約で扱えるようになる。
// 着色計算そのものは既存シェーダーが保持し、本 registry は
// 「どの層がどの見た目・混合・物理パラメータを持つか」のみを提供する。
class SurfaceLayerRegistry {
public:
    static SurfaceLayerRegistry& Get() noexcept {
        static SurfaceLayerRegistry instance;
        return instance;
    }

    // 既定の組み込み層（sand/snow/dirt/grass/rock）を登録する。
    // アプリ起動時に一度だけ呼ぶ。冪等。
    void RegisterBuiltins() {
        if (initialized_) {
            return;
        }
        initialized_ = true;
        layers_.clear();
        Register(MakeSandPreset());
        Register(MakeSnowPreset());
        Register(MakeDirtPreset());
        Register(MakeGrassPreset());
        Register(MakeRockPreset());
    }

    void Register(const SurfacePreset& preset) {
        for (SurfacePreset& existing : layers_) {
            if (existing.id == preset.id) {
                existing = preset;
                ++revision_;
                return;
            }
        }
        layers_.push_back(preset);
        ++revision_;
    }

    const std::vector<SurfacePreset>& Layers() const noexcept { return layers_; }

    std::size_t Count() const noexcept { return layers_.size(); }

    uint64_t GetRevision() const noexcept { return revision_; }

    const SurfacePreset* Find(std::string_view id) const noexcept {
        for (const SurfacePreset& layer : layers_) {
            if (layer.id == id) {
                return &layer;
            }
        }
        return nullptr;
    }

    const SurfacePreset* At(std::size_t index) const noexcept {
        return index < layers_.size() ? &layers_[index] : nullptr;
    }

    int ResolveIndex(std::string_view id) {
        RegisterBuiltins();
        return IndexOf(id);
    }

    int IndexOf(std::string_view id) const noexcept {
        for (std::size_t i = 0; i < layers_.size(); ++i) {
            if (layers_[i].id == id) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

private:
    bool initialized_ = false;
    uint64_t revision_ = 0u;
    std::vector<SurfacePreset> layers_{};
};

} // namespace HIKARI::TERRAIN
