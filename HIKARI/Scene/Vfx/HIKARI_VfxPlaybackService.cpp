#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

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

        uint32_t AccumulateSpawnCount(
            float& accumulator,
            float spawnRatePerSecond,
            float multiplier,
            float deltaSeconds,
            uint32_t capacity) noexcept {

            if (!std::isfinite(accumulator) || accumulator < 0.0f) {
                accumulator = 0.0f;
            }
            if (capacity == 0u || !std::isfinite(spawnRatePerSecond) ||
                !std::isfinite(multiplier) ||
                !std::isfinite(deltaSeconds) ||
                spawnRatePerSecond <= 0.0f || multiplier <= 0.0f ||
                deltaSeconds <= 0.0f) {
                return 0u;
            }

            const double total = static_cast<double>(accumulator) +
                static_cast<double>(spawnRatePerSecond) *
                static_cast<double>(multiplier) *
                static_cast<double>(deltaSeconds);
            if (!std::isfinite(total) || total < 0.0) {
                accumulator = 0.0f;
                return 0u;
            }
            double integral = 0.0;
            accumulator = static_cast<float>(std::modf(total, &integral));
            return integral >= static_cast<double>(capacity)
                ? capacity
                : static_cast<uint32_t>(integral);
        }

        VfxWorldTransform Compose(
            const VfxWorldTransform& parent,
            const VfxTransformData& local) {

            constexpr float kDegreesToRadians =
                3.14159265358979323846f / 180.0f;
            const MATH::Quat localRotation = MATH::Quat::FromEulerXYZ(
                local.rotationEulerDegrees.x * kDegreesToRadians,
                local.rotationEulerDegrees.y * kDegreesToRadians,
                local.rotationEulerDegrees.z * kDegreesToRadians);
            const MATH::Mat4 matrix = MATH::Mat4::TRS(
                parent.position,
                parent.rotation,
                parent.scale) * MATH::Mat4::TRS(
                    local.position,
                    localRotation,
                    local.scale);
            VfxWorldTransform result{};
            if (!MATH::DecomposeTRS(
                    matrix,
                    result.position,
                    result.rotation,
                    result.scale)) {
                result.position = parent.position + local.position;
                result.rotation = parent.rotation;
                result.scale = {
                    parent.scale.x * local.scale.x,
                    parent.scale.y * local.scale.y,
                    parent.scale.z * local.scale.z,
                };
            }
            return result;
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
    }

    struct VfxPlaybackService::HandleSlot {
        size_t instanceIndex = kInvalidInstanceIndex;
        uint32_t generation = 1u;
    };

    struct VfxPlaybackService::EmitterState {
        float spawnAccumulator = 0.0f;
        bool burstPending = true;
        bool resetSimulation = true;
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
    };

    VfxPlaybackService::VfxPlaybackService(uint64_t playbackNamespace)
        : playbackNamespace_(
            playbackNamespace != 0u
                ? playbackNamespace
                : AllocatePlaybackNamespace()) {
    }

    VfxPlaybackService::~VfxPlaybackService() = default;

    void VfxPlaybackService::SetProgramResolver(
        VfxProgramResolver resolver) {

        resolver_ = std::move(resolver);
    }

    uint64_t VfxPlaybackService::GetPlaybackNamespace() const noexcept {
        return playbackNamespace_;
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
        (void)ApplyParameters(
            instance,
            request.parameters,
            &result.diagnostics);

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
        return result;
    }

    void VfxPlaybackService::Update(
        uint64_t frameIndex,
        float deltaSeconds) {

        deltaSeconds = std::isfinite(deltaSeconds)
            ? std::clamp(deltaSeconds, 0.0f, 0.25f)
            : 0.0f;
        seekBatchFrameIndex_ = kInvalidSeekBatchFrame;
        nextSeekSimulationOrdinal_ = 0u;
        renderInput_.frameIndex = frameIndex;
        renderInput_.emitters.clear();
        renderInput_.retiredEmitters.clear();
        renderInput_.retiredEmitters.swap(pendingRetiredEmitters_);

        for (size_t index = instances_.size(); index-- > 0u;) {
            const Instance& instance = instances_[index];
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
    }

    const ParticleRenderInput&
        VfxPlaybackService::GetRenderInput() const noexcept {

        return renderInput_;
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
        seekBatchFrameIndex_ = kInvalidSeekBatchFrame;
        nextSeekSimulationOrdinal_ = 0u;
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

        std::vector<VfxWorldTransform> emitterTransforms(
            program.emitters.size());
        std::vector<bool> transformResolved(
            program.emitters.size(), false);
        std::function<const VfxWorldTransform&(uint32_t)> resolveTransform =
            [&](uint32_t emitterIndex) -> const VfxWorldTransform& {
                if (transformResolved[emitterIndex]) {
                    return emitterTransforms[emitterIndex];
                }
                const VfxProgramEmitter& emitter =
                    program.emitters[emitterIndex];
                const VfxWorldTransform& parent =
                    emitter.parentEmitterIndex != kInvalidVfxProgramIndex
                    ? resolveTransform(emitter.parentEmitterIndex)
                    : instance.transform;
                emitterTransforms[emitterIndex] = Compose(
                    parent,
                    emitter.transform);
                transformResolved[emitterIndex] = true;
                return emitterTransforms[emitterIndex];
            };

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
                spawnCount = AccumulateSpawnCount(
                    state.spawnAccumulator,
                    emitter.spawnRatePerSecond,
                    overrides.spawnRateMultiplier,
                    spawnDelta,
                    emitter.simulation.capacity);
                if (state.burstPending &&
                    (instance.loop || remaining > 0.0f)) {
                    const uint32_t available =
                        emitter.simulation.capacity - spawnCount;
                    spawnCount += (std::min)(
                        emitter.burstCount,
                        available);
                }
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
            submission.emitterAgeSeconds = instance.elapsedSeconds;
            const VfxWorldTransform& transform =
                resolveTransform(emitterIndex);
            submission.worldPosition = transform.position;
            submission.worldRotation = transform.rotation;
            submission.worldScale = transform.scale;
            submission.overrides = overrides;
            submission.simulation = emitter.simulation;
            submission.render = emitter.render;
            renderInput_.emitters.push_back(std::move(submission));
            state.burstPending = false;
            state.resetSimulation = false;
        }

        instance.elapsedSeconds += deltaSeconds;
        if (wrapLoop && instance.loop && program.durationSeconds > 0.0f &&
            instance.elapsedSeconds >= program.durationSeconds) {
            instance.elapsedSeconds = std::fmod(
                instance.elapsedSeconds,
                program.durationSeconds);
            for (EmitterState& state : instance.emitterStates) {
                state.burstPending = true;
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
            const size_t index = static_cast<size_t>(
                input - instance.program->inputs.begin());
            instance.inputValues[index] = parameter.value;
        }
        return allApplied;
    }

} // namespace HIKARI::VFX
