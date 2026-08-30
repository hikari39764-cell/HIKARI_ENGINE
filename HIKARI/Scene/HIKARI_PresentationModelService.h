#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI {

    struct PresentationModelSubmission {
        uint64_t ownerObjectHandle = 0u;
        uint64_t instanceId = 0u;
        std::string assetGuid{};
        std::string assetPath{};
        std::string materialSetAssetGuid{};
        std::string socketName{};
        Transform3D worldTransform{};
        Transform3D localTransform{};
        bool attachmentEnabled = false;
        bool visible = true;
        bool castShadow = true;
        bool receiveShadow = true;
    };

    struct PresentationModelRecord {
        uint64_t ownerObjectHandle = 0u;
        uint64_t instanceId = 0u;
        uint64_t renderObjectId = 0u;
        std::string assetGuid{};
        std::string assetPath{};
        std::string materialSetAssetGuid{};
        std::string socketName{};
        Transform3D worldTransform{};
        Transform3D localTransform{};
        bool attachmentEnabled = false;
        bool visible = true;
        bool castShadow = true;
        bool receiveShadow = true;
    };

    // World-owned render-only model submissions. These records never become
    // GameObjects, ModelComponents, animation owners, or physics bodies.
    class PresentationModelService final {
    public:
        PresentationModelService() = default;
        ~PresentationModelService() = default;

        PresentationModelService(const PresentationModelService&) = delete;
        PresentationModelService& operator=(
            const PresentationModelService&) = delete;

        bool Submit(
            const PresentationModelSubmission& submission,
            uint64_t frameIndex) noexcept;
        bool Remove(
            uint64_t ownerObjectHandle,
            uint64_t instanceId) noexcept;

        // The render cache supplies the current GameObject stable-id domain so
        // presentation ids can probe around real scene ids instead of relying
        // on a potentially colliding hash.
        void PrepareForRender(
            uint64_t frameIndex,
            std::span<const uint64_t> occupiedObjectIds) noexcept;
        bool HasRecords() const noexcept { return !records_.empty(); }

        void AcknowledgeRenderFrame() noexcept;

        void Clear() noexcept;

        const std::vector<PresentationModelRecord>&
            GetFrameRecords() const noexcept {
            return frameRecords_;
        }

        const std::vector<uint64_t>&
            GetRemovedRenderObjectIds() const noexcept {
            return removedRenderObjectIds_;
        }

        uint64_t GetRevision() const noexcept {
            return revision_;
        }

    private:
        struct Key {
            uint64_t ownerObjectHandle = 0u;
            uint64_t instanceId = 0u;

            bool operator==(const Key& rhs) const noexcept {
                return ownerObjectHandle == rhs.ownerObjectHandle &&
                    instanceId == rhs.instanceId;
            }
        };

        struct KeyHash {
            size_t operator()(const Key& key) const noexcept;
        };

        void AdvanceRevision() noexcept;
        uint64_t AllocateRenderObjectId(
            const std::unordered_set<uint64_t>& occupiedIds) noexcept;

        std::unordered_map<Key, PresentationModelRecord, KeyHash> records_{};
        std::unordered_map<Key, uint64_t, KeyHash> touchedFrames_{};
        std::vector<PresentationModelRecord> frameRecords_{};
        std::vector<uint64_t> removedRenderObjectIds_{};
        std::unordered_set<uint64_t> removedRenderObjectIdSet_{};
        uint64_t nextRenderObjectId_ = 0xA000000000000001ull;
        uint64_t revision_ = 1u;
    };

} // namespace HIKARI
