#include "Scene/Vfx/HIKARI_FluidInteractionService.h"

#include <algorithm>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Scene/HIKARI_RuntimeObjectActivityService.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
namespace {
using RENDER3D::FLUID::FluidInteraction;
using RENDER3D::FLUID::FluidInteractionKind;
using RENDER3D::FLUID::FluidInteractionShape;

bool InRange(float value, float low, float high) noexcept {
    return MATH::IsFinite(value) && value >= low && value <= high;
}

bool IsValid(const FluidInteraction& interaction) noexcept {
    const auto& q = interaction.worldRotation;
    const float lengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    const auto& p = interaction.worldPosition;
    const auto& e = interaction.extents;
    return static_cast<uint32_t>(interaction.kind) <= static_cast<uint32_t>(FluidInteractionKind::Emit) &&
           static_cast<uint32_t>(interaction.shape) <= static_cast<uint32_t>(FluidInteractionShape::Box) &&
           MATH::IsFinite(q) && InRange(lengthSquared, 1.0e-12f, 1.0e12f) && InRange(p.x, -1.0e6f, 1.0e6f) &&
           InRange(p.y, -1.0e6f, 1.0e6f) && InRange(p.z, -1.0e6f, 1.0e6f) && InRange(e.x, 0.001f, 1000.0f) &&
           InRange(e.y, 0.0f, 1000.0f) && InRange(e.z, 0.0f, 1000.0f) && MATH::IsFinite(interaction.direction) &&
           MATH::Length(interaction.direction) <= 1.0e6f && InRange(interaction.strength, -10000.0f, 10000.0f) &&
           (interaction.kind != FluidInteractionKind::Emit || interaction.strength >= 0.0f);
}

// 汇（向きの無い負の力）：吸い込んだ烟の量を回読する。
bool IsSink(const FluidInteraction& interaction) noexcept {
    return interaction.kind == FluidInteractionKind::Force && interaction.strength < 0.0f &&
           interaction.direction.x == 0.0f && interaction.direction.y == 0.0f && interaction.direction.z == 0.0f;
}

// (owner, instance) から回読の鍵（0 にはしない）。
uint64_t SinkKey(uint64_t owner, uint64_t instance) noexcept {
    uint64_t h = owner * 0x9e3779b97f4a7c15ull ^ (instance + 0x632be59bd9b4e019ull);
    h = (h ^ (h >> 30u)) * 0xbf58476d1ce4e5b9ull;
    h = (h ^ (h >> 27u)) * 0x94d049bb133111ebull;
    h ^= h >> 31u;
    return h != 0u ? h : 1u;
}

bool IsHidden(const World& world, const GameObject* owner) noexcept {
    const auto* activity = world.Services().Find<RuntimeObjectActivityService>();
    for (const GameObject* object = owner; activity != nullptr && object != nullptr; object = object->GetParent())
        if (activity->Get(object->GetRuntimeHandle()) == GameProjectRuntimeActivity::SuspendedHidden)
            return true;
    return false;
}
} // namespace

GameProjectWorldResultCode FluidInteractionService::Submit(FluidInteractionSource source, uint64_t owner,
                                                           uint64_t instance, const FluidInteraction& interaction,
                                                           uint64_t frameIndex) noexcept {
    if (owner == 0u || instance == 0u || !IsValid(interaction))
        return GameProjectWorldResultCode::InvalidRequest;
    if (frameIndex_ != frameIndex) {
        count_ = 0u;
        frameIndex_ = frameIndex;
    }
    Entry entry{source, owner, instance, interaction};
    entry.interaction.worldRotation = MATH::NormalizeQ(entry.interaction.worldRotation);
    entry.interaction.velocity = {};
    for (uint32_t index = 0u; index < count_; ++index) {
        if (entries_[index].Same(entry)) {
            entries_[index] = entry;
            return GameProjectWorldResultCode::Success;
        }
    }
    if (count_ == entries_.size())
        return GameProjectWorldResultCode::MutationRejected;
    entries_[count_++] = entry;
    return GameProjectWorldResultCode::Success;
}

void FluidInteractionService::Remove(FluidInteractionSource source, uint64_t owner, uint64_t instance) noexcept {
    const Entry key{source, owner, instance, {}};
    for (uint32_t index = 0u; index < count_; ++index) {
        if (entries_[index].Same(key)) {
            std::move(entries_.begin() + index + 1u, entries_.begin() + count_, entries_.begin() + index);
            --count_;
            return;
        }
    }
}

void FluidInteractionService::Clear() noexcept {
    count_ = 0u;
    frameIndex_ = ~uint64_t{0};
    previousCount_ = 0u;
    previousFrame_ = ~uint64_t{0};
    sinkCount_ = 0u;
}

FluidInteractionService::Sink* FluidInteractionService::FindSink(uint64_t owner, uint64_t instance) noexcept {
    for (uint32_t index = 0u; index < sinkCount_; ++index)
        if (sinks_[index].owner == owner && sinks_[index].instance == instance)
            return &sinks_[index];
    return nullptr;
}

void FluidInteractionService::ReceiveSinkReports(std::span<const RENDER3D::FLUID::FluidSinkReport> reports) noexcept {
    for (const RENDER3D::FLUID::FluidSinkReport& report : reports)
        for (uint32_t index = 0u; index < sinkCount_; ++index)
            if (sinks_[index].reportKey == report.reportKey) {
                sinks_[index].removed += report.removed;
                break;
            }
}

bool FluidInteractionService::GetRemovedSmoke(uint64_t owner, uint64_t instance, double& outRemoved) const noexcept {
    outRemoved = 0.0;
    for (uint32_t index = 0u; index < sinkCount_; ++index)
        if (sinks_[index].owner == owner && sinks_[index].instance == instance) {
            outRemoved = sinks_[index].removed;
            return true;
        }
    return false;
}

std::span<const FluidInteraction> FluidInteractionService::PrepareFrame(const World& world, uint64_t frameIndex,
                                                                        float deltaSeconds) {
    if (frameIndex_ != frameIndex)
        count_ = 0u;
    const bool consecutive = previousFrame_ != ~uint64_t{0} && previousFrame_ + 1u == frameIndex && deltaSeconds > 0.0f;
    uint32_t preparedCount = 0u;
    for (uint32_t index = 0u; index < count_; ++index) {
        Entry& entry = entries_[index];
        const GameObject* owner = world.FindObject(RuntimeObjectHandle::FromValue(entry.owner));
        if (owner == nullptr || IsHidden(world, owner))
            continue;
        FluidInteraction& interaction = entry.interaction;
        // 障碍物と注入は前のフレームの位置から動く速さを決める（障碍物は烟を押し、注入は動いた線分を掃く）。
        const bool moving =
            interaction.kind == FluidInteractionKind::Obstacle || interaction.kind == FluidInteractionKind::Emit;
        if (moving && consecutive) {
            for (uint32_t previous = 0u; previous < previousCount_; ++previous) {
                if (!previous_[previous].Same(entry))
                    continue;
                // 瞬間移動（生まれ直し、編集中のドラッグ、形の並びの入れ替え）は速度 0：烟を押さない。
                const MATH::Vec3 velocity =
                    (interaction.worldPosition - previous_[previous].interaction.worldPosition) * (1.0f / deltaSeconds);
                interaction.velocity =
                    MATH::Length(velocity) <= RENDER3D::FLUID::kFluidTeleportSpeed ? velocity : MATH::Vec3{};
                break;
            }
        }
        FluidInteraction& prepared = prepared_[preparedCount++];
        prepared = interaction;
        // プロジェクトの汇は回読の鍵を付ける（累計の表が一杯なら数えない）。
        if (entry.source == FluidInteractionSource::Project && IsSink(interaction)) {
            Sink* sink = FindSink(entry.owner, entry.instance);
            if (sink == nullptr && sinkCount_ < sinks_.size()) {
                sink = &sinks_[sinkCount_++];
                *sink = {entry.owner, entry.instance, SinkKey(entry.owner, entry.instance), 0.0, frameIndex};
            }
            if (sink != nullptr) {
                sink->lastFrame = frameIndex;
                prepared.reportKey = sink->reportKey;
            }
        }
    }
    // 出さなくなって十分たった汇の累計を捨てる。
    for (uint32_t index = sinkCount_; index-- > 0u;)
        if (sinks_[index].lastFrame + kSinkHoldFrames < frameIndex)
            sinks_[index] = sinks_[--sinkCount_];
    // 一時停止中のフレームは位置が変わらないので、次に動いた時の速度は止まる前の位置から決める。
    if (deltaSeconds > 0.0f || previousFrame_ == ~uint64_t{0}) {
        std::copy(entries_.begin(), entries_.begin() + count_, previous_.begin());
        previousCount_ = count_;
    }
    previousFrame_ = frameIndex;
    return {prepared_.data(), preparedCount};
}

} // namespace HIKARI
