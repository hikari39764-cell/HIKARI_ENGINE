#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "HIKARI/Project/WorldTypes.h"
#include "Render3D/Vfx/HIKARI_FluidTypes.h"

namespace HIKARI {
class World;

// 場面の流体を押すもの（障碍物・冲量・力・注入）。文書の実行時が持ち、World へ借りた service として見せる。
// (owner, instance) ごとにこのフレームだけ有効で、次のフレームに出さなければ消える（LocalFogService と同じ規矩）。
// 障碍物と注入の動く速さは同じ組の前のフレームの位置からゲーム時間で決める。
// 出どころ（障碍物組件 / プロジェクトの FluidView）ごとに組は別で、同じ owner と instance でも互いを置き換えない。
// プロジェクトが出した烟を消す力（汇＝向きの無い負の力か、吸収のある力）は消した烟の量を GPU から回読して累計する
// （GetRemovedSmoke）。
enum class FluidInteractionSource : uint8_t { Component, Project };

class FluidInteractionService final {
public:
    GameProjectWorldResultCode Submit(FluidInteractionSource source, uint64_t owner, uint64_t instance,
                                      const RENDER3D::FLUID::FluidInteraction& interaction,
                                      uint64_t frameIndex) noexcept;
    void Remove(FluidInteractionSource source, uint64_t owner, uint64_t instance) noexcept;
    void Clear() noexcept;
    // このフレームの表（持ち主の消えたもの・隠れたものを除く）。deltaSeconds はゲーム時間（一時停止で 0）。
    std::span<const RENDER3D::FLUID::FluidInteraction> PrepareFrame(const World& world, uint64_t frameIndex,
                                                                    float deltaSeconds);
    // 描画が回読した汇の吸い込み量を累計へ足す（次のフレームの PrepareFrame の前に呼ぶ）。
    void ReceiveSinkReports(std::span<const RENDER3D::FLUID::FluidSinkReport> reports) noexcept;
    // プロジェクトが出した汇 (owner, instance) がこれまでに吸い込んだ烟の量 [密度 × m³]。汇でない・まだ出していない・
    // 出さなくなって kSinkHoldFrames を過ぎたものは見つからない（false、out は 0）。
    bool GetRemovedSmoke(uint64_t owner, uint64_t instance, double& outRemoved) const noexcept;

    // 出さなくなった汇の累計を残すフレーム数（遅れて届く回読を受け取り、ゲームが最後の量を読めるように）。
    static constexpr uint64_t kSinkHoldFrames = 120u;

private:
    struct Sink {
        uint64_t owner = 0u;
        uint64_t instance = 0u;
        uint64_t reportKey = 0u;
        double removed = 0.0;
        uint64_t lastFrame = 0u;
    };
    Sink* FindSink(uint64_t owner, uint64_t instance) noexcept;
    struct Entry {
        FluidInteractionSource source = FluidInteractionSource::Component;
        uint64_t owner = 0u;
        uint64_t instance = 0u;
        RENDER3D::FLUID::FluidInteraction interaction{};

        bool Same(const Entry& other) const noexcept {
            return source == other.source && owner == other.owner && instance == other.instance;
        }
    };
    static constexpr uint32_t kCapacity = RENDER3D::FLUID::kMaximumFluidInteractionsPerFrame;
    std::array<Entry, kCapacity> entries_{};
    uint32_t count_ = 0u;
    uint64_t frameIndex_ = ~uint64_t{0};
    // 前の PrepareFrame の表（障碍物と注入の速度を決める）。
    std::array<Entry, kCapacity> previous_{};
    uint32_t previousCount_ = 0u;
    uint64_t previousFrame_ = ~uint64_t{0};
    std::array<RENDER3D::FLUID::FluidInteraction, kCapacity> prepared_{};
    std::array<Sink, kCapacity> sinks_{};
    uint32_t sinkCount_ = 0u;
};

} // namespace HIKARI
