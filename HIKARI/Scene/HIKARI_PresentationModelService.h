#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Render3D/HIKARI_Transform3D.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxParameterSet.h"
#include "Core/Numeric/HIKARI_NonZero.h"

namespace HIKARI {

struct PresentationModelSubmission {
    uint64_t ownerObjectHandle = 0u;
    uint64_t instanceId = 0u;
    std::string assetGuid{};
    std::string assetPath{};
    std::string materialSetAssetGuid{};
    std::string socketName{};
    // Empty keeps the legacy whole-model submission. Otherwise the
    // render projection exposes only this exact-name node and its
    // descendants.
    std::string exclusiveNodeName{};
    Transform3D worldTransform{};
    Transform3D localTransform{};
    bool attachmentEnabled = false;
    bool visible = true;
    bool castShadow = true;
    bool receiveShadow = true;
    // 出し直さなくても残す（Remove まで）。
    bool retain = false;
};

struct PresentationModelRecord {
    uint64_t ownerObjectHandle = 0u;
    uint64_t instanceId = 0u;
    uint64_t renderObjectId = 0u;
    std::string assetGuid{};
    std::string assetPath{};
    std::string materialSetAssetGuid{};
    std::string socketName{};
    // Empty keeps the legacy whole-model submission. Otherwise the
    // render projection exposes only this exact-name node and its
    // descendants.
    std::string exclusiveNodeName{};
    Transform3D worldTransform{};
    Transform3D localTransform{};
    bool attachmentEnabled = false;
    bool visible = true;
    bool castShadow = true;
    bool receiveShadow = true;
    MaterialFxParameterSet materialFx{};
    bool retain = false;
    // 中身（提出の内容と materialFx）が変わるたびに進む版。描画への写しが変わっていない記録を作り直さないのに使う。
    uint64_t payloadRevision = 1u;
};

// World-owned render-only model submissions. These records never become
// GameObjects, ModelComponents, animation owners, or physics bodies.
class PresentationModelService final {
public:
    PresentationModelService() = default;
    ~PresentationModelService() = default;

    PresentationModelService(const PresentationModelService&) = delete;
    PresentationModelService& operator=(const PresentationModelService&) = delete;

    bool Submit(const PresentationModelSubmission& submission, uint64_t frameIndex) noexcept;
    bool Remove(uint64_t ownerObjectHandle, uint64_t instanceId) noexcept;
    bool HasRecord(uint64_t ownerObjectHandle, uint64_t instanceId) const noexcept;
    bool SetMaterialFxProfile(uint64_t ownerObjectHandle, uint64_t instanceId, std::string_view profileId) noexcept;
    bool ClearMaterialFx(uint64_t ownerObjectHandle, uint64_t instanceId) noexcept;
    bool ResetMaterialFxToProfileDefaults(uint64_t ownerObjectHandle, uint64_t instanceId) noexcept;
    bool SetMaterialFxFloat(uint64_t ownerObjectHandle, uint64_t instanceId, std::string_view key,
                            float value) noexcept;
    bool SetMaterialFxFloat2(uint64_t ownerObjectHandle, uint64_t instanceId, std::string_view key,
                             const DirectX::XMFLOAT2& value) noexcept;
    bool SetMaterialFxFloat3(uint64_t ownerObjectHandle, uint64_t instanceId, std::string_view key,
                             const DirectX::XMFLOAT3& value) noexcept;
    bool SetMaterialFxFloat4(uint64_t ownerObjectHandle, uint64_t instanceId, std::string_view key,
                             const DirectX::XMFLOAT4& value) noexcept;
    bool SetMaterialFxToggle(uint64_t ownerObjectHandle, uint64_t instanceId, std::string_view key,
                             bool value) noexcept;
    bool TryGetMaterialFxFloat(uint64_t ownerObjectHandle, uint64_t instanceId, std::string_view key,
                               float& outValue) const noexcept;
    bool TryGetMaterialFxFloat2(uint64_t ownerObjectHandle, uint64_t instanceId, std::string_view key,
                                DirectX::XMFLOAT2& outValue) const noexcept;
    bool TryGetMaterialFxFloat3(uint64_t ownerObjectHandle, uint64_t instanceId, std::string_view key,
                                DirectX::XMFLOAT3& outValue) const noexcept;
    bool TryGetMaterialFxFloat4(uint64_t ownerObjectHandle, uint64_t instanceId, std::string_view key,
                                DirectX::XMFLOAT4& outValue) const noexcept;
    bool TryGetMaterialFxToggle(uint64_t ownerObjectHandle, uint64_t instanceId, std::string_view key,
                                bool& outValue) const noexcept;
    const std::string* FindMaterialFxProfileId(uint64_t ownerObjectHandle, uint64_t instanceId) const noexcept;

    // The render cache supplies the current GameObject stable-id domain so
    // presentation ids can probe around real scene ids instead of relying
    // on a potentially colliding hash.
    void PrepareForRender(uint64_t frameIndex, std::span<const uint64_t> occupiedObjectIds) noexcept;
    bool HasRecords() const noexcept { return !records_.empty(); }

    void AcknowledgeRenderFrame() noexcept;

    void Clear() noexcept;

    const std::vector<PresentationModelRecord>& GetFrameRecords() const noexcept { return frameRecords_; }

    const std::vector<uint64_t>& GetRemovedRenderObjectIds() const noexcept { return removedRenderObjectIds_; }

    uint64_t GetRevision() const noexcept { return revision_; }

private:
    struct Key {
        uint64_t ownerObjectHandle = 0u;
        uint64_t instanceId = 0u;

        bool operator==(const Key& rhs) const noexcept {
            return ownerObjectHandle == rhs.ownerObjectHandle && instanceId == rhs.instanceId;
        }
    };

    struct KeyHash {
        size_t operator()(const Key& key) const noexcept;
    };

    PresentationModelRecord* FindRecord(uint64_t ownerObjectHandle, uint64_t instanceId) noexcept;
    const PresentationModelRecord* FindRecord(uint64_t ownerObjectHandle, uint64_t instanceId) const noexcept;
    template <class Mutation>
    bool MutateMaterialFx(uint64_t ownerObjectHandle, uint64_t instanceId, Mutation&& mutation) noexcept {
        try {
            PresentationModelRecord* record = FindRecord(ownerObjectHandle, instanceId);
            if (record == nullptr) {
                return false;
            }
            MaterialFxParameterSet next = record->materialFx;
            if (!std::forward<Mutation>(mutation)(next)) {
                return false;
            }
            if (record->materialFx.HasSameState(next)) {
                return true;
            }
            record->materialFx = std::move(next);
            ++record->payloadRevision;
            NUMERIC::AdvanceNonZero(revision_);
            return true;
        } catch (...) {
            return false;
        }
    }
    uint64_t AllocateRenderObjectId(const std::unordered_set<uint64_t>& occupiedIds) noexcept;

    std::unordered_map<Key, PresentationModelRecord, KeyHash> records_{};
    std::unordered_map<Key, uint64_t, KeyHash> touchedFrames_{};
    std::vector<PresentationModelRecord> frameRecords_{};
    std::vector<uint64_t> removedRenderObjectIds_{};
    std::unordered_set<uint64_t> removedRenderObjectIdSet_{};
    uint64_t nextRenderObjectId_ = 0xA000000000000001ull;
    // 割り当ての番号が一周したか。一周するまでは、割り当てる番号はどの記録の番号より大きく、記録と重ならない。
    bool renderObjectIdsWrapped_ = false;
    uint64_t revision_ = 1u;
};

} // namespace HIKARI
