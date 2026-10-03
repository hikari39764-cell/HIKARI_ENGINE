#include "Scene/Lighting/HIKARI_LocalFogService.h"

#include <algorithm>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_RuntimeObjectActivityService.h"

namespace HIKARI {
namespace {
bool InRange(float value, float low, float high) noexcept {
    return MATH::IsFinite(value) && value >= low && value <= high;
}

Transform3D BoxTransform(const GameProjectLocalFogVolume& volume) noexcept {
    const auto& source = volume.worldTransform;
    Transform3D transform{};
    transform.position = {source.position.x, source.position.y, source.position.z};
    transform.rotation = {source.rotation.x, source.rotation.y, source.rotation.z, source.rotation.w};
    transform.scale = {source.scale.x, source.scale.y, source.scale.z};
    return transform;
}

bool IsValid(const GameProjectLocalFogVolume& volume) noexcept {
    if (volume.structSize != sizeof(volume) || volume.apiVersion != kGameProjectLocalFogApiVersion ||
        volume.reserved != 0u) {
        return false;
    }
    const Transform3D transform = BoxTransform(volume);
    const auto& q = transform.rotation;
    const float lengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    // Keep matrix inversion and world-space noise finite, including extreme inputs.
    return MATH::IsFinite(transform.position) && MATH::IsFinite(transform.rotation) &&
           InRange(lengthSquared, 1.0e-12f, 1.0e12f) && InRange(transform.scale.x, 0.001f, 100000.0f) &&
           InRange(transform.scale.y, 0.001f, 100000.0f) && InRange(transform.scale.z, 0.001f, 100000.0f) &&
           InRange(transform.position.x, -1.0e6f, 1.0e6f) && InRange(transform.position.y, -1.0e6f, 1.0e6f) &&
           InRange(transform.position.z, -1.0e6f, 1.0e6f) && InRange(volume.color.x, 0.0f, 1.0f) &&
           InRange(volume.color.y, 0.0f, 1.0f) && InRange(volume.color.z, 0.0f, 1.0f) &&
           InRange(volume.density, 0.0f, 10.0f) && InRange(volume.scatteringIntensity, 0.0f, 8.0f) &&
           InRange(volume.edgeFalloff, 0.0f, 1.0f) && InRange(volume.noiseStrength, 0.0f, 1.0f) &&
           InRange(volume.noiseScale, 0.001f, 100.0f) && InRange(volume.noiseOffset.x, -1.0e6f, 1.0e6f) &&
           InRange(volume.noiseOffset.y, -1.0e6f, 1.0e6f) && InRange(volume.noiseOffset.z, -1.0e6f, 1.0e6f);
}
} // namespace

GameProjectWorldResultCode LocalFogService::Submit(uint64_t owner, uint64_t instance,
                                                   const GameProjectLocalFogVolume& volume,
                                                   uint64_t frameIndex) noexcept {
    if (owner == 0u || instance == 0u || !IsValid(volume)) {
        return GameProjectWorldResultCode::InvalidRequest;
    }
    if (frameIndex_ != frameIndex) {
        count_ = 0u;
        frameIndex_ = frameIndex;
    }
    for (uint32_t index = 0u; index < count_; ++index) {
        if (entries_[index].owner == owner && entries_[index].instance == instance) {
            entries_[index].volume = volume;
            return GameProjectWorldResultCode::Success;
        }
    }
    if (count_ == entries_.size()) {
        return GameProjectWorldResultCode::MutationRejected;
    }
    entries_[count_++] = {owner, instance, volume};
    return GameProjectWorldResultCode::Success;
}

void LocalFogService::Remove(uint64_t owner, uint64_t instance) noexcept {
    for (uint32_t index = 0u; index < count_; ++index) {
        if (entries_[index].owner == owner && entries_[index].instance == instance) {
            std::move(entries_.begin() + index + 1u, entries_.begin() + count_, entries_.begin() + index);
            --count_;
            return;
        }
    }
}

void LocalFogService::Clear() noexcept {
    count_ = 0u;
    frameIndex_ = ~uint64_t{0};
    authoredDocument_ = nullptr;
    disabledObjects_.clear();
}

std::span<const RENDER3D::VOLUMETRIC::LocalFogVolumeGpu> LocalFogService::PrepareFrame(const World& world,
                                                                                       uint64_t frameIndex,
                                                                                       const SceneDocument* document,
                                                                                       uint64_t documentRevision) {
    if (frameIndex_ != frameIndex) {
        count_ = 0u;
        return {};
    }
    if (authoredDocument_ != document || authoredRevision_ != documentRevision) {
        disabledObjects_.clear();
        if (document != nullptr) {
            for (const auto& object : document->objects) {
                if (!object.enabled)
                    disabledObjects_.insert(object.id.value);
            }
        }
        authoredDocument_ = document;
        authoredRevision_ = documentRevision;
    }
    uint32_t projectedCount = 0u;
    const auto* activity = world.Services().Find<RuntimeObjectActivityService>();
    for (uint32_t index = 0u; index < count_; ++index) {
        const Entry& entry = entries_[index];
        const GameObject* owner = world.FindObject(RuntimeObjectHandle::FromValue(entry.owner));
        if (owner == nullptr || entry.volume.density == 0.0f) {
            continue;
        }
        bool visible = true;
        for (const GameObject* object = owner; object != nullptr; object = object->GetParent()) {
            if (disabledObjects_.contains(object->GetDocumentId().value) ||
                (activity != nullptr &&
                 activity->Get(object->GetRuntimeHandle()) == GameProjectRuntimeActivity::SuspendedHidden)) {
                visible = false;
                break;
            }
        }
        if (!visible)
            continue;
        const auto& volume = entry.volume;
        auto& gpu = projected_[projectedCount++];
        gpu.worldToBox = MATH::Inverse(BoxTransform(volume).GetWorldMatrix());
        gpu.colorDensity = {volume.color.x, volume.color.y, volume.color.z, volume.density};
        gpu.noiseEdgeScattering = {volume.noiseScale, volume.noiseStrength, volume.edgeFalloff,
                                   volume.scatteringIntensity};
        gpu.noiseOffset = {volume.noiseOffset.x, volume.noiseOffset.y, volume.noiseOffset.z, 0.0f};
    }
    return {projected_.data(), projectedCount};
}

void LocalFogService::DrawSelectedBounds(const World& world, uint64_t sceneObjectId) const {
    if (sceneObjectId == 0u) {
        return;
    }
    for (uint32_t index = 0u; index < count_; ++index) {
        const auto& entry = entries_[index];
        const GameObject* object = world.FindObject(RuntimeObjectHandle::FromValue(entry.owner));
        if (object == nullptr || object->GetDocumentId().value != sceneObjectId) {
            continue;
        }
        RENDERER3D::DEBUG::Box3D box{};
        box.transform = BoxTransform(entry.volume);
        box.outlineRgba = 0xBFE8FFFF;
        box.depthMode = RENDERER3D::DEBUG::DebugDepthMode::XRay;
        RENDERER3D::DEBUG::SubmitBox3D(box);
    }
}

} // namespace HIKARI
