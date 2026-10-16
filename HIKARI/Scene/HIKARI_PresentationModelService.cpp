#include "Scene/HIKARI_PresentationModelService.h"
#include "Core/Numeric/HIKARI_NonZero.h"
#include "Core/Math/HIKARI_MathValidation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace HIKARI {
namespace {
constexpr uint64_t kPresentationIdPrefix = 0xA000000000000000ull;
constexpr uint64_t kPresentationIdMask = 0xF000000000000000ull;
constexpr float kRotationEpsilonSquared = 1.0e-12f;

using MATH::IsFinite;

bool IsValidTransform(const Transform3D& transform) noexcept {
    if (!IsFinite(transform.position) || !IsFinite(transform.rotation) || !IsFinite(transform.scale) ||
        transform.scale.x <= 0.0f || transform.scale.y <= 0.0f || transform.scale.z <= 0.0f) {
        return false;
    }
    const MATH::Quat& rotation = transform.rotation;
    const float lengthSquared =
        rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
    return std::isfinite(lengthSquared) && lengthSquared > kRotationEpsilonSquared;
}

bool HasSameTransformPayload(const Transform3D& lhs, const Transform3D& rhs) noexcept {
    if (lhs.position.x != rhs.position.x || lhs.position.y != rhs.position.y || lhs.position.z != rhs.position.z ||
        lhs.rotation.x != rhs.rotation.x || lhs.rotation.y != rhs.rotation.y || lhs.rotation.z != rhs.rotation.z ||
        lhs.rotation.w != rhs.rotation.w || lhs.scale.x != rhs.scale.x || lhs.scale.y != rhs.scale.y ||
        lhs.scale.z != rhs.scale.z || lhs.useExplicitMatrix != rhs.useExplicitMatrix) {
        return false;
    }
    for (size_t column = 0u; column < 4u; ++column) {
        for (size_t row = 0u; row < 4u; ++row) {
            if (lhs.explicitMatrix.m[column][row] != rhs.explicitMatrix.m[column][row]) {
                return false;
            }
        }
    }
    return true;
}

bool HasSamePayload(const PresentationModelRecord& record, const PresentationModelSubmission& submission) noexcept {
    return record.assetGuid == submission.assetGuid && record.assetPath == submission.assetPath &&
           record.materialSetAssetGuid == submission.materialSetAssetGuid &&
           record.socketName == submission.socketName && record.exclusiveNodeName == submission.exclusiveNodeName &&
           HasSameTransformPayload(record.worldTransform, submission.worldTransform) &&
           HasSameTransformPayload(record.localTransform, submission.localTransform) &&
           record.attachmentEnabled == submission.attachmentEnabled && record.visible == submission.visible &&
           record.castShadow == submission.castShadow && record.receiveShadow == submission.receiveShadow;
}

void AddUnique(std::vector<uint64_t>& ids, std::unordered_set<uint64_t>& membership, uint64_t id) {
    if (id == 0u || !membership.insert(id).second) {
        return;
    }
    try {
        ids.push_back(id);
    } catch (...) {
        membership.erase(id);
        throw;
    }
}
} // namespace

size_t PresentationModelService::KeyHash::operator()(const Key& key) const noexcept {
    uint64_t value = key.ownerObjectHandle + 0x9E3779B97F4A7C15ull;
    value ^= key.instanceId + 0x9E3779B97F4A7C15ull + (value << 6u) + (value >> 2u);
    value ^= value >> 33u;
    value *= 0xFF51AFD7ED558CCDull;
    value ^= value >> 33u;
    return static_cast<size_t>(value);
}

bool PresentationModelService::Submit(const PresentationModelSubmission& submission, uint64_t frameIndex) noexcept {
    if (submission.ownerObjectHandle == 0u || submission.instanceId == 0u ||
        (submission.assetGuid.empty() && submission.assetPath.empty()) ||
        !IsValidTransform(submission.worldTransform) ||
        (submission.attachmentEnabled &&
         (submission.socketName.empty() || !IsValidTransform(submission.localTransform))) ||
        (!submission.attachmentEnabled && !submission.socketName.empty())) {
        return false;
    }

    try {
        const Key key{submission.ownerObjectHandle, submission.instanceId};
        const auto found = records_.find(key);
        bool payloadChanged = true;
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
            record.materialSetAssetGuid = submission.materialSetAssetGuid;
            record.socketName = submission.socketName;
            record.exclusiveNodeName = submission.exclusiveNodeName;
            record.worldTransform = submission.worldTransform;
            record.localTransform = submission.localTransform;
            record.attachmentEnabled = submission.attachmentEnabled;
            record.visible = submission.visible;
            record.castShadow = submission.castShadow;
            record.receiveShadow = submission.receiveShadow;
            records_.emplace(key, std::move(record));
        } else {
            PresentationModelRecord& record = found->second;
            payloadChanged = !HasSamePayload(record, submission);
            if (payloadChanged) {
                record.assetGuid = submission.assetGuid;
                record.assetPath = submission.assetPath;
                record.materialSetAssetGuid = submission.materialSetAssetGuid;
                record.socketName = submission.socketName;
                record.exclusiveNodeName = submission.exclusiveNodeName;
                record.worldTransform = submission.worldTransform;
                record.localTransform = submission.localTransform;
                record.attachmentEnabled = submission.attachmentEnabled;
                record.visible = submission.visible;
                record.castShadow = submission.castShadow;
                record.receiveShadow = submission.receiveShadow;
            }
            removedRenderObjectIdSet_.erase(record.renderObjectId);
            removedRenderObjectIds_.erase(
                std::remove(removedRenderObjectIds_.begin(), removedRenderObjectIds_.end(), record.renderObjectId),
                removedRenderObjectIds_.end());
        }
        // Store the touched frame separately so a failed update cannot
        // accidentally make a stale record appear fresh.
        touchedFrames_[key] = frameIndex;
        if (payloadChanged) {
            NUMERIC::AdvanceNonZero(revision_);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool PresentationModelService::Remove(uint64_t ownerObjectHandle, uint64_t instanceId) noexcept {
    if (ownerObjectHandle == 0u || instanceId == 0u) {
        return false;
    }
    try {
        const Key key{ownerObjectHandle, instanceId};
        const auto found = records_.find(key);
        if (found == records_.end()) {
            return false;
        }
        AddUnique(removedRenderObjectIds_, removedRenderObjectIdSet_, found->second.renderObjectId);
        const uint64_t renderObjectId = found->second.renderObjectId;
        records_.erase(found);
        touchedFrames_.erase(key);
        frameRecords_.erase(std::remove_if(frameRecords_.begin(), frameRecords_.end(),
                                           [renderObjectId](const PresentationModelRecord& record) {
                                               return record.renderObjectId == renderObjectId;
                                           }),
                            frameRecords_.end());
        NUMERIC::AdvanceNonZero(revision_);
        return true;
    } catch (...) {
        return false;
    }
}

PresentationModelRecord* PresentationModelService::FindRecord(uint64_t ownerObjectHandle,
                                                              uint64_t instanceId) noexcept {
    if (ownerObjectHandle == 0u || instanceId == 0u) {
        return nullptr;
    }
    const auto found = records_.find(Key{ownerObjectHandle, instanceId});
    return found != records_.end() ? &found->second : nullptr;
}

const PresentationModelRecord* PresentationModelService::FindRecord(uint64_t ownerObjectHandle,
                                                                    uint64_t instanceId) const noexcept {
    if (ownerObjectHandle == 0u || instanceId == 0u) {
        return nullptr;
    }
    const auto found = records_.find(Key{ownerObjectHandle, instanceId});
    return found != records_.end() ? &found->second : nullptr;
}

bool PresentationModelService::HasRecord(uint64_t ownerObjectHandle, uint64_t instanceId) const noexcept {
    return FindRecord(ownerObjectHandle, instanceId) != nullptr;
}

bool PresentationModelService::SetMaterialFxProfile(uint64_t ownerObjectHandle, uint64_t instanceId,
                                                    std::string_view profileId) noexcept {
    if (profileId.empty()) {
        return false;
    }
    return MutateMaterialFx(ownerObjectHandle, instanceId, [profileId](MaterialFxParameterSet& parameters) {
        return parameters.SetProfileId(std::string(profileId));
    });
}

bool PresentationModelService::ClearMaterialFx(uint64_t ownerObjectHandle, uint64_t instanceId) noexcept {
    return MutateMaterialFx(ownerObjectHandle, instanceId, [](MaterialFxParameterSet& parameters) {
        parameters.ClearProfile();
        return true;
    });
}

bool PresentationModelService::ResetMaterialFxToProfileDefaults(uint64_t ownerObjectHandle,
                                                                uint64_t instanceId) noexcept {
    return MutateMaterialFx(ownerObjectHandle, instanceId,
                            [](MaterialFxParameterSet& parameters) { return parameters.ResetToProfileDefaults(); });
}

bool PresentationModelService::SetMaterialFxFloat(uint64_t ownerObjectHandle, uint64_t instanceId, std::string_view key,
                                                  float value) noexcept {
    return MutateMaterialFx(ownerObjectHandle, instanceId, [key, value](MaterialFxParameterSet& parameters) {
        return parameters.SetFloat(key, value) == MaterialFxParameterResult::Success;
    });
}

bool PresentationModelService::SetMaterialFxFloat2(uint64_t ownerObjectHandle, uint64_t instanceId,
                                                   std::string_view key, const DirectX::XMFLOAT2& value) noexcept {
    return MutateMaterialFx(ownerObjectHandle, instanceId, [key, value](MaterialFxParameterSet& parameters) {
        return parameters.SetFloat2(key, value) == MaterialFxParameterResult::Success;
    });
}

bool PresentationModelService::SetMaterialFxFloat3(uint64_t ownerObjectHandle, uint64_t instanceId,
                                                   std::string_view key, const DirectX::XMFLOAT3& value) noexcept {
    return MutateMaterialFx(ownerObjectHandle, instanceId, [key, value](MaterialFxParameterSet& parameters) {
        return parameters.SetFloat3(key, value) == MaterialFxParameterResult::Success;
    });
}

bool PresentationModelService::SetMaterialFxFloat4(uint64_t ownerObjectHandle, uint64_t instanceId,
                                                   std::string_view key, const DirectX::XMFLOAT4& value) noexcept {
    return MutateMaterialFx(ownerObjectHandle, instanceId, [key, value](MaterialFxParameterSet& parameters) {
        return parameters.SetFloat4(key, value) == MaterialFxParameterResult::Success;
    });
}

bool PresentationModelService::SetMaterialFxToggle(uint64_t ownerObjectHandle, uint64_t instanceId,
                                                   std::string_view key, bool value) noexcept {
    return MutateMaterialFx(ownerObjectHandle, instanceId, [key, value](MaterialFxParameterSet& parameters) {
        return parameters.SetToggle(key, value) == MaterialFxParameterResult::Success;
    });
}

bool PresentationModelService::TryGetMaterialFxFloat(uint64_t ownerObjectHandle, uint64_t instanceId,
                                                     std::string_view key, float& outValue) const noexcept {
    try {
        const PresentationModelRecord* record = FindRecord(ownerObjectHandle, instanceId);
        return record != nullptr && record->materialFx.TryGetFloat(key, outValue) == MaterialFxParameterResult::Success;
    } catch (...) {
        return false;
    }
}

bool PresentationModelService::TryGetMaterialFxFloat2(uint64_t ownerObjectHandle, uint64_t instanceId,
                                                      std::string_view key,
                                                      DirectX::XMFLOAT2& outValue) const noexcept {
    try {
        const PresentationModelRecord* record = FindRecord(ownerObjectHandle, instanceId);
        return record != nullptr &&
               record->materialFx.TryGetFloat2(key, outValue) == MaterialFxParameterResult::Success;
    } catch (...) {
        return false;
    }
}

bool PresentationModelService::TryGetMaterialFxFloat3(uint64_t ownerObjectHandle, uint64_t instanceId,
                                                      std::string_view key,
                                                      DirectX::XMFLOAT3& outValue) const noexcept {
    try {
        const PresentationModelRecord* record = FindRecord(ownerObjectHandle, instanceId);
        return record != nullptr &&
               record->materialFx.TryGetFloat3(key, outValue) == MaterialFxParameterResult::Success;
    } catch (...) {
        return false;
    }
}

bool PresentationModelService::TryGetMaterialFxFloat4(uint64_t ownerObjectHandle, uint64_t instanceId,
                                                      std::string_view key,
                                                      DirectX::XMFLOAT4& outValue) const noexcept {
    try {
        const PresentationModelRecord* record = FindRecord(ownerObjectHandle, instanceId);
        return record != nullptr &&
               record->materialFx.TryGetFloat4(key, outValue) == MaterialFxParameterResult::Success;
    } catch (...) {
        return false;
    }
}

bool PresentationModelService::TryGetMaterialFxToggle(uint64_t ownerObjectHandle, uint64_t instanceId,
                                                      std::string_view key, bool& outValue) const noexcept {
    try {
        const PresentationModelRecord* record = FindRecord(ownerObjectHandle, instanceId);
        return record != nullptr &&
               record->materialFx.TryGetToggle(key, outValue) == MaterialFxParameterResult::Success;
    } catch (...) {
        return false;
    }
}

const std::string* PresentationModelService::FindMaterialFxProfileId(uint64_t ownerObjectHandle,
                                                                     uint64_t instanceId) const noexcept {
    const PresentationModelRecord* record = FindRecord(ownerObjectHandle, instanceId);
    return record != nullptr ? &record->materialFx.GetProfileId() : nullptr;
}

void PresentationModelService::PrepareForRender(uint64_t frameIndex,
                                                std::span<const uint64_t> occupiedObjectIds) noexcept {
    try {
        if (records_.empty()) {
            frameRecords_.clear();
            return;
        }
        std::unordered_set<uint64_t> occupiedIds{};
        occupiedIds.reserve(occupiedObjectIds.size() + records_.size());
        for (const uint64_t id : occupiedObjectIds) {
            if (id != 0u) {
                occupiedIds.insert(id);
            }
        }

        frameRecords_.reserve(records_.size());
        size_t frameRecordCount = 0u;
        for (auto it = records_.begin(); it != records_.end();) {
            const auto touched = touchedFrames_.find(it->first);
            if (touched == touchedFrames_.end() || touched->second != frameIndex) {
                AddUnique(removedRenderObjectIds_, removedRenderObjectIdSet_, it->second.renderObjectId);
                touchedFrames_.erase(it->first);
                it = records_.erase(it);
                continue;
            }

            PresentationModelRecord& record = it->second;
            if (record.renderObjectId == 0u || occupiedIds.contains(record.renderObjectId)) {
                const uint64_t oldId = record.renderObjectId;
                record.renderObjectId = AllocateRenderObjectId(occupiedIds);
                if (record.renderObjectId == 0u) {
                    AddUnique(removedRenderObjectIds_, removedRenderObjectIdSet_, oldId);
                    touchedFrames_.erase(it->first);
                    it = records_.erase(it);
                    continue;
                }
                if (oldId != 0u) {
                    AddUnique(removedRenderObjectIds_, removedRenderObjectIdSet_, oldId);
                }
                NUMERIC::AdvanceNonZero(revision_);
            }
            occupiedIds.insert(record.renderObjectId);
            if (frameRecordCount < frameRecords_.size()) {
                frameRecords_[frameRecordCount] = record;
            } else {
                frameRecords_.push_back(record);
            }
            ++frameRecordCount;
            ++it;
        }
        frameRecords_.resize(frameRecordCount);
    } catch (...) {
        frameRecords_.clear();
    }
}

void PresentationModelService::AcknowledgeRenderFrame() noexcept {
    removedRenderObjectIds_.clear();
    removedRenderObjectIdSet_.clear();
}

void PresentationModelService::Clear() noexcept {
    try {
        for (const auto& entry : records_) {
            AddUnique(removedRenderObjectIds_, removedRenderObjectIdSet_, entry.second.renderObjectId);
        }
        records_.clear();
        touchedFrames_.clear();
        frameRecords_.clear();
        NUMERIC::AdvanceNonZero(revision_);
    } catch (...) {
        records_.clear();
        touchedFrames_.clear();
        frameRecords_.clear();
    }
}

uint64_t PresentationModelService::AllocateRenderObjectId(const std::unordered_set<uint64_t>& occupiedIds) noexcept {
    for (uint64_t attempts = 0u; attempts < (std::numeric_limits<uint32_t>::max)(); ++attempts) {
        uint64_t candidate = nextRenderObjectId_;
        ++nextRenderObjectId_;
        if ((nextRenderObjectId_ & kPresentationIdMask) != kPresentationIdPrefix) {
            nextRenderObjectId_ = kPresentationIdPrefix | 1u;
        }
        if (candidate == 0u || (candidate & kPresentationIdMask) != kPresentationIdPrefix ||
            occupiedIds.contains(candidate)) {
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

} // namespace HIKARI
