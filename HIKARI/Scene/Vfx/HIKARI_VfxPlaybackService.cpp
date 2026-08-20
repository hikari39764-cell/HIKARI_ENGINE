#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackSchedule.h"
#include "Scene/Vfx/HIKARI_VfxSpecialOutputBridge.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace HIKARI::VFX {
    namespace {
        using namespace RENDER3D::PARTICLES;

        constexpr size_t kInvalidInstanceIndex =
            (std::numeric_limits<size_t>::max)();
        constexpr uint64_t kInvalidSeekBatchFrame =
            (std::numeric_limits<uint64_t>::max)();
        constexpr float kMaximumSpawnRateMultiplier = 1024.0f;
        constexpr float kMaximumMotionMultiplier = 1024.0f;
        constexpr float kMaximumHdrColorMultiplier = 64.0f;

        uint64_t AllocatePlaybackNamespace() noexcept {
            static std::atomic_uint64_t nextNamespace{ 1u };
            uint64_t value = nextNamespace.fetch_add(
                1u,
                std::memory_order_relaxed);
            if (value == 0u) {
                value = nextNamespace.fetch_add(
                    1u,
                    std::memory_order_relaxed);
            }
            return value;
        }

        bool IsCompatible(
            const VfxValue& value,
            VfxValueType type) noexcept {

            return value.type == type;
        }

        float ScalarValue(const VfxValue& value) noexcept {
            return value.type == VfxValueType::Int
                ? static_cast<float>(value.integer)
                : value.numeric.x;
        }

        float ClampFinite(
            float value,
            float minimum,
            float maximum,
            float fallback) noexcept {

            return std::isfinite(value)
                ? std::clamp(value, minimum, maximum)
                : fallback;
        }

        MATH::Vec4 ClampColorMultiplier(
            const MATH::Vec4& value,
            float fallback) noexcept {

            return {
                ClampFinite(
                    value.x, 0.0f,
                    kMaximumHdrColorMultiplier, fallback),
                ClampFinite(
                    value.y, 0.0f,
                    kMaximumHdrColorMultiplier, fallback),
                ClampFinite(
                    value.z, 0.0f,
                    kMaximumHdrColorMultiplier, fallback),
                ClampFinite(
                    value.w, 0.0f,
                    kMaximumHdrColorMultiplier, fallback),
            };
        }

        void ApplyRuntimeLink(
            ParticleEmitterRuntimeOverrides& overrides,
            const VfxProgramInputLink& link,
            const VfxValue& value) noexcept {

            std::byte* destination =
                reinterpret_cast<std::byte*>(&overrides) + link.byteOffset;
            switch (link.storageType) {
            case ParticleRuntimePropertyType::Bool: {
                uint32_t current = 0u;
                std::memcpy(&current, destination, sizeof(current));
                current = current != 0u && value.boolean ? 1u : 0u;
                std::memcpy(destination, &current, sizeof(current));
                break;
            }
            case ParticleRuntimePropertyType::Float: {
                float current = 1.0f;
                std::memcpy(&current, destination, sizeof(current));
                const float mapped = ScalarValue(value) *
                    link.scale + link.bias;
                current *= mapped;
                std::memcpy(destination, &current, sizeof(current));
                break;
            }
            case ParticleRuntimePropertyType::Float4: {
                MATH::Vec4 current{};
                std::memcpy(&current, destination, sizeof(current));
                const MATH::Vec4 mapped{
                    value.numeric.x * link.scale + link.bias,
                    value.numeric.y * link.scale + link.bias,
                    value.numeric.z * link.scale + link.bias,
                    value.numeric.w * link.scale + link.bias,
                };
                current = {
                    current.x * mapped.x,
                    current.y * mapped.y,
                    current.z * mapped.z,
                    current.w * mapped.w,
                };
                std::memcpy(destination, &current, sizeof(current));
                break;
            }
            }
        }

        void ClampRuntimeOverrides(
            ParticleEmitterRuntimeOverrides& overrides) noexcept {

            overrides.enabled = overrides.enabled != 0u ? 1u : 0u;
            overrides.spawnRateMultiplier = ClampFinite(
                overrides.spawnRateMultiplier,
                0.0f,
                kMaximumSpawnRateMultiplier,
                0.0f);
            overrides.radialSpeedMultiplier = ClampFinite(
                overrides.radialSpeedMultiplier,
                0.0f,
                kMaximumMotionMultiplier,
                1.0f);
            overrides.scaleMultiplier = ClampFinite(
                overrides.scaleMultiplier,
                0.0f,
                kMaximumMotionMultiplier,
                1.0f);
            overrides.gravityMultiplier = ClampFinite(
                overrides.gravityMultiplier,
                -kMaximumMotionMultiplier,
                kMaximumMotionMultiplier,
                1.0f);
            overrides.colorMultiplier = ClampColorMultiplier(
                overrides.colorMultiplier,
                1.0f);
        }

        bool RequiresLiveOwnerAttachment(
            const VfxProgram& program) noexcept {

            return std::any_of(
                program.emitters.begin(),
                program.emitters.end(),
                [](const VfxProgramEmitter& emitter) {
                    return emitter.attachment.mode ==
                            VfxAttachmentMode::Owner ||
                        emitter.attachment.mode ==
                            VfxAttachmentMode::OwnerSocket;
                });
        }

    }

    struct VfxPlaybackService::HandleSlot {
        size_t instanceIndex = kInvalidInstanceIndex;
        uint32_t generation = 1u;
    };

    struct VfxPlaybackService::EmitterState
        : PLAYBACK::EmitterScheduleState {
        EmitterState() = default;
    };

    struct VfxPlaybackService::Instance {
        VfxPlaybackHandle handle{};
        AssetGuid assetGuid{};
        std::shared_ptr<const VfxProgram> program{};
        RuntimeObjectHandle ownerObject{};
        VfxWorldTransform transform{};
        bool loop = false;
        uint32_t randomSeed = 0u;
        float elapsedSeconds = 0.0f;
        std::vector<VfxValue> inputValues{};
        std::vector<EmitterState> emitterStates{};
        std::vector<VfxWorldTransform> emitterTransforms{};
        std::vector<uint8_t> transformResolved{};
        std::vector<uint32_t> transformStack{};
    };

    VfxPlaybackService::VfxPlaybackService(uint64_t playbackNamespace)
        : playbackNamespace_(
            playbackNamespace != 0u
                ? playbackNamespace
                : AllocatePlaybackNamespace()) {
        renderInput_.sourceIdentity = playbackNamespace_;
    }

    VfxPlaybackService::~VfxPlaybackService() = default;

    void VfxPlaybackService::SetProgramResolver(
        VfxProgramResolver resolver) {

        resolver_ = std::move(resolver);
    }

    void VfxPlaybackService::SetAttachmentResolver(
        VfxAttachmentResolver resolver) {

        attachmentResolver_ = std::move(resolver);
    }

    void VfxPlaybackService::SetReliableRenderDeliveryEnabled(
        bool enabled) noexcept {

        reliableRenderDeliveryEnabled_ = enabled;
        if (!enabled) {
            pendingRenderDeliverySequence_ = 0u;
            pendingRenderDeltaSeconds_ = 0.0f;
        }
    }

    uint64_t VfxPlaybackService::GetPlaybackNamespace() const noexcept {
        return playbackNamespace_;
    }

    bool VfxPlaybackService::ResolveSpecialOutput(
        const AssetGuid& assetGuid,
        std::string_view outputId,
        VfxSpecialOutputType expectedType,
        VfxSpecialOutputData& outOutput) const {

        outOutput = {};
        if (!assetGuid.IsValid() || outputId.empty() || !resolver_) {
            return false;
        }
        std::string error{};
        const std::shared_ptr<const VfxProgram> program =
            resolver_(assetGuid, error);
        if (!program) {
            return false;
        }
        const VfxSpecialOutputData* output = FindVfxSpecialOutput(
            *program,
            outputId,
            expectedType);
        if (output == nullptr || !PLAYBACK::HasUniqueSpecialOutputIds(*program)) {
            return false;
        }
        outOutput = *output;
        return true;
    }

    VfxPlayResult VfxPlaybackService::PlayDetailed(
        const VfxPlayRequest& request) {

        VfxPlayResult result{};
        std::shared_ptr<const VfxProgram> program = request.program;
        if (!program && resolver_ && request.assetGuid.IsValid()) {
            std::string error{};
            program = resolver_(request.assetGuid, error);
            if (!program) {
                result.diagnostics.push_back({
                    VfxDiagnosticSeverity::Error,
                    "Vfx.ProgramResolveFailed",
                    error.empty()
                        ? "VFX program could not be resolved"
                        : std::move(error),
                });
                return result;
            }
        }
        // Keep the runtime contract explicit for cooked/direct programs. The
        // asset validator also rejects this shape, but PlayDetailed must
        // report the stable output-id violation before generic validation so
        // callers can diagnose a bad cooked table deterministically.
        if (program && !PLAYBACK::HasUniqueSpecialOutputIds(*program)) {
            result.diagnostics.push_back({
                VfxDiagnosticSeverity::Error,
                "Vfx.DuplicateSpecialOutputId",
                "VFX special output ids must be globally unique",
            });
            return result;
        }
        std::string validationError{};
        if (!program ||
            !ValidateVfxProgram(*program, validationError)) {
            result.diagnostics.push_back({
                VfxDiagnosticSeverity::Error,
                "Vfx.InvalidProgram",
                validationError.empty()
                    ? "VFX play request has no compiled program"
                    : std::move(validationError),
            });
            return result;
        }
        if (!PLAYBACK::HasOrderedBurstTimeline(*program)) {
            result.diagnostics.push_back({
                VfxDiagnosticSeverity::Error,
                "Vfx.InvalidBurstTimelineOrder",
                "VFX burst timeline must be sorted by ascending local time",
            });
            return result;
        }

        const size_t emitterCount = program->emitters.size();
        const size_t pendingRetirementCount =
            pendingRetiredEmitters_.size();
        const bool outstandingEmitterBudgetInvalid =
            pendingRetirementCount >
                kMaximumActiveVfxPlaybackEmitters ||
            admissionStats_.activeEmitterInstances >
                kMaximumActiveVfxPlaybackEmitters -
                    pendingRetirementCount;
        const size_t outstandingEmitterCount =
            outstandingEmitterBudgetInvalid
                ? kMaximumActiveVfxPlaybackEmitters
                : pendingRetirementCount +
                    admissionStats_.activeEmitterInstances;
        if (instances_.size() >=
                kMaximumActiveVfxPlaybackInstances ||
            emitterCount > kMaximumActiveVfxPlaybackEmitters ||
            outstandingEmitterBudgetInvalid ||
            emitterCount > kMaximumActiveVfxPlaybackEmitters -
                outstandingEmitterCount) {
            ++admissionStats_.budgetRejections;
            result.diagnostics.push_back({
                VfxDiagnosticSeverity::Error,
                "Vfx.PlaybackBudgetExceeded",
                "VFX playback active or pending-retirement emitter "
                "budget is exhausted",
            });
            return result;
        }

        Instance instance{};
        instance.assetGuid = request.assetGuid;
        instance.program = std::move(program);
        instance.ownerObject = request.ownerObject;
        instance.transform = request.transform;
        instance.loop = request.loopOverride.value_or(instance.program->loop);
        instance.randomSeed = request.randomSeed;
        instance.inputValues.reserve(instance.program->inputs.size());
        for (const VfxProgramInput& input : instance.program->inputs) {
            instance.inputValues.push_back(input.defaultValue);
        }
        instance.emitterStates.resize(instance.program->emitters.size());
        instance.emitterTransforms.resize(
            instance.program->emitters.size());
        instance.transformResolved.resize(
            instance.program->emitters.size());
        instance.transformStack.reserve(
            instance.program->emitters.size());
        if (!ApplyParameters(
                instance,
                request.parameters,
                &result.diagnostics)) {
            result.diagnostics.push_back({
                VfxDiagnosticSeverity::Error,
                "Vfx.InvalidParameterOverrides",
                "VFX play request contains an unknown or type-mismatched input",
            });
            return result;
        }

        const size_t instanceIndex = instances_.size();
        instance.handle = AllocateHandle(instanceIndex);
        if (!instance.handle.IsValid()) {
            result.diagnostics.push_back({
                VfxDiagnosticSeverity::Error,
                "Vfx.HandleExhausted",
                "VFX playback handle capacity is exhausted",
            });
            return result;
        }
        result.handle = instance.handle;
        instances_.push_back(std::move(instance));
        ++admissionStats_.acceptedPlayRequests;
        admissionStats_.activeInstances = instances_.size();
        admissionStats_.activeEmitterInstances += emitterCount;
        return result;
    }

    bool VfxPlaybackService::Stop(VfxPlaybackHandle handle) {
        const Instance* instance = Find(handle);
        if (instance == nullptr) return false;
        RetireInstanceAt(handleSlots_[handle.slot].instanceIndex);
        return true;
    }

    size_t VfxPlaybackService::StopByOwner(
        RuntimeObjectHandle ownerObject) {

        size_t stopped = 0u;
        for (size_t index = instances_.size(); index-- > 0u;) {
            if (instances_[index].ownerObject == ownerObject) {
                RetireInstanceAt(index);
                ++stopped;
            }
        }
        return stopped;
    }

    bool VfxPlaybackService::SetTransform(
        VfxPlaybackHandle handle,
        const VfxWorldTransform& transform) {

        Instance* instance = Find(handle);
        if (instance == nullptr) return false;
        instance->transform = transform;
        return true;
    }

    bool VfxPlaybackService::SetParameters(
        VfxPlaybackHandle handle,
        const std::vector<VfxParameterOverride>& parameters,
        std::vector<VfxPlaybackDiagnostic>* outDiagnostics) {

        Instance* instance = Find(handle);
        return instance != nullptr &&
            ApplyParameters(*instance, parameters, outDiagnostics);
    }

    VfxSeekCapacity VfxPlaybackService::GetSeekCapacity(
        VfxPlaybackHandle handle,
        uint64_t frameIndex) const noexcept {

        VfxSeekCapacity capacity{};
        const Instance* instance = Find(handle);
        if (instance == nullptr) return capacity;

        capacity.handleActive = true;
        capacity.currentSeconds = instance->elapsedSeconds;
        capacity.emitterCount = static_cast<uint32_t>((std::min)(
            instance->program->emitters.size(),
            static_cast<size_t>(
                (std::numeric_limits<uint32_t>::max)())));
        const size_t usedSubmissions = seekBatchFrameIndex_ == frameIndex
            ? (std::min)(
                renderInput_.emitters.size(),
                static_cast<size_t>(
                    RENDER3D::PARTICLES::
                        kMaximumParticleEmitterSubmissionsPerFrame))
            : 0u;
        capacity.remainingSubmissions =
            RENDER3D::PARTICLES::
                kMaximumParticleEmitterSubmissionsPerFrame -
            static_cast<uint32_t>(usedSubmissions);
        if (capacity.emitterCount != 0u) {
            capacity.maximumSteps = capacity.remainingSubmissions /
                capacity.emitterCount;
        }
        return capacity;
    }

    VfxSeekResult VfxPlaybackService::Seek(
        VfxPlaybackHandle handle,
        uint64_t frameIndex,
        float targetSeconds,
        float fixedStepSeconds,
        uint32_t maximumSteps) {

        VfxSeekResult result{};
        if (reliableRenderDeliveryEnabled_ &&
            pendingRenderDeliverySequence_ != 0u) {
            result.diagnostics.push_back({
                VfxDiagnosticSeverity::Warning,
                "Vfx.RenderDeliveryPending",
                "VFX seek is waiting for the current render input receipt",
            });
            return result;
        }
        Instance* instance = Find(handle);
        if (instance == nullptr) {
            result.diagnostics.push_back({
                VfxDiagnosticSeverity::Error,
                "Vfx.InvalidHandle",
                "VFX seek handle is not active",
            });
            return result;
        }
        const float maximumTarget = instance->loop
            ? instance->program->durationSeconds
            : instance->program->durationSeconds +
                instance->program->maximumParticleLifetimeSeconds;
        if (!std::isfinite(targetSeconds) || targetSeconds < 0.0f ||
            targetSeconds > maximumTarget ||
            !std::isfinite(fixedStepSeconds) ||
            fixedStepSeconds <= 0.0f) {
            result.diagnostics.push_back({
                VfxDiagnosticSeverity::Error,
                "Vfx.InvalidSeek",
                "VFX seek target or fixed step is outside its valid range",
            });
            return result;
        }
        result.accepted = true;
        maximumSteps = std::clamp(
            maximumSteps,
            1u,
            kMaximumVfxSeekSubsteps);
        fixedStepSeconds = std::clamp(
            fixedStepSeconds,
            1.0f / 1000.0f,
            1.0f);

        const VfxSeekCapacity seekCapacity = GetSeekCapacity(
            handle,
            frameIndex);
        const uint32_t executableSteps = (std::min)(
            maximumSteps,
            seekCapacity.maximumSteps);
        if (executableSteps == 0u) {
            result.currentSeconds = instance->elapsedSeconds;
            result.diagnostics.push_back({
                VfxDiagnosticSeverity::Warning,
                "Vfx.SeekSubmissionBudgetExhausted",
                "VFX seek did not advance because the per-frame "
                "submission budget is exhausted",
            });
            return result;
        }

        if (seekBatchFrameIndex_ != frameIndex) {
            const bool preserveStagedRetirements =
                renderInput_.frameIndex == frameIndex;
            renderInput_.frameIndex = frameIndex;
            renderInput_.emitters.clear();
            if (!preserveStagedRetirements) {
                renderInput_.retiredEmitters.clear();
            }
            renderInput_.retiredEmitters.insert(
                renderInput_.retiredEmitters.end(),
                pendingRetiredEmitters_.begin(),
                pendingRetiredEmitters_.end());
            pendingRetiredEmitters_.clear();
            seekBatchFrameIndex_ = frameIndex;
            nextSeekSimulationOrdinal_ = 0u;
        }

        constexpr float kSeekEpsilon = 1.0e-6f;
        const bool rewind = targetSeconds + kSeekEpsilon <
            instance->elapsedSeconds;
        if (rewind || targetSeconds <= kSeekEpsilon) {
            ResetInstanceSimulation(*instance);
        }

        const size_t batchStart = renderInput_.emitters.size();
        if (targetSeconds <= kSeekEpsilon) {
            (void)AppendInstanceStep(
                *instance,
                0.0f,
                nextSeekSimulationOrdinal_++,
                false,
                false);
        } else {
            while (result.executedSteps < executableSteps &&
                instance->elapsedSeconds + kSeekEpsilon < targetSeconds) {
                const float step = (std::min)(
                    fixedStepSeconds,
                    targetSeconds - instance->elapsedSeconds);
                (void)AppendInstanceStep(
                    *instance,
                    step,
                    nextSeekSimulationOrdinal_++,
                    false,
                    false);
                ++result.executedSteps;
            }
            if (renderInput_.emitters.size() == batchStart) {
                (void)AppendInstanceStep(
                    *instance,
                    0.0f,
                    nextSeekSimulationOrdinal_++,
                    false,
                    false);
            }
        }

        const uint32_t finalOrdinal = nextSeekSimulationOrdinal_ - 1u;
        for (size_t index = batchStart;
            index < renderInput_.emitters.size();
            ++index) {
            renderInput_.emitters[index].drawAfterSimulation =
                renderInput_.emitters[index].simulationOrdinal ==
                finalOrdinal;
        }
        result.complete = std::abs(
            instance->elapsedSeconds - targetSeconds) <= kSeekEpsilon;
        if (result.complete) {
            instance->elapsedSeconds = targetSeconds;
        } else if (executableSteps < maximumSteps &&
            result.executedSteps == executableSteps) {
            result.diagnostics.push_back({
                VfxDiagnosticSeverity::Warning,
                "Vfx.SeekSubmissionBudgetExhausted",
                "VFX seek reached the per-frame submission budget; "
                "use GetSeekCapacity to select a larger fixed step",
            });
        }
        result.currentSeconds = instance->elapsedSeconds;
        PublishRenderInputDelivery();
        return result;
    }

    void VfxPlaybackService::Update(
        uint64_t frameIndex,
        float deltaSeconds) {

        if (hasUpdatedFrame_ && lastUpdateFrame_ == frameIndex) {
            return;
        }
        hasUpdatedFrame_ = true;
        lastUpdateFrame_ = frameIndex;
        deltaSeconds = std::isfinite(deltaSeconds)
            ? std::clamp(deltaSeconds, 0.0f, 0.25f)
            : 0.0f;
        if (reliableRenderDeliveryEnabled_ &&
            pendingRenderDeliverySequence_ != 0u) {
            pendingRenderDeltaSeconds_ = (std::min)(
                pendingRenderDeltaSeconds_ + deltaSeconds,
                0.25f);
            return;
        }
        deltaSeconds = (std::min)(
            deltaSeconds + pendingRenderDeltaSeconds_,
            0.25f);
        pendingRenderDeltaSeconds_ = 0.0f;
        seekBatchFrameIndex_ = kInvalidSeekBatchFrame;
        nextSeekSimulationOrdinal_ = 0u;
        renderInput_.frameIndex = frameIndex;
        renderInput_.emitters.clear();
        renderInput_.retiredEmitters.clear();
        renderInput_.retiredEmitters.swap(pendingRetiredEmitters_);

        for (size_t index = instances_.size(); index-- > 0u;) {
            const Instance& instance = instances_[index];
            if (attachmentResolver_ && instance.ownerObject.IsValid() &&
                RequiresLiveOwnerAttachment(*instance.program)) {
                VfxWorldTransform ownerTransform{};
                if (!attachmentResolver_(
                        instance.ownerObject,
                        {},
                        ownerTransform)) {
                    // An Owner/OwnerSocket effect may tolerate a transient
                    // pose or socket miss, but the empty-socket lookup is the
                    // authoritative owner-liveness probe. Retire the complete
                    // instance before its last captured transform can become
                    // a permanent looping ghost.
                    RetireInstanceAt(index);
                    continue;
                }
            }
            if (!instance.loop && instance.elapsedSeconds >=
                instance.program->durationSeconds +
                instance.program->maximumParticleLifetimeSeconds) {
                RetireInstanceAt(index);
            }
        }
        renderInput_.retiredEmitters.insert(
            renderInput_.retiredEmitters.end(),
            pendingRetiredEmitters_.begin(),
            pendingRetiredEmitters_.end());
        pendingRetiredEmitters_.clear();

        for (Instance& instance : instances_) {
            (void)AppendInstanceStep(
                instance,
                deltaSeconds,
                0u,
                true,
                true);
        }
        PublishRenderInputDelivery();
    }

    const ParticleRenderInput&
        VfxPlaybackService::GetRenderInput() const noexcept {

        return renderInput_;
    }

    bool VfxPlaybackService::AcknowledgeRenderInput(
        ParticleRenderDeliveryToken token) noexcept {

        if (!reliableRenderDeliveryEnabled_ || !token.IsValid() ||
            token.sourceIdentity != playbackNamespace_ ||
            token.sequence != pendingRenderDeliverySequence_) {
            return false;
        }
        pendingRenderDeliverySequence_ = 0u;
        return true;
    }

    bool VfxPlaybackService::HasPendingRenderInput() const noexcept {
        return reliableRenderDeliveryEnabled_ &&
            pendingRenderDeliverySequence_ != 0u;
    }

    bool VfxPlaybackService::IsActive(
        VfxPlaybackHandle handle) const noexcept {

        return Find(handle) != nullptr;
    }

    size_t VfxPlaybackService::GetActiveInstanceCount() const noexcept {
        return instances_.size();
    }

    const VfxPlaybackAdmissionStats&
        VfxPlaybackService::GetAdmissionStats() const noexcept {

        return admissionStats_;
    }

    void VfxPlaybackService::Reset() {
        for (const Instance& instance : instances_) {
            RetireEmitterKeys(instance);
        }
        for (const Instance& instance : instances_) {
            ReleaseHandle(instance.handle);
        }
        instances_.clear();
        admissionStats_ = {};
        renderInput_.emitters.clear();
        renderInput_.retiredEmitters.clear();
        renderInput_.deliverySequence = 0u;
        pendingRenderDeliverySequence_ = 0u;
        pendingRenderDeltaSeconds_ = 0.0f;
        seekBatchFrameIndex_ = kInvalidSeekBatchFrame;
        nextSeekSimulationOrdinal_ = 0u;
    }

    void VfxPlaybackService::PublishRenderInputDelivery() noexcept {
        if (renderInput_.emitters.empty() &&
            renderInput_.retiredEmitters.empty()) {
            renderInput_.deliverySequence = 0u;
            return;
        }
        ++nextRenderDeliverySequence_;
        if (nextRenderDeliverySequence_ == 0u) {
            nextRenderDeliverySequence_ = 1u;
        }
        renderInput_.deliverySequence = nextRenderDeliverySequence_;
        if (reliableRenderDeliveryEnabled_) {
            pendingRenderDeliverySequence_ = nextRenderDeliverySequence_;
        }
    }

    VfxPlaybackHandle VfxPlaybackService::AllocateHandle(
        size_t instanceIndex) {

        if (!freeHandleSlots_.empty()) {
            const uint32_t slotIndex = freeHandleSlots_.back();
            freeHandleSlots_.pop_back();
            HandleSlot& slot = handleSlots_[slotIndex];
            slot.instanceIndex = instanceIndex;
            if (slot.generation == 0u) slot.generation = 1u;
            return { slotIndex, slot.generation };
        }
        if (handleSlots_.size() >= static_cast<size_t>(
            (std::numeric_limits<uint32_t>::max)())) {
            return {};
        }
        const uint32_t slotIndex =
            static_cast<uint32_t>(handleSlots_.size());
        handleSlots_.push_back({ instanceIndex, 1u });
        return { slotIndex, 1u };
    }

    void VfxPlaybackService::ReleaseHandle(
        VfxPlaybackHandle handle) noexcept {

        if (!handle.IsValid() || handle.slot >= handleSlots_.size()) return;
        HandleSlot& slot = handleSlots_[handle.slot];
        if (slot.generation != handle.generation) return;
        slot.instanceIndex = kInvalidInstanceIndex;
        ++slot.generation;
        if (slot.generation == 0u) slot.generation = 1u;
        freeHandleSlots_.push_back(handle.slot);
    }

    VfxPlaybackService::Instance* VfxPlaybackService::Find(
        VfxPlaybackHandle handle) noexcept {

        if (!handle.IsValid() || handle.slot >= handleSlots_.size()) {
            return nullptr;
        }
        const HandleSlot& slot = handleSlots_[handle.slot];
        if (slot.generation != handle.generation ||
            slot.instanceIndex >= instances_.size()) {
            return nullptr;
        }
        Instance& instance = instances_[slot.instanceIndex];
        return instance.handle == handle ? &instance : nullptr;
    }

    const VfxPlaybackService::Instance* VfxPlaybackService::Find(
        VfxPlaybackHandle handle) const noexcept {

        return const_cast<VfxPlaybackService*>(this)->Find(handle);
    }

    void VfxPlaybackService::RetireInstanceAt(size_t index) {
        if (index >= instances_.size()) return;
        const size_t emitterCount =
            instances_[index].program->emitters.size();
        Instance retired = std::move(instances_[index]);
        RetireEmitterKeys(retired);
        ReleaseHandle(retired.handle);
        if (index + 1u != instances_.size()) {
            instances_[index] = std::move(instances_.back());
            handleSlots_[instances_[index].handle.slot].instanceIndex = index;
        }
        instances_.pop_back();
        admissionStats_.activeInstances = instances_.size();
        admissionStats_.activeEmitterInstances =
            admissionStats_.activeEmitterInstances >= emitterCount
                ? admissionStats_.activeEmitterInstances - emitterCount
                : 0u;
    }

    void VfxPlaybackService::RetireEmitterKeys(
        const Instance& instance) {

        for (uint32_t emitterIndex = 0u;
            emitterIndex < instance.program->emitters.size();
            ++emitterIndex) {
            pendingRetiredEmitters_.push_back({
                playbackNamespace_,
                instance.handle.slot,
                instance.handle.generation,
                emitterIndex,
            });
        }
    }

    void VfxPlaybackService::ResetInstanceSimulation(
        Instance& instance) noexcept {

        instance.elapsedSeconds = 0.0f;
        for (EmitterState& state : instance.emitterStates) {
            state = {};
        }
    }

    size_t VfxPlaybackService::AppendInstanceStep(
        Instance& instance,
        float deltaSeconds,
        uint32_t simulationOrdinal,
        bool drawAfterSimulation,
        bool wrapLoop) {

        const VfxProgram& program = *instance.program;
        const float remaining = (std::max)(
            0.0f,
            program.durationSeconds - instance.elapsedSeconds);
        const float spawnDelta = instance.loop
            ? deltaSeconds
            : (std::min)(deltaSeconds, remaining);
        const float duration = (std::max)(0.0f, program.durationSeconds);
        const float submittedAge = instance.loop && duration > 0.0f
            ? std::fmod(instance.elapsedSeconds + spawnDelta, duration)
            : (std::min)(
                duration,
                instance.elapsedSeconds + spawnDelta);

        std::fill(
            instance.transformResolved.begin(),
            instance.transformResolved.end(),
            uint8_t{ 0u });
        for (uint32_t emitterIndex = 0u;
             emitterIndex < program.emitters.size();
             ++emitterIndex) {
            if (instance.transformResolved[emitterIndex] != 0u) continue;
            instance.transformStack.clear();
            uint32_t current = emitterIndex;
            while (current != kInvalidVfxProgramIndex &&
                instance.transformResolved[current] == 0u) {
                instance.transformStack.push_back(current);
                current = program.emitters[current].parentEmitterIndex;
            }
            VfxWorldTransform parent =
                current == kInvalidVfxProgramIndex
                ? instance.transform
                : instance.emitterTransforms[current];
            while (!instance.transformStack.empty()) {
                const uint32_t child =
                    instance.transformStack.back();
                instance.transformStack.pop_back();
                parent = PLAYBACK::ResolveEmitterTransform(
                    instance.transform,
                    instance.ownerObject,
                    program.emitters[child],
                    parent,
                    instance.emitterStates[child],
                    attachmentResolver_);
                instance.emitterTransforms[child] = parent;
                instance.transformResolved[child] = 1u;
            }
        }

        const size_t firstSubmission = renderInput_.emitters.size();
        for (uint32_t emitterIndex = 0u;
            emitterIndex < program.emitters.size();
            ++emitterIndex) {
            const VfxProgramEmitter& emitter =
                program.emitters[emitterIndex];
            EmitterState& state = instance.emitterStates[emitterIndex];
            ParticleEmitterRuntimeOverrides overrides =
                emitter.authoredOverrides;
            for (const VfxProgramInputLink& link : emitter.inputLinks) {
                if (link.inputIndex < instance.inputValues.size()) {
                    ApplyRuntimeLink(
                        overrides,
                        link,
                        instance.inputValues[link.inputIndex]);
                }
            }
            ClampRuntimeOverrides(overrides);

            uint32_t spawnCount = 0u;
            if (overrides.enabled != 0u) {
                spawnCount = PLAYBACK::ComputeEmitterSpawnCount(
                    state,
                    program,
                    emitter,
                    instance.elapsedSeconds,
                    spawnDelta,
                    overrides.spawnRateMultiplier,
                    instance.loop,
                    wrapLoop);
            }

            ParticleEmitterSubmission submission{};
            submission.key = {
                playbackNamespace_,
                instance.handle.slot,
                instance.handle.generation,
                emitterIndex,
            };
            submission.simulationOrdinal = simulationOrdinal;
            submission.drawAfterSimulation = drawAfterSimulation;
            submission.resetSimulation = state.resetSimulation;
            submission.spawnCount = spawnCount;
            submission.randomSeed = instance.randomSeed +
                emitterIndex * 0x9e3779b9u;
            submission.deltaSeconds = deltaSeconds;
            // Age is the local post-step time. In particular, a single host
            // delta can cross several short loop periods; never publish a
            // pre-wrap value greater than the authored program duration.
            submission.emitterAgeSeconds = submittedAge;
            const VfxWorldTransform& transform =
                instance.emitterTransforms[emitterIndex];
            submission.worldPosition = transform.position;
            submission.worldRotation = transform.rotation;
            submission.worldScale = transform.scale;
            submission.overrides = overrides;
            submission.simulation = emitter.simulation;
            submission.render = emitter.render;
            renderInput_.emitters.push_back(std::move(submission));
            state.resetSimulation = false;
        }

        instance.elapsedSeconds += deltaSeconds;
        if (wrapLoop && instance.loop && program.durationSeconds > 0.0f &&
            instance.elapsedSeconds >= program.durationSeconds) {
            const float wrapped = std::fmod(
                instance.elapsedSeconds,
                program.durationSeconds);
            instance.elapsedSeconds = wrapped;
            // ComputeEmitterSpawnCount already advanced state through every
            // crossed loop. Only an exact boundary starts a fresh schedule;
            // a non-zero remainder must keep its burst cursor/accumulator.
            if (wrapped <= 1.0e-6f ||
                program.durationSeconds - wrapped <= 1.0e-6f) {
                instance.elapsedSeconds = 0.0f;
                for (EmitterState& state : instance.emitterStates) {
                    state.burstPending = true;
                    state.nextBurstIndex = 0u;
                    state.resetSimulation = true;
                }
            }
        }
        return renderInput_.emitters.size() - firstSubmission;
    }

    bool VfxPlaybackService::ApplyParameters(
        Instance& instance,
        const std::vector<VfxParameterOverride>& parameters,
        std::vector<VfxPlaybackDiagnostic>* outDiagnostics) {

        bool allApplied = true;
        for (const VfxParameterOverride& parameter : parameters) {
            const auto input = std::find_if(
                instance.program->inputs.begin(),
                instance.program->inputs.end(),
                [&parameter](const VfxProgramInput& candidate) {
                    return candidate.id == parameter.inputId;
                });
            if (input == instance.program->inputs.end() ||
                !IsCompatible(parameter.value, input->type)) {
                allApplied = false;
                if (outDiagnostics != nullptr) {
                    outDiagnostics->push_back({
                        VfxDiagnosticSeverity::Warning,
                        input == instance.program->inputs.end()
                            ? "Vfx.UnknownInput"
                            : "Vfx.InputTypeMismatch",
                        "VFX parameter override was ignored: " +
                            parameter.inputId,
                    });
                }
                continue;
            }
        }
        if (!allApplied) {
            return false;
        }

        // Validate the complete batch before mutating any input. Public
        // VfxView::Update is transactional: a rejected parameter must not
        // leave the instance partially updated.
        for (const VfxParameterOverride& parameter : parameters) {
            const auto input = std::find_if(
                instance.program->inputs.begin(),
                instance.program->inputs.end(),
                [&parameter](const VfxProgramInput& candidate) {
                    return candidate.id == parameter.inputId;
                });
            const size_t index = static_cast<size_t>(
                input - instance.program->inputs.begin());
            instance.inputValues[index] = parameter.value;
        }
        return true;
    }

} // namespace HIKARI::VFX
