#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "HIKARI/Project/SceneCaptureTypes.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {
    class Material;
}

namespace HIKARI::SCENE_CAPTURE {

    struct SceneCaptureRequest {
        GameProjectSceneCaptureHandle handle{};
        RuntimeObjectHandle cameraObject{};
        uint32_t width = 960u;
        uint32_t height = 640u;
        uint32_t flags = 0u;
        // Stable renderer resource slot owned by this World service. This is
        // intentionally not part of the public GameProject ABI.
        uint32_t renderSlot = 0u;

        bool IsEnabled() const noexcept {
            return (flags & GameProjectSceneCaptureFlags(
                GameProjectSceneCaptureFlag::Enabled)) != 0u;
        }
    };

    struct SceneCaptureMaterialBinding {
        GameProjectSceneCaptureHandle capture{};
        RuntimeObjectHandle modelObject{};
        std::string materialSlotName{};
        uint32_t fallbackMaterialIndex = kGameProjectInvalidMaterialIndex;
        uint32_t flags = 0u;
        GameProjectSceneCaptureSourceRect sourceRect{};
        uint32_t quarterTurns = 0u;
        uint32_t resolvedMaterialIndex = kGameProjectInvalidMaterialIndex;
        RENDER3D::TextureResourceHandle appliedTexture{};
        const Material* appliedMaterial = nullptr;
        bool applied = false;
    };

    // World-scoped CPU authority for a bounded set of secondary captures. GPU
    // allocation and rendering stay in Render3D; texture handles stored here
    // are borrowed from that owner and never released by this service.
    class SceneCaptureService {
    public:
        GameProjectSceneCaptureHandle Create(
            RuntimeObjectHandle cameraObject,
            const GameProjectSceneCaptureDesc& desc) noexcept {
            if (!cameraObject.IsValid()) {
                return {};
            }
            CaptureState* state = nullptr;
            uint32_t renderSlot = 0u;
            for (uint32_t index = 0u;
                 index < static_cast<uint32_t>(captures_.size());
                 ++index) {
                std::optional<CaptureState>& candidate = captures_[index];
                if (!candidate.has_value()) {
                    candidate.emplace();
                    state = &*candidate;
                    renderSlot = index;
                    break;
                }
            }
            if (state == nullptr) {
                return {};
            }
            GameProjectSceneCaptureHandle handle{
                nextHandleValue_++, epoch_ };
            if (nextHandleValue_ == 0u) {
                nextHandleValue_ = 1u;
            }
            state->request = SceneCaptureRequest{
                handle,
                cameraObject,
                desc.width,
                desc.height,
                desc.flags,
                renderSlot,
            };
            state->snapshot = {};
            state->snapshot.capture = handle;
            state->snapshot.status = state->request.IsEnabled()
                ? GameProjectSceneCaptureStatus::Pending
                : GameProjectSceneCaptureStatus::Disabled;
            state->targetRebuildPending = true;
            return handle;
        }

        bool Update(
            GameProjectSceneCaptureHandle capture,
            RuntimeObjectHandle cameraObject,
            const GameProjectSceneCaptureDesc& desc) noexcept {
            CaptureState* state = Find(capture);
            if (state == nullptr || !cameraObject.IsValid()) {
                return false;
            }
            const bool targetChanged =
                state->request.width != desc.width ||
                state->request.height != desc.height;
            const bool cameraChanged =
                state->request.cameraObject != cameraObject;
            state->request.cameraObject = cameraObject;
            state->request.width = desc.width;
            state->request.height = desc.height;
            state->request.flags = desc.flags;
            state->targetRebuildPending =
                state->targetRebuildPending || targetChanged;
            if (state->request.IsEnabled() &&
                state->targetRebuildPending) {
                // The material bridge observes the missing publication before
                // Render3D retires the old target ring in this frame.
                state->outputTexture = {};
            }
            if (!state->request.IsEnabled()) {
                state->snapshot.status =
                    GameProjectSceneCaptureStatus::Disabled;
                state->snapshot.failure =
                    GameProjectSceneCaptureFailure::None;
            }
            else if (targetChanged || cameraChanged ||
                     state->snapshot.status ==
                        GameProjectSceneCaptureStatus::Disabled) {
                state->snapshot.status =
                    GameProjectSceneCaptureStatus::Pending;
                state->snapshot.failure =
                    GameProjectSceneCaptureFailure::None;
            }
            return true;
        }

        bool Destroy(GameProjectSceneCaptureHandle capture) {
            std::optional<CaptureState>* slot = FindSlot(capture);
            if (slot == nullptr) {
                return false;
            }
            RetireBindings(capture);
            retiredCaptureTargetSlots_.push_back(
                (*slot)->request.renderSlot);
            slot->reset();
            return true;
        }

        bool BindMaterial(SceneCaptureMaterialBinding binding) {
            if (!Matches(binding.capture) ||
                !binding.modelObject.IsValid() ||
                (binding.materialSlotName.empty() &&
                 binding.fallbackMaterialIndex ==
                    kGameProjectInvalidMaterialIndex)) {
                return false;
            }
            const auto sameDestination = [&binding](
                const SceneCaptureMaterialBinding& existing) {
                return existing.modelObject == binding.modelObject &&
                    existing.materialSlotName == binding.materialSlotName &&
                    existing.fallbackMaterialIndex ==
                        binding.fallbackMaterialIndex;
            };
            const auto found = std::find_if(
                bindings_.begin(),
                bindings_.end(),
                sameDestination);
            if (found != bindings_.end()) {
                retiredBindings_.push_back(std::move(*found));
                *found = std::move(binding);
            }
            else {
                bindings_.push_back(std::move(binding));
            }
            return true;
        }

        bool ClearMaterialBinding(
            GameProjectSceneCaptureHandle capture,
            RuntimeObjectHandle modelObject) {
            if (!Matches(capture)) {
                return false;
            }
            bool removed = false;
            auto binding = bindings_.begin();
            while (binding != bindings_.end()) {
                if (binding->capture == capture &&
                    binding->modelObject == modelObject) {
                    retiredBindings_.push_back(std::move(*binding));
                    binding = bindings_.erase(binding);
                    removed = true;
                }
                else {
                    ++binding;
                }
            }
            return removed;
        }

        std::vector<SceneCaptureRequest> Requests() const {
            std::vector<SceneCaptureRequest> result{};
            result.reserve(captures_.size());
            for (const std::optional<CaptureState>& state : captures_) {
                if (state.has_value()) {
                    result.push_back(state->request);
                }
            }
            return result;
        }

        std::vector<SceneCaptureMaterialBinding>& Bindings() noexcept {
            return bindings_;
        }

        const std::vector<SceneCaptureMaterialBinding>&
            Bindings() const noexcept {
            return bindings_;
        }

        std::vector<SceneCaptureMaterialBinding>
            ConsumeRetiredBindings() noexcept {
            std::vector<SceneCaptureMaterialBinding> result =
                std::move(retiredBindings_);
            retiredBindings_.clear();
            return result;
        }

        std::vector<uint32_t>
            ConsumeRetiredCaptureTargets() noexcept {
            std::vector<uint32_t> result =
                std::move(retiredCaptureTargetSlots_);
            retiredCaptureTargetSlots_.clear();
            return result;
        }

        void PublishReady(
            GameProjectSceneCaptureHandle capture,
            RENDER3D::TextureResourceHandle texture,
            uint32_t width,
            uint32_t height,
            uint64_t frameIndex,
            uint64_t outputRevision) noexcept {
            CaptureState* state = Find(capture);
            if (state == nullptr || !state->request.IsEnabled() ||
                !texture || outputRevision == 0u) {
                return;
            }
            state->outputTexture = texture;
            state->snapshot.capture = state->request.handle;
            state->snapshot.status = GameProjectSceneCaptureStatus::Ready;
            state->snapshot.failure = GameProjectSceneCaptureFailure::None;
            state->snapshot.width = width;
            state->snapshot.height = height;
            state->snapshot.renderedFrameIndex = frameIndex;
            state->snapshot.outputRevision = outputRevision;
            state->targetRebuildPending = false;
        }

        void PublishFailure(
            GameProjectSceneCaptureHandle capture,
            GameProjectSceneCaptureFailure failure) noexcept {
            CaptureState* state = Find(capture);
            if (state == nullptr) {
                return;
            }
            state->snapshot.capture = state->request.handle;
            state->snapshot.status = GameProjectSceneCaptureStatus::Failed;
            state->snapshot.failure = failure;
        }

        RENDER3D::TextureResourceHandle OutputTexture(
            GameProjectSceneCaptureHandle capture) const noexcept {
            const CaptureState* state = Find(capture);
            return state != nullptr
                ? state->outputTexture
                : RENDER3D::TextureResourceHandle{};
        }

        bool TryGetSnapshot(
            GameProjectSceneCaptureHandle capture,
            GameProjectSceneCaptureSnapshot& outSnapshot) const noexcept {
            outSnapshot = {};
            const CaptureState* state = Find(capture);
            if (state == nullptr) {
                return false;
            }
            outSnapshot = state->snapshot;
            return true;
        }

        void Reset() noexcept {
            captures_ = {};
            bindings_.clear();
            retiredBindings_.clear();
            retiredCaptureTargetSlots_.clear();
            ++epoch_;
            if (epoch_ == 0u) {
                epoch_ = 1u;
            }
            nextHandleValue_ = 1u;
        }

    private:
        struct CaptureState {
            SceneCaptureRequest request{};
            RENDER3D::TextureResourceHandle outputTexture{};
            GameProjectSceneCaptureSnapshot snapshot{};
            bool targetRebuildPending = false;
        };

        CaptureState* Find(
            GameProjectSceneCaptureHandle capture) noexcept {
            std::optional<CaptureState>* slot = FindSlot(capture);
            return slot != nullptr ? &**slot : nullptr;
        }

        const CaptureState* Find(
            GameProjectSceneCaptureHandle capture) const noexcept {
            for (const std::optional<CaptureState>& state : captures_) {
                if (state.has_value() && capture.IsValid() &&
                    state->request.handle == capture) {
                    return &*state;
                }
            }
            return nullptr;
        }

        std::optional<CaptureState>* FindSlot(
            GameProjectSceneCaptureHandle capture) noexcept {
            for (std::optional<CaptureState>& state : captures_) {
                if (state.has_value() && capture.IsValid() &&
                    state->request.handle == capture) {
                    return &state;
                }
            }
            return nullptr;
        }

        bool Matches(
            GameProjectSceneCaptureHandle capture) const noexcept {
            return Find(capture) != nullptr;
        }

        void RetireBindings(GameProjectSceneCaptureHandle capture) {
            auto binding = bindings_.begin();
            while (binding != bindings_.end()) {
                if (binding->capture == capture) {
                    retiredBindings_.push_back(std::move(*binding));
                    binding = bindings_.erase(binding);
                }
                else {
                    ++binding;
                }
            }
        }

        std::array<
            std::optional<CaptureState>,
            kGameProjectSceneCaptureMaximumActiveCount> captures_{};
        std::vector<SceneCaptureMaterialBinding> bindings_{};
        std::vector<SceneCaptureMaterialBinding> retiredBindings_{};
        std::vector<uint32_t> retiredCaptureTargetSlots_{};
        uint64_t epoch_ = 1u;
        uint64_t nextHandleValue_ = 1u;
    };

} // namespace HIKARI::SCENE_CAPTURE
