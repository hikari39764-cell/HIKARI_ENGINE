#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "HIKARI/Project/WorldTypes.h"
#include "Render3D/Vfx/HIKARI_FluidTypes.h"

namespace HIKARI {
class World;

// 場面の流体を押すもの（障碍物・冲量・力）。文書の実行時が持ち、World へ借りた service として見せる。
// (owner, instance) ごとにこのフレームだけ有効で、次のフレームに出さなければ消える（LocalFogService と同じ規矩）。
// 障碍物の速度は同じ組の前のフレームの位置からゲーム時間で決める。
class FluidInteractionService final {
public:
    GameProjectWorldResultCode Submit(uint64_t owner, uint64_t instance,
                                      const RENDER3D::FLUID::FluidInteraction& interaction,
                                      uint64_t frameIndex) noexcept;
    void Remove(uint64_t owner, uint64_t instance) noexcept;
    void Clear() noexcept;
    // このフレームの表（持ち主の消えたもの・隠れたものを除く）。deltaSeconds はゲーム時間（一時停止で 0）。
    std::span<const RENDER3D::FLUID::FluidInteraction> PrepareFrame(const World& world, uint64_t frameIndex,
                                                                    float deltaSeconds);

private:
    struct Entry {
        uint64_t owner = 0u;
        uint64_t instance = 0u;
        RENDER3D::FLUID::FluidInteraction interaction{};
    };
    static constexpr uint32_t kCapacity = RENDER3D::FLUID::kMaximumFluidInteractionsPerFrame;
    std::array<Entry, kCapacity> entries_{};
    uint32_t count_ = 0u;
    uint64_t frameIndex_ = ~uint64_t{0};
    // 前の PrepareFrame の障碍物（速度を決める）。
    std::array<Entry, kCapacity> previous_{};
    uint32_t previousCount_ = 0u;
    uint64_t previousFrame_ = ~uint64_t{0};
    std::array<RENDER3D::FLUID::FluidInteraction, kCapacity> prepared_{};
};

} // namespace HIKARI
