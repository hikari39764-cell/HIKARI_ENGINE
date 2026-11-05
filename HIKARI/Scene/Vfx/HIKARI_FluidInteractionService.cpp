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

// 一フレームの移動から決める障碍物の速さの上限 [m/s]（瞬間移動で烟が吹き飛ばないように）。
constexpr float kMaximumObstacleSpeed = 60.0f;

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

GameProjectWorldResultCode FluidInteractionService::Submit(uint64_t owner, uint64_t instance,
                                                           const FluidInteraction& interaction,
                                                           uint64_t frameIndex) noexcept {
    if (owner == 0u || instance == 0u || !IsValid(interaction))
        return GameProjectWorldResultCode::InvalidRequest;
    if (frameIndex_ != frameIndex) {
        count_ = 0u;
        frameIndex_ = frameIndex;
    }
    FluidInteraction stored = interaction;
    stored.worldRotation = MATH::NormalizeQ(stored.worldRotation);
    stored.velocity = {};
    for (uint32_t index = 0u; index < count_; ++index) {
        if (entries_[index].owner == owner && entries_[index].instance == instance) {
            entries_[index].interaction = stored;
            return GameProjectWorldResultCode::Success;
        }
    }
    if (count_ == entries_.size())
        return GameProjectWorldResultCode::MutationRejected;
    entries_[count_++] = {owner, instance, stored};
    return GameProjectWorldResultCode::Success;
}

void FluidInteractionService::Remove(uint64_t owner, uint64_t instance) noexcept {
    for (uint32_t index = 0u; index < count_; ++index) {
        if (entries_[index].owner == owner && entries_[index].instance == instance) {
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
                if (previous_[previous].owner != entry.owner || previous_[previous].instance != entry.instance)
                    continue;
                MATH::Vec3 velocity =
                    (interaction.worldPosition - previous_[previous].interaction.worldPosition) * (1.0f / deltaSeconds);
                const float speed = MATH::Length(velocity);
                if (speed > kMaximumObstacleSpeed)
                    velocity = velocity * (kMaximumObstacleSpeed / speed);
                interaction.velocity = velocity;
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
