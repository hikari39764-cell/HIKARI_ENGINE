#include "Scene/HIKARI_PresentationModelService.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace HIKARI {
    namespace {
        constexpr uint64_t kPresentationIdPrefix =
            0xA000000000000000ull;
        constexpr uint64_t kPresentationIdMask =
            0xF000000000000000ull;
        constexpr float kRotationEpsilonSquared = 1.0e-12f;

        bool IsFinite(const MATH::Vec3& value) noexcept {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        bool IsFinite(const MATH::Quat& value) noexcept {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z) &&
                std::isfinite(value.w);
        }

        bool IsValidTransform(const Transform3D& transform) noexcept {
            if (!IsFinite(transform.position) ||
                !IsFinite(transform.rotation) ||
                !IsFinite(transform.scale) ||
                transform.scale.x <= 0.0f ||
                transform.scale.y <= 0.0f ||
                transform.scale.z <= 0.0f) {
                return false;
            }
            const MATH::Quat& rotation = transform.rotation;
            const float lengthSquared =
                rotation.x * rotation.x +
                rotation.y * rotation.y +
                rotation.z * rotation.z +
                rotation.w * rotation.w;
            return std::isfinite(lengthSquared) &&
                lengthSquared > kRotationEpsilonSquared;
        }

        void AddUnique(
            std::vector<uint64_t>& ids,
            uint64_t id) {
            if (id == 0u ||
                std::find(ids.begin(), ids.end(), id) != ids.end()) {
                return;
            }
            ids.push_back(id);
        }
    }

    size_t PresentationModelService::KeyHash::operator()(
        const Key& key) const noexcept {
        uint64_t value = key.ownerObjectHandle +
            0x9E3779B97F4A7C15ull;
        value ^= key.instanceId +
            0x9E3779B97F4A7C15ull +
            (value << 6u) + (value >> 2u);
        value ^= value >> 33u;
        value *= 0xFF51AFD7ED558CCDull;
        value ^= value >> 33u;
        return static_cast<size_t>(value);
    }

    bool PresentationModelService::Submit(
        const PresentationModelSubmission& submission,
        uint64_t frameIndex) noexcept {
        if (submission.ownerObjectHandle == 0u ||
            submission.instanceId == 0u ||
            (submission.assetGuid.empty() && submission.assetPath.empty()) ||
            !IsValidTransform(submission.worldTransform) ||
            (submission.attachmentEnabled &&
                (submission.socketName.empty() ||
                    !IsValidTransform(submission.localTransform))) ||
            (!submission.attachmentEnabled &&
                !submission.socketName.empty())) {
            return false;
        }

        try {
            const Key key{
                submission.ownerObjectHandle,
                submission.instanceId
            };
            const auto found = records_.find(key);
            if (found == records_.end()) {
                PresentationModelRecord record{};
                record.ownerObjectHandle = submission.ownerObjectHandle;
                record.instanceId = submission.instanceId;
                record.renderObjectId = AllocateRenderObjectId({});
                if (record.renderObjectId == 0u) {
                    return false;
                }
                record.assetGuid = submission.assetGuid;
                record.assetPath = submission.assetPath;
                record.materialSetAssetGuid =
                    submission.materialSetAssetGuid;
                record.socketName = submission.socketName;
                record.worldTransform = submission.worldTransform;
                record.localTransform = submission.localTransform;
                record.attachmentEnabled = submission.attachmentEnabled;
                record.visible = submission.visible;
                record.castShadow = submission.castShadow;
                record.receiveShadow = submission.receiveShadow;
                records_.emplace(key, std::move(record));
            } else {
                PresentationModelRecord& record = found->second;
                record.assetGuid = submission.assetGuid;
                record.assetPath = submission.assetPath;
                record.materialSetAssetGuid =
                    submission.materialSetAssetGuid;
                record.socketName = submission.socketName;
                record.worldTransform = submission.worldTransform;
                record.localTransform = submission.localTransform;
                record.attachmentEnabled = submission.attachmentEnabled;
                record.visible = submission.visible;
                record.castShadow = submission.castShadow;
                record.receiveShadow = submission.receiveShadow;
                removedRenderObjectIds_.erase(
                    std::remove(
                        removedRenderObjectIds_.begin(),
                        removedRenderObjectIds_.end(),
                        record.renderObjectId),
                    removedRenderObjectIds_.end());
            }
            auto updated = records_.find(key);
            if (updated != records_.end()) {
                // The frame token belongs to the submission, not to the
                // render pass. Multiple render views can consume the same
                // frame without expiring a late-update submission.
                updated->second.worldTransform = submission.worldTransform;
                updated->second.ownerObjectHandle =
                    submission.ownerObjectHandle;
                updated->second.instanceId = submission.instanceId;
                updated->second.assetGuid = submission.assetGuid;
                updated->second.assetPath = submission.assetPath;
                updated->second.materialSetAssetGuid =
                    submission.materialSetAssetGuid;
                updated->second.socketName = submission.socketName;
                updated->second.visible = submission.visible;
                updated->second.castShadow = submission.castShadow;
                updated->second.receiveShadow = submission.receiveShadow;
                updated->second.localTransform = submission.localTransform;
                updated->second.attachmentEnabled =
                    submission.attachmentEnabled;
            }
            // Store the touched frame separately so a failed update cannot
            // accidentally make a stale record appear fresh.
            touchedFrames_[key] = frameIndex;
            AdvanceRevision();
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool PresentationModelService::Remove(
        uint64_t ownerObjectHandle,
        uint64_t instanceId) noexcept {
        if (ownerObjectHandle == 0u || instanceId == 0u) {
            return false;
        }
        try {
            const Key key{ ownerObjectHandle, instanceId };
            const auto found = records_.find(key);
            if (found == records_.end()) {
                return false;
            }
            AddUnique(
                removedRenderObjectIds_,
                found->second.renderObjectId);
            const uint64_t renderObjectId = found->second.renderObjectId;
            records_.erase(found);
            touchedFrames_.erase(key);
            frameRecords_.erase(
                std::remove_if(
                    frameRecords_.begin(),
                    frameRecords_.end(),
                    [renderObjectId](
                        const PresentationModelRecord& record) {
                        return record.renderObjectId == renderObjectId;
                    }),
                frameRecords_.end());
            AdvanceRevision();
            return true;
        }
        catch (...) {
            return false;
        }
    }

    void PresentationModelService::PrepareForRender(
        uint64_t frameIndex,
        std::span<const uint64_t> occupiedObjectIds) noexcept {
        try {
            std::vector<uint64_t> occupiedIds{};
            occupiedIds.reserve(
                occupiedObjectIds.size() + records_.size());
            for (const uint64_t id : occupiedObjectIds) {
                AddUnique(occupiedIds, id);
            }

            frameRecords_.clear();
            frameRecords_.reserve(records_.size());
            for (auto it = records_.begin(); it != records_.end();) {
                const auto touched = touchedFrames_.find(it->first);
                if (touched == touchedFrames_.end() ||
                    touched->second != frameIndex) {
                    AddUnique(
                        removedRenderObjectIds_,
                        it->second.renderObjectId);
                    touchedFrames_.erase(it->first);
                    it = records_.erase(it);
                    continue;
                }

                PresentationModelRecord& record = it->second;
                if (record.renderObjectId == 0u ||
                    std::find(
                        occupiedIds.begin(),
                        occupiedIds.end(),
                        record.renderObjectId) != occupiedIds.end()) {
                    const uint64_t oldId = record.renderObjectId;
                    record.renderObjectId = AllocateRenderObjectId(
                        occupiedIds);
                    if (record.renderObjectId == 0u) {
                        AddUnique(removedRenderObjectIds_, oldId);
                        touchedFrames_.erase(it->first);
                        it = records_.erase(it);
                        continue;
                    }
                    if (oldId != 0u) {
                        AddUnique(removedRenderObjectIds_, oldId);
                    }
                    AdvanceRevision();
                }
                AddUnique(occupiedIds, record.renderObjectId);
                frameRecords_.push_back(record);
                ++it;
            }
        }
        catch (...) {
            frameRecords_.clear();
        }
    }

    void PresentationModelService::AcknowledgeRenderFrame() noexcept {
        removedRenderObjectIds_.clear();
    }

    void PresentationModelService::Clear() noexcept {
        try {
            for (const auto& entry : records_) {
                AddUnique(
                    removedRenderObjectIds_,
                    entry.second.renderObjectId);
            }
            records_.clear();
            touchedFrames_.clear();
            frameRecords_.clear();
            AdvanceRevision();
        }
        catch (...) {
            records_.clear();
            touchedFrames_.clear();
            frameRecords_.clear();
        }
    }

    uint64_t PresentationModelService::AllocateRenderObjectId(
        const std::vector<uint64_t>& occupiedIds) noexcept {
        for (uint64_t attempts = 0u; attempts <
                (std::numeric_limits<uint32_t>::max)(); ++attempts) {
            uint64_t candidate = nextRenderObjectId_;
            ++nextRenderObjectId_;
            if ((nextRenderObjectId_ & kPresentationIdMask) !=
                kPresentationIdPrefix) {
                nextRenderObjectId_ = kPresentationIdPrefix | 1u;
            }
            if (candidate == 0u ||
                (candidate & kPresentationIdMask) !=
                    kPresentationIdPrefix ||
                std::find(
                    occupiedIds.begin(),
                    occupiedIds.end(),
                    candidate) != occupiedIds.end()) {
                continue;
            }
            bool usedByRecord = false;
            for (const auto& entry : records_) {
                if (entry.second.renderObjectId == candidate) {
                    usedByRecord = true;
                    break;
                }
            }
            if (!usedByRecord) {
                return candidate;
            }
        }
        return 0u;
    }

    void PresentationModelService::AdvanceRevision() noexcept {
        ++revision_;
        if (revision_ == 0u) {
            revision_ = 1u;
        }
    }

} // namespace HIKARI
