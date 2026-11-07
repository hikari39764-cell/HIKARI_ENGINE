#include "Scene/Vfx/HIKARI_VfxPlaybackState.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Numeric/HIKARI_NonZero.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include "Render3D/Vfx/HIKARI_ParticleCpuSimulation.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackSchedule.h"
#include "Scene/Vfx/HIKARI_VfxLayerHistory.h"
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

constexpr size_t kInvalidInstanceIndex = (std::numeric_limits<size_t>::max)();
constexpr uint64_t kInvalidSeekBatchFrame = (std::numeric_limits<uint64_t>::max)();
constexpr float kMaximumSpawnRateMultiplier = 1024.0f;
constexpr float kMaximumMotionMultiplier = 1024.0f;
constexpr float kMaximumHdrColorMultiplier = 64.0f;

uint64_t AllocatePlaybackNamespace() noexcept {
    static std::atomic_uint64_t nextNamespace{1u};
    uint64_t value = nextNamespace.fetch_add(1u, std::memory_order_relaxed);
    if (value == 0u) {
        value = nextNamespace.fetch_add(1u, std::memory_order_relaxed);
    }
    return value;
}

bool IsCompatible(const VfxValue& value, VfxValueType type) noexcept { return value.type == type; }

float ScalarValue(const VfxValue& value) noexcept {
    return value.type == VfxValueType::Int ? static_cast<float>(value.integer) : value.numeric.x;
}

float ClampFinite(float value, float minimum, float maximum, float fallback) noexcept {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
}

MATH::Vec4 ClampColorMultiplier(const MATH::Vec4& value, float fallback) noexcept {
    return {
        ClampFinite(value.x, 0.0f, kMaximumHdrColorMultiplier, fallback),
        ClampFinite(value.y, 0.0f, kMaximumHdrColorMultiplier, fallback),
        ClampFinite(value.z, 0.0f, kMaximumHdrColorMultiplier, fallback),
        ClampFinite(value.w, 0.0f, kMaximumHdrColorMultiplier, fallback),
    };
}

void ApplyRuntimeLink(ParticleEmitterRuntimeOverrides& overrides, const VfxProgramInputLink& link,
                      const VfxValue& value) noexcept {
    std::byte* destination = reinterpret_cast<std::byte*>(&overrides) + link.byteOffset;
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
        const float mapped = ScalarValue(value) * link.scale + link.bias;
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

void ClampRuntimeOverrides(ParticleEmitterRuntimeOverrides& overrides) noexcept {
    overrides.enabled = overrides.enabled != 0u ? 1u : 0u;
    overrides.spawnRateMultiplier = ClampFinite(overrides.spawnRateMultiplier, 0.0f, kMaximumSpawnRateMultiplier, 0.0f);
    overrides.radialSpeedMultiplier =
        ClampFinite(overrides.radialSpeedMultiplier, 0.0f, kMaximumMotionMultiplier, 1.0f);
    overrides.scaleMultiplier = ClampFinite(overrides.scaleMultiplier, 0.0f, kMaximumMotionMultiplier, 1.0f);
    overrides.gravityMultiplier =
        ClampFinite(overrides.gravityMultiplier, -kMaximumMotionMultiplier, kMaximumMotionMultiplier, 1.0f);
    overrides.colorMultiplier = ClampColorMultiplier(overrides.colorMultiplier, 1.0f);
}

bool RequiresLiveOwnerAttachment(const VfxProgram& program) noexcept {
    return std::any_of(program.emitters.begin(), program.emitters.end(), [](const VfxProgramEmitter& emitter) {
        return emitter.attachment.mode == VfxAttachmentMode::Owner ||
               emitter.attachment.mode == VfxAttachmentMode::OwnerSocket;
    });
}

} // namespace

VfxPlaybackService::VfxPlaybackService(uint64_t playbackNamespace)
    : playbackNamespace_(playbackNamespace != 0u ? playbackNamespace : AllocatePlaybackNamespace()) {
    renderInput_.sourceIdentity = playbackNamespace_;
}

VfxPlaybackService::~VfxPlaybackService() = default;

void VfxPlaybackService::SetProgramResolver(VfxProgramResolver resolver) { resolver_ = std::move(resolver); }

void VfxPlaybackService::SetAttachmentResolver(VfxAttachmentResolver resolver) {
    attachmentResolver_ = std::move(resolver);
}

void VfxPlaybackService::SetReliableRenderDeliveryEnabled(bool enabled) noexcept {
    reliableRenderDeliveryEnabled_ = enabled;
    if (!enabled) {
        pendingRenderDeliverySequence_ = 0u;
        pendingRenderDeltaSeconds_ = 0.0f;
    }
}

uint64_t VfxPlaybackService::GetPlaybackNamespace() const noexcept { return playbackNamespace_; }

bool VfxPlaybackService::ResolveSpecialOutput(const AssetGuid& assetGuid, std::string_view outputId,
                                              VfxSpecialOutputType expectedType,
                                              VfxSpecialOutputData& outOutput) const {
    outOutput = {};
    if (!assetGuid.IsValid() || outputId.empty() || !resolver_) {
        return false;
    }
    std::string error{};
    const std::shared_ptr<const VfxProgram> program = resolver_(assetGuid, error);
    if (!program) {
        return false;
    }
    const VfxSpecialOutputData* output = FindVfxSpecialOutput(*program, outputId, expectedType);
    if (output == nullptr || !PLAYBACK::HasUniqueSpecialOutputIds(*program)) {
        return false;
    }
    outOutput = *output;
    return true;
}

VfxPlayResult VfxPlaybackService::PlayDetailed(const VfxPlayRequest& request) {
    VfxPlayResult result{};
    std::shared_ptr<const VfxProgram> program = request.program;
    if (!program && resolver_ && request.assetGuid.IsValid()) {
        std::string error{};
        program = resolver_(request.assetGuid, error);
        if (!program) {
            result.diagnostics.push_back({
                VfxDiagnosticSeverity::Error,
                "Vfx.ProgramResolveFailed",
                error.empty() ? "VFX program could not be resolved" : std::move(error),
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
    if (!program || !ValidateVfxProgram(*program, validationError)) {
        result.diagnostics.push_back({
            VfxDiagnosticSeverity::Error,
            "Vfx.InvalidProgram",
            validationError.empty() ? "VFX play request has no compiled program" : std::move(validationError),
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
    const size_t pendingRetirementCount = pendingRetiredEmitters_.size();
    const bool outstandingEmitterBudgetInvalid =
        pendingRetirementCount > kMaximumActiveVfxPlaybackEmitters ||
        admissionStats_.activeEmitterInstances > kMaximumActiveVfxPlaybackEmitters - pendingRetirementCount;
    const size_t outstandingEmitterCount = outstandingEmitterBudgetInvalid
                                               ? kMaximumActiveVfxPlaybackEmitters
                                               : pendingRetirementCount + admissionStats_.activeEmitterInstances;
    if (instances_.size() >= kMaximumActiveVfxPlaybackInstances || emitterCount > kMaximumActiveVfxPlaybackEmitters ||
        outstandingEmitterBudgetInvalid || emitterCount > kMaximumActiveVfxPlaybackEmitters - outstandingEmitterCount) {
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
    instance.emitterTransforms.resize(instance.program->emitters.size());
    instance.transformResolved.resize(instance.program->emitters.size());
    instance.transformStack.reserve(instance.program->emitters.size());
    if (!ApplyParameters(instance, request.parameters, &result.diagnostics)) {
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
    if (instance == nullptr)
        return false;
    RetireInstanceAt(handleSlots_[handle.slot].instanceIndex);
    return true;
}

bool VfxPlaybackService::SetTransform(VfxPlaybackHandle handle, const VfxWorldTransform& transform) {
    Instance* instance = Find(handle);
    if (instance == nullptr)
        return false;
    instance->transform = transform;
    return true;
}

bool VfxPlaybackService::SetVelocity(VfxPlaybackHandle handle, const MATH::Vec3& velocity) {
    Instance* instance = Find(handle);
    if (instance == nullptr || !MATH::IsFinite(velocity))
        return false;
    instance->velocity = velocity;
    return true;
}

bool VfxPlaybackService::SetParameters(VfxPlaybackHandle handle, const std::vector<VfxParameterOverride>& parameters,
                                       std::vector<VfxPlaybackDiagnostic>* outDiagnostics) {
    Instance* instance = Find(handle);
    return instance != nullptr && ApplyParameters(*instance, parameters, outDiagnostics);
}

VfxSeekCapacity VfxPlaybackService::GetSeekCapacity(VfxPlaybackHandle handle, uint64_t frameIndex) const noexcept {
    VfxSeekCapacity capacity{};
    const Instance* instance = Find(handle);
    if (instance == nullptr)
        return capacity;

    capacity.handleActive = true;
    capacity.currentSeconds = instance->elapsedSeconds;
    capacity.emitterCount = static_cast<uint32_t>(
        (std::min)(instance->program->emitters.size(), static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
    const size_t usedSubmissions =
        seekBatchFrameIndex_ == frameIndex
            ? (std::min)(renderInput_.emitters.size(),
                         static_cast<size_t>(RENDER3D::PARTICLES::kMaximumParticleEmitterSubmissionsPerFrame))
            : 0u;
    capacity.remainingSubmissions =
        RENDER3D::PARTICLES::kMaximumParticleEmitterSubmissionsPerFrame - static_cast<uint32_t>(usedSubmissions);
    if (capacity.emitterCount != 0u) {
        capacity.maximumSteps = capacity.remainingSubmissions / capacity.emitterCount;
    }
    return capacity;
}

VfxSeekResult VfxPlaybackService::Seek(VfxPlaybackHandle handle, uint64_t frameIndex, float targetSeconds,
                                       float fixedStepSeconds, uint32_t maximumSteps) {
    return SeekInternal(handle, frameIndex, targetSeconds, fixedStepSeconds, maximumSteps, true);
}
VfxSeekResult VfxPlaybackService::SeekInternal(VfxPlaybackHandle handle, uint64_t frameIndex, float targetSeconds,
                                               float fixedStepSeconds, uint32_t maximumSteps, bool publish) {
    VfxSeekResult result{};
    if (reliableRenderDeliveryEnabled_ && pendingRenderDeliverySequence_ != 0u) {
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
    const float maximumTarget =
        instance->loop ? instance->program->durationSeconds
                       : instance->program->durationSeconds + instance->program->maximumParticleLifetimeSeconds;
    if (!std::isfinite(targetSeconds) || targetSeconds < 0.0f || targetSeconds > maximumTarget ||
        !std::isfinite(fixedStepSeconds) || fixedStepSeconds <= 0.0f) {
        result.diagnostics.push_back({
            VfxDiagnosticSeverity::Error,
            "Vfx.InvalidSeek",
            "VFX seek target or fixed step is outside its valid range",
        });
        return result;
    }
    result.accepted = true;
    maximumSteps = std::clamp(maximumSteps, 1u, kMaximumVfxSeekSubsteps);
    fixedStepSeconds = std::clamp(fixedStepSeconds, 1.0f / 1000.0f, 1.0f);

    const VfxSeekCapacity seekCapacity = GetSeekCapacity(handle, frameIndex);
    const uint32_t executableSteps = (std::min)(maximumSteps, seekCapacity.maximumSteps);
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
        const bool preserveStagedRetirements = renderInput_.frameIndex == frameIndex;
        renderInput_.frameIndex = frameIndex;
        renderInput_.emitters.clear();
        renderInput_.compositions.clear();
        renderInput_.fluids.clear();
        renderInput_.fluidSources.clear();
        if (!preserveStagedRetirements) {
            renderInput_.retiredEmitters.clear();
        }
        renderInput_.retiredEmitters.insert(renderInput_.retiredEmitters.end(), pendingRetiredEmitters_.begin(),
                                            pendingRetiredEmitters_.end());
        pendingRetiredEmitters_.clear();
        seekBatchFrameIndex_ = frameIndex;
        nextSeekSimulationOrdinal_ = 0u;
    }

    constexpr float kSeekEpsilon = 1.0e-6f;
    const bool rewind = targetSeconds + kSeekEpsilon < instance->elapsedSeconds;
    if (rewind || targetSeconds <= kSeekEpsilon) {
        ResetInstanceSimulation(*instance);
    }

    const size_t batchStart = renderInput_.emitters.size();
    const bool hasParticles =
        std::any_of(instance->program->emitters.begin(), instance->program->emitters.end(), [](const auto& layer) {
            return layer.kind == VfxLayerKind::Particles || layer.history.mode != VfxHistoryMode::None;
        });
    if (!hasParticles) {
        // 流体は時間を飛ばせない：時刻が変わったら域を作り直して預熱からやり直す（巻き戻しは上で作り直し済み）。
        if (std::abs(targetSeconds - instance->elapsedSeconds) > kSeekEpsilon)
            for (size_t index = 0u; index < instance->program->emitters.size(); ++index)
                if (instance->program->emitters[index].kind == VfxLayerKind::Fluid)
                    instance->emitterStates[index].resetSimulation = true;
        instance->elapsedSeconds = targetSeconds;
        (void)AppendInstanceStep(*instance, 0.0f, nextSeekSimulationOrdinal_++, true, false);
        result.executedSteps = 1;
    } else if (targetSeconds <= kSeekEpsilon) {
        (void)AppendInstanceStep(*instance, 0.0f, nextSeekSimulationOrdinal_++, true, false);
    } else {
        while (result.executedSteps < executableSteps && instance->elapsedSeconds + kSeekEpsilon < targetSeconds) {
            const float step = (std::min)(fixedStepSeconds, targetSeconds - instance->elapsedSeconds);
            (void)AppendInstanceStep(*instance, step, nextSeekSimulationOrdinal_++,
                                     instance->elapsedSeconds + step + kSeekEpsilon >= targetSeconds, false);
            ++result.executedSteps;
        }
        if (renderInput_.emitters.size() == batchStart) {
            (void)AppendInstanceStep(*instance, 0.0f, nextSeekSimulationOrdinal_++, true, false);
        }
    }

    const uint32_t finalOrdinal = nextSeekSimulationOrdinal_ - 1u;
    for (size_t index = batchStart; index < renderInput_.emitters.size(); ++index) {
        renderInput_.emitters[index].drawAfterSimulation =
            renderInput_.emitters[index].simulationOrdinal == finalOrdinal;
    }
    result.complete = std::abs(instance->elapsedSeconds - targetSeconds) <= kSeekEpsilon;
    if (result.complete) {
        instance->elapsedSeconds = targetSeconds;
    } else if (executableSteps < maximumSteps && result.executedSteps == executableSteps) {
        result.diagnostics.push_back({
            VfxDiagnosticSeverity::Warning,
            "Vfx.SeekSubmissionBudgetExhausted",
            "VFX seek reached the per-frame submission budget; "
            "use GetSeekCapacity to select a larger fixed step",
        });
    }
    result.currentSeconds = instance->elapsedSeconds;
    if (publish)
        PublishRenderInputDelivery();
    return result;
}

bool VfxPlaybackService::QueueSeek(VfxPlaybackHandle handle, float seconds) {
    auto* instance = Find(handle);
    if (!instance || !std::isfinite(seconds) || seconds < 0.0f)
        return false;
    if (instance->loop && instance->program->durationSeconds > 0.0f)
        seconds = std::fmod(seconds, instance->program->durationSeconds);
    if (seconds > instance->program->durationSeconds + instance->program->maximumParticleLifetimeSeconds)
        return false;
    instance->queuedSeek = seconds;
    return true;
}

bool VfxPlaybackService::SetTimeScale(VfxPlaybackHandle handle, float scale) {
    auto* instance = Find(handle);
    if (!instance || !std::isfinite(scale) || scale < 0.0f || scale > 8.0f)
        return false;
    instance->timeScale = scale;
    return true;
}

void VfxPlaybackService::Update(uint64_t frameIndex, float deltaSeconds) {
    if (hasUpdatedFrame_ && lastUpdateFrame_ == frameIndex) {
        return;
    }
    hasUpdatedFrame_ = true;
    lastUpdateFrame_ = frameIndex;
    deltaSeconds = std::isfinite(deltaSeconds) ? std::clamp(deltaSeconds, 0.0f, 0.25f) : 0.0f;
    if (reliableRenderDeliveryEnabled_ && pendingRenderDeliverySequence_ != 0u) {
        pendingRenderDeltaSeconds_ = (std::min)(pendingRenderDeltaSeconds_ + deltaSeconds, 0.25f);
        return;
    }
    deltaSeconds = (std::min)(deltaSeconds + pendingRenderDeltaSeconds_, 0.25f);
    pendingRenderDeltaSeconds_ = 0.0f;
    seekBatchFrameIndex_ = kInvalidSeekBatchFrame;
    nextSeekSimulationOrdinal_ = 0u;
    renderInput_.frameIndex = frameIndex;
    renderInput_.emitters.clear();
    renderInput_.compositions.clear();
    renderInput_.fluids.clear();
    renderInput_.fluidSources.clear();
    renderInput_.retiredEmitters.clear();
    renderInput_.retiredEmitters.swap(pendingRetiredEmitters_);

    for (size_t index = instances_.size(); index-- > 0u;) {
        const Instance& instance = instances_[index];
        if (attachmentResolver_ && instance.ownerObject.IsValid() && RequiresLiveOwnerAttachment(*instance.program)) {
            VfxWorldTransform ownerTransform{};
            if (!attachmentResolver_(instance.ownerObject, {}, ownerTransform)) {
                // An Owner/OwnerSocket effect may tolerate a transient
                // pose or socket miss, but the empty-socket lookup is the
                // authoritative owner-liveness probe. Retire the complete
                // instance before its last captured transform can become
                // a permanent looping ghost.
                RetireInstanceAt(index);
                continue;
            }
        }
        if (!instance.queuedSeek && !instance.loop &&
            instance.elapsedSeconds >=
                instance.program->durationSeconds + instance.program->maximumParticleLifetimeSeconds) {
            RetireInstanceAt(index);
        }
    }
    renderInput_.retiredEmitters.insert(renderInput_.retiredEmitters.end(), pendingRetiredEmitters_.begin(),
                                        pendingRetiredEmitters_.end());
    pendingRetiredEmitters_.clear();

    // Reserve one draw submission for every other emitter before spending
    // the remaining budget on preroll; one cue cannot starve the scene.
    size_t remainingEmitters = admissionStats_.activeEmitterInstances;
    seekBatchFrameIndex_ = frameIndex;
    for (Instance& instance : instances_) {
        const size_t emitters = instance.program->emitters.size();
        remainingEmitters -= emitters;
        if (instance.queuedSeek && emitters != 0u) {
            const size_t used = renderInput_.emitters.size() + remainingEmitters;
            const size_t free = kMaximumParticleEmitterSubmissionsPerFrame -
                                (std::min)(used, size_t(kMaximumParticleEmitterSubmissionsPerFrame));
            const uint32_t steps = static_cast<uint32_t>((std::min)(size_t(32u), free / emitters));
            if (steps != 0u) {
                const auto result =
                    SeekInternal(instance.handle, frameIndex, *instance.queuedSeek, 1.0f / 60.0f, steps, false);
                if (result.complete || !result.accepted)
                    instance.queuedSeek.reset();
                continue;
            }
        }
        const float instanceSeconds = deltaSeconds * instance.timeScale;
        instance.transform.position = instance.transform.position + instance.velocity * instanceSeconds;
        (void)AppendInstanceStep(instance, instanceSeconds, 0u, true, true);
    }
    PublishRenderInputDelivery();
}

const ParticleRenderInput& VfxPlaybackService::GetRenderInput() const noexcept { return renderInput_; }

bool VfxPlaybackService::AcknowledgeRenderInput(ParticleRenderDeliveryToken token) noexcept {
    if (!reliableRenderDeliveryEnabled_ || !token.IsValid() || token.sourceIdentity != playbackNamespace_ ||
        token.sequence != pendingRenderDeliverySequence_) {
        return false;
    }
    pendingRenderDeliverySequence_ = 0u;
    return true;
}

bool VfxPlaybackService::HasPendingRenderInput() const noexcept {
    return reliableRenderDeliveryEnabled_ && pendingRenderDeliverySequence_ != 0u;
}

bool VfxPlaybackService::IsActive(VfxPlaybackHandle handle) const noexcept { return Find(handle) != nullptr; }

size_t VfxPlaybackService::GetActiveInstanceCount() const noexcept { return instances_.size(); }

const VfxPlaybackAdmissionStats& VfxPlaybackService::GetAdmissionStats() const noexcept { return admissionStats_; }

void VfxPlaybackService::ApplyConnectTarget(Instance& instance, uint32_t emitterIndex) const noexcept {
    if (!instance.program || emitterIndex >= instance.program->emitters.size() ||
        emitterIndex >= instance.emitterTransforms.size()) {
        return;
    }
    const auto& render = instance.program->emitters[emitterIndex].render;
    const uint32_t mode = render.connectTargetMode;
    if (mode == static_cast<uint32_t>(VfxConnectTargetMode::None))
        return;

    MATH::Vec3 target{};
    if (mode == static_cast<uint32_t>(VfxConnectTargetMode::LayerOrigin)) {
        const uint32_t index = render.connectTargetLayerIndex;
        if (index == RENDER3D::PARTICLES::ParticleEmitterRenderParameters::kInvalidConnectLayerIndex ||
            index >= instance.emitterTransforms.size() || index >= instance.transformResolved.size() ||
            instance.transformResolved[index] == 0u) {
            return;
        }
        target = instance.emitterTransforms[index].position;
    } else {
        // ローカル指定点はエフェクト根の変換で world へ持ち上げる。
        const MATH::Vec4 local{render.connectLocalPosition.x, render.connectLocalPosition.y,
                               render.connectLocalPosition.z, 1.0f};
        const MATH::Vec4 world =
            MATH::Mat4::TRS(instance.transform.position, instance.transform.rotation, instance.transform.scale)
                .TransformPoint(local);
        target = {world.x, world.y, world.z};
    }

    // 長さ軸は Look At Line と同じ軸を使う。形状ごとの特例は作らない。
    const uint32_t axis = render.billboardAxis;
    const MATH::Vec3 lengthAxis = axis == 1u   ? MATH::Vec3{0.0f, 1.0f, 0.0f}
                                  : axis == 2u ? MATH::Vec3{0.0f, 0.0f, 1.0f}
                                               : MATH::Vec3{1.0f, 0.0f, 0.0f};
    VfxWorldTransform& transform = instance.emitterTransforms[emitterIndex];
    (void)PLAYBACK::ApplyConnectTargetTransform(
        transform, target, lengthAxis, render.connectPivot, render.connectLengthScale, render.connectStartOffset,
        render.connectEndOffset, (render.connectFlags & 1u) != 0u, (render.connectFlags & 2u) != 0u);
}

size_t VfxPlaybackService::CollectMeshLayers(std::vector<VfxLayerMeshSample>& outSamples) const {
    outSamples.clear();
    for (const Instance& instance : instances_) {
        if (!instance.program)
            continue;
        const auto& emitters = instance.program->emitters;
        const size_t emitterCount = (std::min)(emitters.size(), instance.emitterTransforms.size());
        for (size_t emitterIndex = 0u; emitterIndex < emitterCount; ++emitterIndex) {
            const auto& render = emitters[emitterIndex].render;
            if (render.meshAssetGuid.empty())
                continue;
            if (emitterIndex >= instance.layerVisible.size() || instance.layerVisible[emitterIndex] == 0u) {
                continue;
            }
            VfxLayerMeshSample sample{};
            sample.playbackNamespace = playbackNamespace_;
            sample.handle = instance.handle;
            sample.emitterIndex = static_cast<uint32_t>(emitterIndex);
            sample.meshAssetGuid = render.meshAssetGuid;
            sample.materialAssetGuid = render.materialAssetGuid;
            sample.worldTransform = instance.emitterTransforms[emitterIndex];
            sample.flags = render.meshOutputFlags;
            outSamples.push_back(std::move(sample));
        }
    }
    return outSamples.size();
}

size_t VfxPlaybackService::CollectLightOutputs(std::vector<VfxLayerLightSample>& outSamples) const {
    outSamples.clear();
    for (const Instance& instance : instances_) {
        if (!instance.program)
            continue;
        const auto& emitters = instance.program->emitters;
        const size_t emitterCount = (std::min)(emitters.size(), instance.emitterTransforms.size());
        for (size_t emitterIndex = 0u; emitterIndex < emitterCount; ++emitterIndex) {
            if (emitterIndex >= instance.layerVisible.size() || instance.layerVisible[emitterIndex] == 0u) {
                continue;
            }
            const auto& emitter = emitters[emitterIndex];
            const float start = PLAYBACK::EmitterStartDelay(*instance.program, emitter);
            const float end = PLAYBACK::EmitterActiveEnd(*instance.program, emitter);
            if (instance.elapsedSeconds < start || instance.elapsedSeconds >= end)
                continue;
            for (uint32_t outputIndex = 0; outputIndex < emitter.specialOutputs.size(); ++outputIndex) {
                const auto& output = emitter.specialOutputs[outputIndex];
                if (!output.enabled || output.type != VfxSpecialOutputType::Light)
                    continue;
                const auto& profile = output.light;
                if (!(profile.luminousFlux > 0.0f) || !(profile.range > 0.0f))
                    continue;
                // 流体の発光に付いて行く光源：まだ回読が届いていない・光っていない域は出さない。
                const RENDER3D::FLUID::FluidGlowReport* glow = nullptr;
                float glowLuminance = 0.0f;
                if (profile.followFluidGlow) {
                    glow = FindFluidGlow(instance.handle, static_cast<uint32_t>(emitterIndex));
                    if (glow != nullptr)
                        glowLuminance =
                            0.2126f * glow->intensity.x + 0.7152f * glow->intensity.y + 0.0722f * glow->intensity.z;
                    if (!(glowLuminance > 0.0f) || !(profile.glowShare > 0.0f))
                        continue;
                }
                // レイヤー色をそのまま落とす。エフェクトの色と光源色が
                // 食い違うと、加法発光と照明が別物に見えてしまう。
                MATH::Vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
                if (emitterIndex < instance.layerOverrides.size()) {
                    tint = instance.layerOverrides[emitterIndex].colorMultiplier;
                }
                // Shape/Group はレイヤー自身の色サンプルも掛かる。描画側の
                // submission と同じ合成規則にして、光と発光を一致させる。
                if (emitters[emitterIndex].kind != VfxLayerKind::Particles &&
                    emitterIndex < instance.layerSamples.size()) {
                    const MATH::Vec4& sampled = instance.layerSamples[emitterIndex].color;
                    tint = {
                        tint.x * sampled.x,
                        tint.y * sampled.y,
                        tint.z * sampled.z,
                        tint.w * sampled.w,
                    };
                }
                VfxLayerLightSample sample{};
                sample.handle = instance.handle;
                sample.emitterIndex = static_cast<uint32_t>(emitterIndex);
                sample.outputIndex = outputIndex;
                sample.position = instance.emitterTransforms[emitterIndex].position;
                sample.color = {
                    profile.color.x * tint.x,
                    profile.color.y * tint.y,
                    profile.color.z * tint.z,
                    profile.color.w * tint.w,
                };
                sample.luminousFlux = profile.luminousFlux;
                sample.range = profile.range;
                sample.sourceRadius = profile.sourceRadius;
                sample.flickerFrequency = profile.flickerFrequency;
                sample.flickerAmplitude = profile.flickerAmplitude;
                sample.ageSeconds = instance.elapsedSeconds - start;
                sample.priority = profile.priority;
                sample.castsShadows = profile.castsShadows;
                // 発光の重心に置き、光束は 4π × 発光の光度 × 届く割合（luminousFlux が上限）、色に発光の色を掛ける。
                if (glow != nullptr) {
                    const float peak = (std::max)({glow->intensity.x, glow->intensity.y, glow->intensity.z});
                    sample.position = glow->worldPosition;
                    sample.luminousFlux =
                        (std::min)(profile.luminousFlux, 12.5663706f * glowLuminance * profile.glowShare);
                    sample.color = {sample.color.x * glow->intensity.x / peak,
                                    sample.color.y * glow->intensity.y / peak,
                                    sample.color.z * glow->intensity.z / peak, sample.color.w};
                }
                outSamples.push_back(sample);
            }
        }
    }
    return outSamples.size();
}

size_t VfxPlaybackService::CollectScreenWaves(VfxScreenWaveBatch& outBatch) const {
    outBatch = {};
    for (const Instance& instance : instances_) {
        if (!instance.program)
            continue;
        const auto& emitters = instance.program->emitters;
        const size_t emitterCount = (std::min)(emitters.size(), instance.emitterTransforms.size());
        for (size_t emitterIndex = 0u; emitterIndex < emitterCount; ++emitterIndex) {
            if (outBatch.count >= outBatch.waves.size()) {
                return outBatch.count;
            }
            if (emitterIndex >= instance.layerVisible.size() || instance.layerVisible[emitterIndex] == 0u) {
                continue;
            }
            const auto& emitter = emitters[emitterIndex];
            const float start = PLAYBACK::EmitterStartDelay(*instance.program, emitter);
            const float end = PLAYBACK::EmitterActiveEnd(*instance.program, emitter);
            if (instance.elapsedSeconds < start || instance.elapsedSeconds >= end)
                continue;
            for (const auto& output : emitter.specialOutputs) {
                if (!output.enabled || output.type != VfxSpecialOutputType::ScreenImpulse)
                    continue;
                if (outBatch.count >= outBatch.waves.size())
                    return outBatch.count;
                const auto& profile = output.screenImpulse;
                const float age = instance.elapsedSeconds - start;
                if (!(profile.durationSeconds > age) || !(profile.intensity > 0.0f))
                    continue;
                RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrame wave{};
                wave.worldPosition = instance.emitterTransforms[emitterIndex].position;
                wave.ageSeconds = age;
                wave.durationSeconds = profile.durationSeconds;
                wave.maximumScreenRadius = profile.maxRadius;
                wave.ringThickness = profile.ringThickness;
                wave.distortionStrength = profile.distortion;
                wave.intensity = profile.intensity;
                wave.color = {
                    (std::max)(profile.color.x, 0.0f),
                    (std::max)(profile.color.y, 0.0f),
                    (std::max)(profile.color.z, 0.0f),
                };
                wave.chromaticShift = profile.chromaticAberration;
                wave.flashIntensity = profile.flash;
                wave.waveFrequency = profile.waveFrequency;
                wave.waveAmplitude = profile.waveAmplitude;
                wave.waveDamping = profile.waveDamping;
                wave.waveCount = profile.waveCount;
                outBatch.waves[outBatch.count++] = wave;
            }
        }
    }
    return outBatch.count;
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
    renderInput_.compositions.clear();
    renderInput_.fluids.clear();
    renderInput_.fluidSources.clear();
    renderInput_.retiredEmitters.clear();
    renderInput_.deliverySequence = 0u;
    pendingRenderDeliverySequence_ = 0u;
    pendingRenderDeltaSeconds_ = 0.0f;
    seekBatchFrameIndex_ = kInvalidSeekBatchFrame;
    nextSeekSimulationOrdinal_ = 0u;
}

void VfxPlaybackService::PublishRenderInputDelivery() noexcept {
    if (renderInput_.emitters.empty() && renderInput_.retiredEmitters.empty() && renderInput_.fluids.empty()) {
        renderInput_.deliverySequence = 0u;
        return;
    }
    NUMERIC::AdvanceNonZero(nextRenderDeliverySequence_);
    renderInput_.deliverySequence = nextRenderDeliverySequence_;
    if (reliableRenderDeliveryEnabled_) {
        pendingRenderDeliverySequence_ = nextRenderDeliverySequence_;
    }
}

bool VfxPlaybackService::PublishFrozenRenderInput() noexcept {
    if (reliableRenderDeliveryEnabled_ && pendingRenderDeliverySequence_ != 0u)
        return false;
    if (renderInput_.emitters.empty() && renderInput_.retiredEmitters.empty() && renderInput_.fluids.empty()) {
        return false;
    }
    bool needsFreeze = std::any_of(renderInput_.fluids.begin(), renderInput_.fluids.end(), [](const auto& domain) {
        return domain.deltaSeconds > 0.0f || domain.resetSimulation;
    });
    for (const auto& submission : renderInput_.emitters) {
        if (submission.deltaSeconds > 0.0f || submission.spawnCount != 0u || submission.resetSimulation) {
            needsFreeze = true;
            break;
        }
    }
    if (!needsFreeze) {
        // すでに時間が止まっている入力を配信済み。毎フレーム呼ばれる経路なので
        // 余計な配信 (delivery sequence) を増やさない。
        return false;
    }
    // シークの中間ステップを描画へ昇格させず、受領済みの最終状態だけ残す。
    std::erase_if(renderInput_.emitters, [](const auto& submission) { return !submission.drawAfterSimulation; });
    // 同じ入力を再描画してもシミュレーションが進まないように時間を止める。
    // frameIndex は据え置き、レンダラ側の per-frame 再シミュレーション判定にも
    // 「同じフレーム」として扱わせる。
    for (auto& submission : renderInput_.emitters) {
        submission.deltaSeconds = 0.0f;
        submission.spawnCount = 0u;
        submission.resetSimulation = false;
        submission.drawAfterSimulation = true;
    }
    for (auto& domain : renderInput_.fluids) {
        domain.deltaSeconds = 0.0f;
        domain.resetSimulation = false;
        for (auto& source : domain.sources)
            source.burstDensity = 0.0f;
    }
    for (auto& source : renderInput_.fluidSources)
        source.burstDensity = 0.0f;
    PublishRenderInputDelivery();
    return true;
}

VfxPlaybackHandle VfxPlaybackService::AllocateHandle(size_t instanceIndex) {
    if (!freeHandleSlots_.empty()) {
        const uint32_t slotIndex = freeHandleSlots_.back();
        freeHandleSlots_.pop_back();
        HandleSlot& slot = handleSlots_[slotIndex];
        slot.instanceIndex = instanceIndex;
        if (slot.generation == 0u)
            slot.generation = 1u;
        return {slotIndex, slot.generation};
    }
    if (handleSlots_.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return {};
    }
    const uint32_t slotIndex = static_cast<uint32_t>(handleSlots_.size());
    handleSlots_.push_back({instanceIndex, 1u});
    return {slotIndex, 1u};
}

void VfxPlaybackService::ReleaseHandle(VfxPlaybackHandle handle) noexcept {
    if (!handle.IsValid() || handle.slot >= handleSlots_.size())
        return;
    HandleSlot& slot = handleSlots_[handle.slot];
    if (slot.generation != handle.generation)
        return;
    slot.instanceIndex = kInvalidInstanceIndex;
    ++slot.generation;
    if (slot.generation == 0u)
        slot.generation = 1u;
    freeHandleSlots_.push_back(handle.slot);
}

VfxPlaybackService::Instance* VfxPlaybackService::Find(VfxPlaybackHandle handle) noexcept {
    if (!handle.IsValid() || handle.slot >= handleSlots_.size()) {
        return nullptr;
    }
    const HandleSlot& slot = handleSlots_[handle.slot];
    if (slot.generation != handle.generation || slot.instanceIndex >= instances_.size()) {
        return nullptr;
    }
    Instance& instance = instances_[slot.instanceIndex];
    return instance.handle == handle ? &instance : nullptr;
}

const VfxPlaybackService::Instance* VfxPlaybackService::Find(VfxPlaybackHandle handle) const noexcept {
    return const_cast<VfxPlaybackService*>(this)->Find(handle);
}

void VfxPlaybackService::RetireInstanceAt(size_t index) {
    if (index >= instances_.size())
        return;
    const size_t emitterCount = instances_[index].program->emitters.size();
    Instance retired = std::move(instances_[index]);
    RetireEmitterKeys(retired);
    ReleaseHandle(retired.handle);
    if (index + 1u != instances_.size()) {
        instances_[index] = std::move(instances_.back());
        handleSlots_[instances_[index].handle.slot].instanceIndex = index;
    }
    instances_.pop_back();
    admissionStats_.activeInstances = instances_.size();
    admissionStats_.activeEmitterInstances = admissionStats_.activeEmitterInstances >= emitterCount
                                                 ? admissionStats_.activeEmitterInstances - emitterCount
                                                 : 0u;
}

void VfxPlaybackService::RetireEmitterKeys(const Instance& instance) {
    for (uint32_t emitterIndex = 0u; emitterIndex < instance.program->emitters.size(); ++emitterIndex) {
        pendingRetiredEmitters_.push_back({
            playbackNamespace_,
            instance.handle.slot,
            instance.handle.generation,
            emitterIndex,
        });
    }
}

void VfxPlaybackService::ResetInstanceSimulation(Instance& instance) noexcept {
    instance.elapsedSeconds = 0.0f;
    for (EmitterState& state : instance.emitterStates) {
        state = {};
    }
}

size_t VfxPlaybackService::AppendInstanceStep(Instance& instance, float deltaSeconds, uint32_t simulationOrdinal,
                                              bool drawAfterSimulation, bool wrapLoop) {
    const VfxProgram& program = *instance.program;
    const float remaining = (std::max)(0.0f, program.durationSeconds - instance.elapsedSeconds);
    const float spawnDelta = instance.loop ? deltaSeconds : (std::min)(deltaSeconds, remaining);
    const float duration = (std::max)(0.0f, program.durationSeconds);
    if (instance.loop && duration > 0 && instance.elapsedSeconds + spawnDelta >= duration)
        for (auto& state : instance.emitterStates)
            state.history.Clear();
    const float submittedAge = instance.loop && duration > 0.0f
                                   ? std::fmod(instance.elapsedSeconds + spawnDelta, duration)
                                   : (std::min)(duration, instance.elapsedSeconds + spawnDelta);

    std::fill(instance.transformResolved.begin(), instance.transformResolved.end(), uint8_t{0u});
    instance.layerSamples.resize(program.emitters.size());
    instance.groupTints.resize(program.emitters.size());
    instance.animationFrames.resize(program.emitters.size());
    instance.layerOverrides.resize(program.emitters.size());
    instance.layerVisible.resize(program.emitters.size());
    for (size_t i = 0; i < program.emitters.size(); ++i) {
        const auto& layer = program.emitters[i];
        auto& overrides = instance.layerOverrides[i];
        overrides = layer.authoredOverrides;
        for (const auto& link : layer.inputLinks) {
            if (!ResolveVfxRuntimePropertyLayout(link.semantic).renderParameter &&
                link.inputIndex < instance.inputValues.size())
                ApplyRuntimeLink(overrides, link, instance.inputValues[link.inputIndex]);
        }
        ClampRuntimeOverrides(overrides);
    }
    for (uint32_t emitterIndex = 0u; emitterIndex < program.emitters.size(); ++emitterIndex) {
        if (instance.transformResolved[emitterIndex] != 0u)
            continue;
        instance.transformStack.clear();
        uint32_t current = emitterIndex;
        while (current != kInvalidVfxProgramIndex && instance.transformResolved[current] == 0u) {
            instance.transformStack.push_back(current);
            current = program.emitters[current].parentEmitterIndex;
        }
        VfxWorldTransform parent =
            current == kInvalidVfxProgramIndex ? instance.transform : instance.emitterTransforms[current];
        while (!instance.transformStack.empty()) {
            const uint32_t child = instance.transformStack.back();
            instance.transformStack.pop_back();
            parent =
                PLAYBACK::ResolveEmitterTransform(instance.transform, instance.ownerObject, program.emitters[child],
                                                  parent, instance.emitterStates[child], attachmentResolver_);
            const auto& layer = program.emitters[child];
            instance.animationFrames[child] = parent;
            const bool parentVisible = layer.parentEmitterIndex == kInvalidVfxProgramIndex ||
                                       instance.layerVisible[layer.parentEmitterIndex] != 0;
            const float start = PLAYBACK::EmitterStartDelay(program, layer);
            const float end = PLAYBACK::EmitterActiveEnd(program, layer);
            // パーティクルの既存個体は放出終了後も寿命まで描画する。
            instance.layerVisible[child] =
                parentVisible && instance.layerOverrides[child].enabled &&
                (layer.kind == VfxLayerKind::Particles || (submittedAge >= start && submittedAge < end));
            if (layer.kind != VfxLayerKind::Particles) {
                const float span = end - start;
                auto& sample = instance.layerSamples[child];
                sample =
                    EvaluateVfxLayerAnimation(layer, std::clamp(submittedAge - start, 0.0f, (std::max)(span, 0.0f)),
                                              span, instance.randomSeed + child * 0x9e3779b9u);
                parent = PLAYBACK::ComposeTransforms(
                    parent, {sample.position,
                             EvaluateVfxLayerRotation(layer, sample,
                                                      std::clamp(submittedAge - start, 0.0f, (std::max)(span, 0.0f)),
                                                      span, instance.randomSeed + child * 0x9e3779b9u),
                             sample.scale});
            }
            const auto inherited = layer.parentEmitterIndex == kInvalidVfxProgramIndex
                                       ? MATH::Vec4{1, 1, 1, 1}
                                       : instance.groupTints[layer.parentEmitterIndex];
            auto& color = instance.layerOverrides[child].colorMultiplier;
            color = {color.x * inherited.x, color.y * inherited.y, color.z * inherited.z, color.w * inherited.w};
            instance.groupTints[child] = inherited;
            if (layer.kind == VfxLayerKind::Group) {
                const auto tint = instance.layerSamples[child].color;
                instance.groupTints[child] = {color.x * tint.x, color.y * tint.y, color.z * tint.z, color.w * tint.w};
            }
            instance.emitterTransforms[child] = parent;
            instance.transformResolved[child] = 1u;
        }
    }
    // 接続ターゲットは全レイヤーの変換が確定してから一度だけ適用する。
    // 参照先が未解決でも順序に依存しない。
    for (uint32_t emitterIndex = 0u; emitterIndex < program.emitters.size(); ++emitterIndex) {
        if (instance.transformResolved[emitterIndex] == 0u)
            continue;
        ApplyConnectTarget(instance, emitterIndex);
    }

    if (drawAfterSimulation)
        ProjectComposition(instance, submittedAge);
    const size_t firstSubmission = renderInput_.emitters.size();
    for (uint32_t emitterIndex = 0u; emitterIndex < program.emitters.size(); ++emitterIndex) {
        const VfxProgramEmitter& emitter = program.emitters[emitterIndex];
        if (emitter.kind == VfxLayerKind::Group || emitter.kind == VfxLayerKind::Adjustment)
            continue;
        // 流体源は域の提交の中で扱う。域の下に無いものは場面の域へ注入する源として渡す。
        if (emitter.kind == VfxLayerKind::FluidSource) {
            if (emitter.fluidDomainIndex == kInvalidVfxProgramIndex)
                AppendFluidSceneSource(instance, emitterIndex, spawnDelta, drawAfterSimulation, wrapLoop);
            continue;
        }
        // 流体域は粒子 pass へは出さず、下の流体源をまとめた域の提交にする（源は域の中で処理する）。
        if (emitter.kind == VfxLayerKind::Fluid) {
            AppendFluidDomain(instance, emitterIndex, deltaSeconds, spawnDelta, drawAfterSimulation, wrapLoop);
            continue;
        }
        // Mesh 出力レイヤーはモデル描画側が担当する。粒子 pass へは
        // 一切渡さない（形状は手続きプリミティブではないため）。
        if (!emitter.render.meshAssetGuid.empty())
            continue;

        EmitterState& state = instance.emitterStates[emitterIndex];
        ParticleEmitterRuntimeOverrides overrides = instance.layerOverrides[emitterIndex];
        overrides.enabled = instance.layerVisible[emitterIndex];

        uint32_t spawnCount = 0u;
        if (overrides.enabled != 0u && emitter.kind == VfxLayerKind::Particles) {
            spawnCount = PLAYBACK::ComputeEmitterSpawnCount(state, program, emitter, instance.elapsedSeconds,
                                                            spawnDelta, overrides.spawnRateMultiplier, instance.loop,
                                                            wrapLoop, emitter.simulation.capacity);
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
        submission.randomSeed = instance.randomSeed + emitterIndex * 0x9e3779b9u;
        submission.deltaSeconds = deltaSeconds;
        // Age is the local post-step time. In particular, a single host
        // delta can cross several short loop periods; never publish a
        // pre-wrap value greater than the authored program duration.
        submission.emitterAgeSeconds = submittedAge;
        const VfxWorldTransform& transform = instance.emitterTransforms[emitterIndex];
        submission.worldPosition = transform.position;
        submission.worldRotation = transform.rotation;
        submission.worldScale = transform.scale;
        submission.overrides = overrides;
        submission.simulation = emitter.simulation;
        submission.render = emitter.render;
        const float layerStart = PLAYBACK::EmitterStartDelay(program, emitter);
        const float layerSpan = PLAYBACK::EmitterActiveEnd(program, emitter) - layerStart;
        EvaluateVfxLayerRenderParameters(emitter, layerSpan > 0 ? (submittedAge - layerStart) / layerSpan : 0,
                                         submission.render, layerSpan);
        ProjectAdjustmentParameters(instance, emitter, submittedAge, submission.render);
        // Render bindings are absolute and apply after authored curves to every
        // current geometry submission, including particles that already exist.
        for (const auto& link : emitter.inputLinks) {
            if (!ResolveVfxRuntimePropertyLayout(link.semantic).renderParameter)
                continue;
            float value = ScalarValue(instance.inputValues[link.inputIndex]) * link.scale + link.bias;
            if (!std::isfinite(value))
                continue;
            if (link.semantic == VfxRuntimeBindingSemantic::RingStartAngleDegrees ||
                link.semantic == VfxRuntimeBindingSemantic::RingEndAngleDegrees)
                value *= 0.01745329251994329577f;
            else
                value = (std::max)(0.f, value);
            std::memcpy(reinterpret_cast<std::byte*>(&submission.render) + link.byteOffset, &value, sizeof(value));
        }
        submission.render.ringInnerRadius =
            (std::min)(submission.render.ringInnerRadius, submission.render.ringOuterRadius);
        if (emitter.kind == VfxLayerKind::Shape) {
            submission.directShape = true;
            const float start = PLAYBACK::EmitterStartDelay(program, emitter);
            const float end = PLAYBACK::EmitterActiveEnd(program, emitter);
            submission.shapeDurationSeconds = emitter.animationTiming.Duration(end - start);
            submission.shapeAgeSeconds = emitter.animationTiming.Seconds(submittedAge - start, end - start);
            submission.overrides.enabled = overrides.enabled && submittedAge >= start && submittedAge < end ? 1u : 0u;
            const auto& tint = instance.layerSamples[emitterIndex].color;
            submission.overrides.colorMultiplier = {
                tint.x * overrides.colorMultiplier.x, tint.y * overrides.colorMultiplier.y,
                tint.z * overrides.colorMultiplier.z, tint.w * overrides.colorMultiplier.w};
        }
        if (emitter.kind == VfxLayerKind::Shape) {
            if (!instance.layerOverrides[emitterIndex].enabled ||
                (emitter.parentEmitterIndex != kInvalidVfxProgramIndex &&
                 !instance.layerVisible[emitter.parentEmitterIndex]))
                state.history.Clear();
            state.history.Project(emitter.history, instance.elapsedSeconds + deltaSeconds, submission);
            if (!drawAfterSimulation)
                continue;
        }
        renderInput_.emitters.push_back(std::move(submission));
        state.resetSimulation = false;
    }

    instance.elapsedSeconds += deltaSeconds;
    if (wrapLoop && instance.loop && program.durationSeconds > 0.0f &&
        instance.elapsedSeconds >= program.durationSeconds) {
        const float wrapped = std::fmod(instance.elapsedSeconds, program.durationSeconds);
        instance.elapsedSeconds = wrapped;
        // ComputeEmitterSpawnCount already advanced state through every
        // crossed loop. Only an exact boundary starts a fresh schedule;
        // a non-zero remainder must keep its burst cursor/accumulator.
        if (wrapped <= 1.0e-6f || program.durationSeconds - wrapped <= 1.0e-6f) {
            instance.elapsedSeconds = 0.0f;
            for (size_t index = 0u; index < instance.emitterStates.size(); ++index) {
                EmitterState& state = instance.emitterStates[index];
                state.burstPending = true;
                state.nextBurstIndex = 0u;
                // 流体域は輪の継ぎ目で作り直さない（烟が続いたまま次の周回へ流れる）。
                state.resetSimulation = state.resetSimulation || program.emitters[index].kind != VfxLayerKind::Fluid;
            }
        }
    }
    return renderInput_.emitters.size() - firstSubmission;
}

bool VfxPlaybackService::ApplyParameters(Instance& instance, const std::vector<VfxParameterOverride>& parameters,
                                         std::vector<VfxPlaybackDiagnostic>* outDiagnostics) {
    bool allApplied = true;
    for (const VfxParameterOverride& parameter : parameters) {
        const auto input =
            std::find_if(instance.program->inputs.begin(), instance.program->inputs.end(),
                         [&parameter](const VfxProgramInput& candidate) { return candidate.id == parameter.inputId; });
        if (input == instance.program->inputs.end() || !IsCompatible(parameter.value, input->type)) {
            allApplied = false;
            if (outDiagnostics != nullptr) {
                outDiagnostics->push_back({
                    VfxDiagnosticSeverity::Warning,
                    input == instance.program->inputs.end() ? "Vfx.UnknownInput" : "Vfx.InputTypeMismatch",
                    "VFX parameter override was ignored: " + parameter.inputId,
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
        const auto input =
            std::find_if(instance.program->inputs.begin(), instance.program->inputs.end(),
                         [&parameter](const VfxProgramInput& candidate) { return candidate.id == parameter.inputId; });
        const size_t index = static_cast<size_t>(input - instance.program->inputs.begin());
        instance.inputValues[index] = parameter.value;
    }
    return true;
}

} // namespace HIKARI::VFX
