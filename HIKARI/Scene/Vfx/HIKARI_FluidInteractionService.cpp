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
    return static_cast<uint32_t>(interaction.kind) <= static_cast<uint32_t>(FluidInteractionKind::Force) &&
           static_cast<uint32_t>(interaction.shape) <= static_cast<uint32_t>(FluidInteractionShape::Box) &&
           MATH::IsFinite(q) && InRange(lengthSquared, 1.0e-12f, 1.0e12f) && InRange(p.x, -1.0e6f, 1.0e6f) &&
           InRange(p.y, -1.0e6f, 1.0e6f) && InRange(p.z, -1.0e6f, 1.0e6f) && InRange(e.x, 0.001f, 1000.0f) &&
           InRange(e.y, 0.0f, 1000.0f) && InRange(e.z, 0.0f, 1000.0f) && MATH::IsFinite(interaction.direction) &&
           MATH::Length(interaction.direction) <= 1.0e6f && InRange(interaction.strength, -10000.0f, 10000.0f);
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
        if (interaction.kind == FluidInteractionKind::Obstacle && consecutive) {
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
        prepared_[preparedCount++] = interaction;
    }
    // 一時停止中のフレームは位置が変わらないので、次に動いた時の速度は止まる前の位置から決める。
    if (deltaSeconds > 0.0f || previousFrame_ == ~uint64_t{0}) {
        std::copy(entries_.begin(), entries_.begin() + count_, previous_.begin());
        previousCount_ = count_;
    }
    previousFrame_ = frameIndex;
    return {prepared_.data(), preparedCount};
}

} // namespace HIKARI
