#include "Scene/Sequencer/Runtime/HIKARI_SequencePlaybackService.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace HIKARI::SEQUENCER {

    namespace {
        constexpr size_t kInvalidInstanceIndex =
            (std::numeric_limits<size_t>::max)();
    }

    void SequencePlaybackService::SetAssetStore(
        SequenceAssetStore* assetStore) noexcept {

        assetStore_ = assetStore;
    }

    bool SequencePlaybackService::RegisterDriver(
        std::unique_ptr<ISequenceTrackDriver> driver) {

        if (!driver || driver->GetDriverId().empty()) {
            return false;
        }
        const auto duplicate = std::find_if(
            drivers_.begin(),
            drivers_.end(),
            [&driver](const auto& existing) {
                return existing && existing->GetDriverId() ==
                    driver->GetDriverId();
            });
        if (duplicate != drivers_.end()) {
            return false;
        }
        drivers_.push_back(std::move(driver));
        return true;
    }

    SequencePlaybackHandle SequencePlaybackService::Play(
        const SequencePlayRequest& request) {

        return PlayDetailed(request).handle;
    }

    SequencePlayResult SequencePlaybackService::PlayDetailed(
        const SequencePlayRequest& request) {

        if (assetStore_ == nullptr || !request.assetGuid.IsValid()) {
            const std::string message = "Invalid sequence asset request";
            PushRejected(0, request.assetGuid, message);
            return {
                {},
                { SequencePlaybackDiagnostic{
                    SequenceDiagnosticSeverity::Error,
                    "InvalidAssetRequest",
                    {},
                    message } }
            };
        }
        std::string error{};
        std::shared_ptr<const SequenceAsset> asset =
            assetStore_->Load(request.assetGuid, &error);
        if (!asset) {
            if (error.empty()) {
                error = "Sequence asset could not be loaded";
            }
            PushRejected(0, request.assetGuid, error);
            return {
                {},
                { SequencePlaybackDiagnostic{
                    SequenceDiagnosticSeverity::Error,
                    "AssetLoadFailed",
                    {},
                    std::move(error) } }
            };
        }
        return PlayLoaded(std::move(asset), request, 0);
    }

    SequencePlaybackHandle SequencePlaybackService::PlayInline(
        const CinematicSequence& sequence,
        const SequenceBindingContext& bindings,
        const SequencePlaybackOptions& options,
        float startTimeSeconds,
        std::string channel,
        int priority,
        SequenceChannelPolicy channelPolicy) {

        auto asset = std::make_shared<SequenceAsset>();
        asset->displayName = sequence.name;
        asset->sequence = sequence;
        NormalizeCinematicSequence(asset->sequence);
        SequencePlayRequest request{};
        request.bindings = bindings;
        request.options = options;
        request.startTimeSeconds = startTimeSeconds;
        request.channel = std::move(channel);
        request.priority = priority;
        request.channelPolicy = channelPolicy;
        return PlayLoaded(std::move(asset), request, 0).handle;
    }

    SequencePlayResult SequencePlaybackService::PlayLoaded(
        std::shared_ptr<const SequenceAsset> asset,
        const SequencePlayRequest& request,
        uint64_t requestId) {

        if (!asset) {
            const std::string message = "Sequence asset is null";
            PushRejected(requestId, request.assetGuid, message);
            return {
                {},
                { SequencePlaybackDiagnostic{
                    SequenceDiagnosticSeverity::Error,
                    "NullAsset",
                    {},
                    message } }
            };
        }

        Instance instance{};
        instance.handle = { nextHandleValue_++, epoch_ };
        if (nextHandleValue_ == 0) {
            nextHandleValue_ = 1;
        }
        instance.asset = std::move(asset);
        instance.bindings = request.bindings;
        instance.channel = request.channel;
        instance.priority = request.priority;
        instance.requestId = requestId;
        SequencePlayResult result{};
        const SequencePlaybackInstanceView validationView = MakeView(instance);
        if (!ValidateBindings(validationView, result.diagnostics) ||
            !ResolveChannelConflict(
                request,
                requestId,
                result.diagnostics)) {
            const std::string message = !result.diagnostics.empty()
                ? result.diagnostics.front().message
                : "Sequence playback request was rejected";
            PushRejected(requestId, request.assetGuid, message);
            return result;
        }
        if (!instance.cursor.Bind(
                instance.asset->sequence.durationSeconds,
                request.startTimeSeconds) ||
            !instance.cursor.Play(request.options)) {
            const std::string message =
                "Sequence playback cursor rejected the request";
            result.diagnostics.push_back({
                SequenceDiagnosticSeverity::Error,
                "InvalidPlaybackRange",
                {},
                message
            });
            PushRejected(requestId, request.assetGuid, message);
            return result;
        }

        instances_.push_back(std::move(instance));
        Instance& started = instances_.back();
        const SequencePlaybackInstanceView view = MakeView(started);
        for (const auto& driver : drivers_) {
            driver->OnSequenceStarted(view);
        }
        events_.push_back({
            SequencePlaybackEventKind::Started,
            started.handle,
            started.asset->guid,
            requestId,
            started.cursor.GetTimeSeconds(),
            SequenceStopReason::Stopped,
            {}
        });
        result.handle = started.handle;
        return result;
    }

    bool SequencePlaybackService::ValidateBindings(
        const SequencePlaybackInstanceView& instance,
        std::vector<SequencePlaybackDiagnostic>& diagnostics) const {

        if (instance.asset == nullptr || instance.bindings == nullptr) {
            diagnostics.push_back({
                SequenceDiagnosticSeverity::Error,
                "InvalidPlaybackInstance",
                {},
                "Sequence playback instance is incomplete"
            });
            return false;
        }

        for (const SequenceBinding& binding :
                instance.asset->sequence.bindings) {
            SceneObjectId resolved{};
            if (instance.bindings->Resolve(
                    instance.asset->sequence.bindings,
                    binding.id,
                    resolved)) {
                continue;
            }
            diagnostics.push_back({
                binding.required
                    ? SequenceDiagnosticSeverity::Error
                    : SequenceDiagnosticSeverity::Warning,
                binding.targetKind == SequenceBindingTargetKind::Slot
                    ? (binding.required
                        ? "MissingSlotBinding"
                        : "OptionalSlotUnbound")
                    : (binding.required
                        ? "MissingSceneObjectBinding"
                        : "OptionalSceneObjectUnbound"),
                {},
                binding.targetKind == SequenceBindingTargetKind::Slot
                    ? (binding.required
                        ? "Required sequence slot is not bound: "
                        : "Optional sequence slot is not bound: ") +
                        binding.slotName
                    : (binding.required
                        ? "Sequence binding has no scene object: "
                        : "Optional binding has no scene object: ") +
                        binding.name,
                binding.id,
                binding.slotName
            });
        }

        for (const auto& driver : drivers_) {
            if (driver) {
                driver->Validate(instance, diagnostics);
            }
        }
        return std::none_of(
            diagnostics.begin(),
            diagnostics.end(),
            [](const SequencePlaybackDiagnostic& diagnostic) {
                return diagnostic.severity ==
                    SequenceDiagnosticSeverity::Error;
            });
    }

    bool SequencePlaybackService::ResolveChannelConflict(
        const SequencePlayRequest& request,
        uint64_t requestId,
        std::vector<SequencePlaybackDiagnostic>& diagnostics) {

        if (request.channel.empty() || request.channelPolicy ==
                SequenceChannelPolicy::Parallel) {
            return true;
        }

        bool occupied = false;
        int highestPriority = (std::numeric_limits<int>::min)();
        for (const Instance& instance : instances_) {
            if (instance.channel != request.channel) {
                continue;
            }
            occupied = true;
            highestPriority = (std::max)(
                highestPriority,
                instance.priority);
        }
        if (!occupied) {
            return true;
        }

        if (request.channelPolicy ==
                SequenceChannelPolicy::RejectIfOccupied) {
            diagnostics.push_back({
                SequenceDiagnosticSeverity::Error,
                "ChannelOccupied",
                {},
                "Sequence channel is already occupied: " +
                    request.channel
            });
            return false;
        }
        if (request.channelPolicy ==
                SequenceChannelPolicy::ReplaceIfHigherOrEqual &&
            request.priority < highestPriority) {
            diagnostics.push_back({
                SequenceDiagnosticSeverity::Error,
                "ChannelPriorityRejected",
                {},
                "Sequence channel has a higher-priority owner: " +
                    request.channel
            });
            return false;
        }

        for (size_t index = instances_.size(); index-- > 0;) {
            if (instances_[index].channel == request.channel) {
                StopInstanceAt(
                    index,
                    SequenceStopReason::Replaced,
                    requestId);
            }
        }
        return true;
    }

    bool SequencePlaybackService::Stop(
        SequencePlaybackHandle handle,
        SequenceStopReason reason) {

        return StopInternal(handle, reason, 0);
    }

    bool SequencePlaybackService::Pause(SequencePlaybackHandle handle) {
        return PauseInternal(handle, 0);
    }

    bool SequencePlaybackService::Resume(SequencePlaybackHandle handle) {
        return ResumeInternal(handle, 0);
    }

    bool SequencePlaybackService::Seek(
        SequencePlaybackHandle handle,
        float timeSeconds) {

        return SeekInternal(handle, timeSeconds, 0);
    }

    bool SequencePlaybackService::StopInternal(
        SequencePlaybackHandle handle,
        SequenceStopReason reason,
        uint64_t requestId) {

        const size_t index = FindInstanceIndex(handle);
        if (index == kInvalidInstanceIndex) {
            return false;
        }
        StopInstanceAt(index, reason, requestId);
        return true;
    }

    bool SequencePlaybackService::PauseInternal(
        SequencePlaybackHandle handle,
        uint64_t requestId) {

        const size_t index = FindInstanceIndex(handle);
        if (index == kInvalidInstanceIndex ||
            !instances_[index].cursor.IsPlaying()) {
            return false;
        }
        instances_[index].cursor.Pause();
        events_.push_back({
            SequencePlaybackEventKind::Paused,
            handle,
            instances_[index].asset->guid,
            requestId,
            instances_[index].cursor.GetTimeSeconds(),
            SequenceStopReason::Stopped,
            {}
        });
        return true;
    }

    bool SequencePlaybackService::ResumeInternal(
        SequencePlaybackHandle handle,
        uint64_t requestId) {

        const size_t index = FindInstanceIndex(handle);
        if (index == kInvalidInstanceIndex ||
            !instances_[index].cursor.Resume()) {
            return false;
        }
        events_.push_back({
            SequencePlaybackEventKind::Resumed,
            handle,
            instances_[index].asset->guid,
            requestId,
            instances_[index].cursor.GetTimeSeconds(),
            SequenceStopReason::Stopped,
            {}
        });
        return true;
    }

    bool SequencePlaybackService::SeekInternal(
        SequencePlaybackHandle handle,
        float timeSeconds,
        uint64_t requestId) {

        const size_t index = FindInstanceIndex(handle);
        if (index == kInvalidInstanceIndex ||
            !std::isfinite(timeSeconds) ||
            !instances_[index].cursor.Seek(timeSeconds)) {
            return false;
        }
        const SequenceEvaluationContext context =
            instances_[index].cursor.Tick(0.0f, SequenceEvaluationMode::Runtime);
        EvaluateInstance(instances_[index], context);
        events_.push_back({
            SequencePlaybackEventKind::Seeked,
            handle,
            instances_[index].asset->guid,
            requestId,
            instances_[index].cursor.GetTimeSeconds(),
            SequenceStopReason::Stopped,
            {}
        });
        return true;
    }

    uint64_t SequencePlaybackService::Submit(
        SequencePlaybackCommand command) {

        if (command.requestId == 0) {
            command.requestId = nextRequestId_++;
            if (nextRequestId_ == 0) {
                nextRequestId_ = 1;
            }
        }
        const uint64_t requestId = command.requestId;
        pendingCommands_.push_back(std::move(command));
        return requestId;
    }

    void SequencePlaybackService::FlushCommands() {
        std::vector<SequencePlaybackCommand> commands{};
        commands.swap(pendingCommands_);
        for (SequencePlaybackCommand& command : commands) {
            bool accepted = false;
            switch (command.kind) {
            case SequencePlaybackCommandKind::Play: {
                if (assetStore_ == nullptr ||
                    !command.play.assetGuid.IsValid()) {
                    PushRejected(
                        command.requestId,
                        command.play.assetGuid,
                        "Invalid sequence asset request");
                    break;
                }
                std::string error{};
                auto asset = assetStore_->Load(
                    command.play.assetGuid,
                    &error);
                if (!asset) {
                    PushRejected(
                        command.requestId,
                        command.play.assetGuid,
                        error.empty()
                            ? "Sequence asset could not be loaded"
                            : std::move(error));
                    break;
                }
                accepted = PlayLoaded(
                    std::move(asset),
                    command.play,
                    command.requestId).IsAccepted();
                break;
            }
            case SequencePlaybackCommandKind::Stop:
                accepted = StopInternal(
                    command.handle,
                    SequenceStopReason::Stopped,
                    command.requestId);
                break;
            case SequencePlaybackCommandKind::Pause:
                accepted = PauseInternal(command.handle, command.requestId);
                break;
            case SequencePlaybackCommandKind::Resume:
                accepted = ResumeInternal(command.handle, command.requestId);
                break;
            case SequencePlaybackCommandKind::Seek:
                accepted = SeekInternal(
                    command.handle,
                    command.timeSeconds,
                    command.requestId);
                break;
            }
            if (!accepted && command.kind !=
                    SequencePlaybackCommandKind::Play) {
                PushRejected(
                    command.requestId,
                    command.play.assetGuid,
                    "Sequence command target is no longer active");
            }
        }
    }

    void SequencePlaybackService::Update(float deltaTime) {
        FlushCommands();
        for (size_t index = 0; index < instances_.size();) {
            Instance& instance = instances_[index];
            if (!instance.cursor.IsPlaying()) {
                ++index;
                continue;
            }
            const SequenceEvaluationContext context = instance.cursor.Tick(
                deltaTime,
                SequenceEvaluationMode::Runtime);
            EvaluateInstance(instance, context);
            if (context.completed) {
                const uint64_t requestId = instance.requestId;
                StopInstanceAt(
                    index,
                    SequenceStopReason::Completed,
                    requestId);
            } else {
                ++index;
            }
        }
    }

    void SequencePlaybackService::EvaluateInstance(
        Instance& instance,
        const SequenceEvaluationContext& context) {

        const SequencePlaybackInstanceView view = MakeView(instance);
        for (const auto& driver : drivers_) {
            driver->Evaluate(view, context);
        }
    }

    SequencePlaybackInstanceView SequencePlaybackService::MakeView(
        const Instance& instance) const noexcept {

        return {
            instance.handle,
            instance.asset.get(),
            &instance.bindings,
            instance.channel,
            instance.priority
        };
    }

    size_t SequencePlaybackService::FindInstanceIndex(
        SequencePlaybackHandle handle) const noexcept {

        const auto found = std::find_if(
            instances_.begin(),
            instances_.end(),
            [handle](const Instance& instance) {
                return instance.handle == handle;
            });
        return found == instances_.end()
            ? kInvalidInstanceIndex
            : static_cast<size_t>(found - instances_.begin());
    }

    void SequencePlaybackService::StopInstanceAt(
        size_t index,
        SequenceStopReason reason,
        uint64_t requestId) {

        if (index >= instances_.size()) {
            return;
        }
        const Instance& instance = instances_[index];
        for (const auto& driver : drivers_) {
            driver->OnSequenceStopped(instance.handle, reason);
        }
        const SequencePlaybackEventKind kind =
            reason == SequenceStopReason::Completed
            ? SequencePlaybackEventKind::Completed
            : SequencePlaybackEventKind::Stopped;
        events_.push_back({
            kind,
            instance.handle,
            instance.asset->guid,
            requestId,
            instance.cursor.GetTimeSeconds(),
            reason,
            reason == SequenceStopReason::Replaced ? "Replaced" : ""
        });
        instances_.erase(instances_.begin() + index);
    }

    void SequencePlaybackService::Reset() {
        for (size_t index = instances_.size(); index-- > 0;) {
            StopInstanceAt(index, SequenceStopReason::Reset, 0);
        }
        for (const auto& driver : drivers_) {
            driver->Reset();
        }
        pendingCommands_.clear();
        events_.clear();
        nextHandleValue_ = 1;
        nextRequestId_ = 1;
        AdvanceEpoch();
    }

    bool SequencePlaybackService::IsPlaying(
        SequencePlaybackHandle handle) const noexcept {

        const size_t index = FindInstanceIndex(handle);
        return index != kInvalidInstanceIndex &&
            instances_[index].cursor.IsPlaying();
    }

    bool SequencePlaybackService::IsPaused(
        SequencePlaybackHandle handle) const noexcept {

        const size_t index = FindInstanceIndex(handle);
        return index != kInvalidInstanceIndex &&
            instances_[index].cursor.IsPaused();
    }

    bool SequencePlaybackService::IsActive(
        SequencePlaybackHandle handle) const noexcept {

        return FindInstanceIndex(handle) != kInvalidInstanceIndex;
    }

    float SequencePlaybackService::GetTimeSeconds(
        SequencePlaybackHandle handle) const noexcept {

        const size_t index = FindInstanceIndex(handle);
        return index != kInvalidInstanceIndex
            ? instances_[index].cursor.GetTimeSeconds()
            : 0.0f;
    }

    bool SequencePlaybackService::TryGetSnapshot(
        SequencePlaybackHandle handle,
        SequencePlaybackSnapshot& outSnapshot) const noexcept {

        const size_t index = FindInstanceIndex(handle);
        if (index == kInvalidInstanceIndex) {
            outSnapshot = {};
            return false;
        }
        const Instance& instance = instances_[index];
        outSnapshot.handle = instance.handle;
        outSnapshot.assetGuid = instance.asset
            ? instance.asset->guid
            : AssetGuid{};
        outSnapshot.state = instance.cursor.GetState();
        outSnapshot.timeSeconds = instance.cursor.GetTimeSeconds();
        outSnapshot.durationSeconds = instance.cursor.GetDurationSeconds();
        outSnapshot.channel = instance.channel;
        outSnapshot.priority = instance.priority;
        return true;
    }

    size_t SequencePlaybackService::GetActiveInstanceCount() const noexcept {
        return instances_.size();
    }

    std::vector<SequencePlaybackEvent>
        SequencePlaybackService::ConsumeEvents() {

        std::vector<SequencePlaybackEvent> result{};
        result.swap(events_);
        return result;
    }

    void SequencePlaybackService::PushRejected(
        uint64_t requestId,
        const AssetGuid& assetGuid,
        std::string message) {

        events_.push_back({
            SequencePlaybackEventKind::Rejected,
            {},
            assetGuid,
            requestId,
            0.0f,
            SequenceStopReason::Stopped,
            std::move(message)
        });
    }

    void SequencePlaybackService::AdvanceEpoch() noexcept {
        ++epoch_;
        if (epoch_ == 0) {
            epoch_ = 1;
        }
    }

} // namespace HIKARI::SEQUENCER
