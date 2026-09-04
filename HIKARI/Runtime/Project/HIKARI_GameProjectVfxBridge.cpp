#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "HIKARI/Project/ActionViews.h"
#include "HIKARI/Project/VfxView.h"
#include "HIKARI/Project/WorldView.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Vfx/HIKARI_ActionPostReactionService.h"
#include "Scene/Vfx/HIKARI_CombatAfterimageService.h"
#include "Scene/Vfx/HIKARI_CombatPresentationService.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

namespace HIKARI {
    namespace {

        constexpr float kDirectionEpsilon = 1.0e-6f;
        constexpr size_t kDiagnosticThrottleCapacity = 32u;
        constexpr uint32_t kDiagnosticEmissionLimit = 4u;
        constexpr uint64_t kDiagnosticFrameInterval = 30u;
        constexpr uint64_t kInvalidDiagnosticFrame =
            (std::numeric_limits<uint64_t>::max)();

        struct DiagnosticThrottleEntry {
            std::string key{};
            uint64_t lastFrame = kInvalidDiagnosticFrame;
            uint32_t emissionCount = 0u;
        };

        std::mutex gDiagnosticThrottleMutex;
        std::array<DiagnosticThrottleEntry, kDiagnosticThrottleCapacity>
            gDiagnosticThrottle{};

        bool IsDiagnosticTextCharacter(char value) noexcept {
            return (value >= 'a' && value <= 'z') ||
                (value >= 'A' && value <= 'Z') ||
                (value >= '0' && value <= '9') ||
                value == '.' || value == '_' || value == ':' ||
                value == '-';
        }

        void AppendDiagnosticText(
            std::string& destination,
            std::string_view value) {
            constexpr size_t kMaximumFieldCharacters = 128u;
            size_t appended = 0u;
            for (const char character : value) {
                if (appended >= kMaximumFieldCharacters) {
                    destination.append("...");
                    break;
                }
                destination.push_back(
                    IsDiagnosticTextCharacter(character) ? character : '?');
                ++appended;
            }
        }

        bool ConsumeDiagnosticBudget(
            std::string_view key,
            uint64_t frameIndex) {
            std::lock_guard lock(gDiagnosticThrottleMutex);

            DiagnosticThrottleEntry* available = nullptr;
            for (DiagnosticThrottleEntry& entry : gDiagnosticThrottle) {
                if (entry.key == key) {
                    if (entry.emissionCount >= kDiagnosticEmissionLimit) {
                        return false;
                    }
                    if (entry.lastFrame != kInvalidDiagnosticFrame &&
                        frameIndex >= entry.lastFrame &&
                        frameIndex - entry.lastFrame <
                            kDiagnosticFrameInterval) {
                        return false;
                    }
                    entry.lastFrame = frameIndex;
                    ++entry.emissionCount;
                    return true;
                }
                if (available == nullptr && entry.key.empty()) {
                    available = &entry;
                }
            }

            // The table is deliberately non-evicting.  A malformed public
            // caller must not be able to rotate arbitrary ids into an
            // unbounded diagnostic stream.
            if (available == nullptr) {
                return false;
            }
            available->key.assign(key);
            available->lastFrame = frameIndex;
            available->emissionCount = 1u;
            return true;
        }

        void EmitVfxBridgeDiagnostic(
            std::string_view event,
            std::string_view reason,
            std::string_view code = {},
            std::string_view assetGuid = {},
            std::string_view outputId = {},
            std::string_view expectedType = {}) noexcept {
            try {
                std::string key{};
                key.reserve(
                    event.size() + reason.size() + code.size() +
                    assetGuid.size() + outputId.size() +
                    expectedType.size() + 8u);
                AppendDiagnosticText(key, event);
                key.push_back('|');
                AppendDiagnosticText(key, reason);
                key.push_back('|');
                AppendDiagnosticText(key, code);
                key.push_back('|');
                AppendDiagnosticText(key, assetGuid);
                key.push_back('|');
                AppendDiagnosticText(key, outputId);
                key.push_back('|');
                AppendDiagnosticText(key, expectedType);
                if (!ConsumeDiagnosticBudget(
                        key,
                        TIME::GetFrameContext().frameIndex)) {
                    return;
                }

                std::string message{
                    "[GameProjectVfx][WARN] event="};
                AppendDiagnosticText(message, event);
                message.append(" reason=");
                AppendDiagnosticText(message, reason);
                if (!code.empty()) {
                    message.append(" code=");
                    AppendDiagnosticText(message, code);
                }
                if (!assetGuid.empty()) {
                    message.append(" assetGuid=");
                    AppendDiagnosticText(message, assetGuid);
                }
                if (!outputId.empty()) {
                    message.append(" outputId=");
                    AppendDiagnosticText(message, outputId);
                }
                if (!expectedType.empty()) {
                    message.append(" expectedType=");
                    AppendDiagnosticText(message, expectedType);
                }
                DEBUGLOG::PushRenderError(std::move(message));
            }
            catch (...) {
                // Diagnostics must never change the public bridge result.
            }
        }

        const char* ToSpecialOutputTypeName(
            VFX::VfxSpecialOutputType type) noexcept {
            switch (type) {
            case VFX::VfxSpecialOutputType::WeaponPathDistortion:
                return "WeaponPathDistortion";
            case VFX::VfxSpecialOutputType::ScreenImpulse:
                return "ScreenImpulse";
            case VFX::VfxSpecialOutputType::SkinnedAfterimage:
                return "SkinnedAfterimage";
            default:
                return "Unknown";
            }
        }

        std::string_view ToSafePlaybackRejectReason(
            std::string_view code) noexcept {
            if (code == "Vfx.ProgramResolveFailed") {
                return "program_resolve_failed";
            }
            if (code == "Vfx.DuplicateSpecialOutputId") {
                return "duplicate_special_output_id";
            }
            if (code == "Vfx.InvalidProgram") {
                return "invalid_program";
            }
            if (code == "Vfx.InvalidBurstTimelineOrder") {
                return "invalid_burst_timeline_order";
            }
            if (code == "Vfx.PlaybackBudgetExceeded") {
                return "playback_budget_exceeded";
            }
            if (code == "Vfx.InvalidParameterOverrides") {
                return "invalid_parameter_overrides";
            }
            if (code == "Vfx.HandleExhausted") {
                return "playback_handle_exhausted";
            }
            return code.empty() ? std::string_view{
                "playback_rejected"} : code;
        }

        void LogPlayRejection(
            const VFX::VfxPlayResult& result,
            std::string_view assetGuid) noexcept {
            if (result.diagnostics.empty()) {
                EmitVfxBridgeDiagnostic(
                    "play_rejected",
                    "playback_rejected",
                    {},
                    assetGuid);
                return;
            }
            const VFX::VfxPlaybackDiagnostic& diagnostic =
                result.diagnostics.front();
            EmitVfxBridgeDiagnostic(
                "play_rejected",
                ToSafePlaybackRejectReason(diagnostic.code),
                diagnostic.code,
                assetGuid);
        }

        bool IsFinite(const GameProjectFloat3& value) noexcept {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        bool IsFinite(const GameProjectQuat& value) noexcept {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z) &&
                std::isfinite(value.w);
        }

        bool IsFinitePositive(float value) noexcept {
            return std::isfinite(value) && value > 0.0f;
        }

        bool IsFiniteNonNegative(float value) noexcept {
            return std::isfinite(value) && value >= 0.0f;
        }

        bool IsFiniteNonNegative(
            const GameProjectFloat3& value) noexcept {
            return IsFinite(value) && value.x >= 0.0f &&
                value.y >= 0.0f && value.z >= 0.0f;
        }

        bool IsFiniteNonZeroDirection(
            const GameProjectFloat3& value) noexcept {
            if (!IsFinite(value)) {
                return false;
            }
            const float lengthSquared = value.x * value.x +
                value.y * value.y + value.z * value.z;
            return std::isfinite(lengthSquared) &&
                lengthSquared > kDirectionEpsilon * kDirectionEpsilon;
        }

        MATH::Vec3 ToRuntime(const GameProjectFloat3& value) noexcept {
            return { value.x, value.y, value.z };
        }

        MATH::Quat ToRuntime(const GameProjectQuat& value) noexcept {
            return { value.x, value.y, value.z, value.w };
        }

        RuntimeObjectHandle DecodeRuntimeHandle(
            uint64_t value) noexcept {
            if (value == 0u) {
                return {};
            }
            const uint32_t encodedSlot = static_cast<uint32_t>(
                value & 0xFFFFFFFFu);
            const uint32_t generation = static_cast<uint32_t>(
                value >> 32u);
            if (encodedSlot == 0u || generation == 0u) {
                return {};
            }
            RuntimeObjectHandle result{};
            result.slot = encodedSlot - 1u;
            result.generation = generation;
            return result.IsValid() ? result : RuntimeObjectHandle{};
        }

        bool IsFiniteNonZeroRotation(
            const GameProjectQuat& value) noexcept {
            if (!IsFinite(value)) {
                return false;
            }
            const float lengthSquared = value.x * value.x +
                value.y * value.y + value.z * value.z +
                value.w * value.w;
            return std::isfinite(lengthSquared) &&
                lengthSquared > kDirectionEpsilon * kDirectionEpsilon;
        }

        bool IsValidTransform(
            const GameProjectTransform& transform) noexcept {
            return IsFinite(transform.position) &&
                IsFinite(transform.rotation) &&
                IsFinite(transform.scale) &&
                IsFiniteNonZeroRotation(transform.rotation) &&
                transform.scale.x > 0.0f &&
                transform.scale.y > 0.0f &&
                transform.scale.z > 0.0f;
        }

        bool HasNonZeroReserved(
            const uint8_t* values,
            size_t count) noexcept {
            if (values == nullptr) {
                return true;
            }
            for (size_t index = 0u; index < count; ++index) {
                if (values[index] != 0u) {
                    return true;
                }
            }
            return false;
        }

        template<size_t Capacity>
        bool ReadFixedText(
            const char (&source)[Capacity],
            std::string& outValue) {
            size_t count = 0u;
            while (count < Capacity && source[count] != '\0') {
                ++count;
            }
            if (count == Capacity) {
                outValue.clear();
                return false;
            }
            outValue.assign(source, count);
            return true;
        }

        VFX::VfxValueType ToVfxValueType(
            GameProjectBindingValueType type) noexcept {
            switch (type) {
            case GameProjectBindingValueType::Bool:
                return VFX::VfxValueType::Bool;
            case GameProjectBindingValueType::Int:
                return VFX::VfxValueType::Int;
            case GameProjectBindingValueType::Float2:
                return VFX::VfxValueType::Float2;
            case GameProjectBindingValueType::Float3:
                return VFX::VfxValueType::Float3;
            case GameProjectBindingValueType::Float4:
                return VFX::VfxValueType::Float4;
            case GameProjectBindingValueType::Color:
                return VFX::VfxValueType::Color;
            case GameProjectBindingValueType::Float:
            default:
                return VFX::VfxValueType::Float;
            }
        }

        bool ToVfxValue(
            const GameProjectBindingValue& source,
            VFX::VfxValue& outValue) noexcept {
            outValue = {};
            const auto hasFiniteNumeric = [&source](size_t count) noexcept {
                for (size_t index = 0u; index < count; ++index) {
                    if (!std::isfinite(source.numeric[index])) {
                        return false;
                    }
                }
                return true;
            };
            switch (source.type) {
            case GameProjectBindingValueType::Bool:
            case GameProjectBindingValueType::Int:
                outValue.type = ToVfxValueType(source.type);
                outValue.boolean = source.boolean;
                outValue.integer = source.integer;
                return true;
            case GameProjectBindingValueType::Float:
                if (!hasFiniteNumeric(1u)) return false;
                break;
            case GameProjectBindingValueType::Float2:
                if (!hasFiniteNumeric(2u)) return false;
                break;
            case GameProjectBindingValueType::Float3:
                if (!hasFiniteNumeric(3u)) return false;
                break;
            case GameProjectBindingValueType::Float4:
            case GameProjectBindingValueType::Color:
                if (!hasFiniteNumeric(4u)) return false;
                break;
            default:
                return false;
            }
            outValue.type = ToVfxValueType(source.type);
            outValue.boolean = source.boolean;
            outValue.integer = source.integer;
            outValue.numeric = {
                source.numeric[0],
                source.numeric[1],
                source.numeric[2],
                source.numeric[3]
            };
            return true;
        }

        bool ToVfxParameters(
            const GameProjectVfxParameterOverrides& source,
            std::vector<VFX::VfxParameterOverride>& outParameters) {
            if (source.count >
                kGameProjectVfxMaximumParameterOverrides) {
                return false;
            }
            outParameters.clear();
            outParameters.reserve(source.count);
            for (uint32_t index = 0u; index < source.count; ++index) {
                const GameProjectVfxParameterOverride& parameter =
                    source.values[index];
                std::string inputId{};
                if (!ReadFixedText(parameter.inputId, inputId) ||
                    inputId.empty()) {
                    return false;
                }
                VFX::VfxValue value{};
                if (!ToVfxValue(parameter.value, value)) {
                    return false;
                }
                outParameters.push_back({
                    std::move(inputId),
                    value
                });
            }
            return true;
        }

        VFX::VfxWorldTransform ToVfxTransform(
            const GameProjectTransform& source) noexcept {
            VFX::VfxWorldTransform result{};
            result.position = ToRuntime(source.position);
            result.rotation = ToRuntime(source.rotation);
            result.scale = ToRuntime(source.scale);
            return result;
        }

        GameProjectVfxPlaybackHandle ToProjectVfxHandle(
            const VFX::VfxPlaybackHandle& source,
            uint64_t playbackNamespace) noexcept {
            if (!source.IsValid() || playbackNamespace == 0u) {
                return {};
            }
            return {
                playbackNamespace,
                source.slot,
                source.generation
            };
        }

        bool ToRuntimeVfxHandle(
            const GameProjectVfxPlaybackHandle& source,
            uint64_t playbackNamespace,
            VFX::VfxPlaybackHandle& outHandle) noexcept {
            outHandle = {};
            if (!source.IsValid() ||
                source.playbackNamespace != playbackNamespace) {
                return false;
            }
            outHandle.slot = source.slot;
            outHandle.generation = source.generation;
            return outHandle.IsValid();
        }

        VFX::VfxPlaybackService* FindVfxPlaybackService(
            World* world) noexcept {
            return world != nullptr
                ? world->Services().Find<VFX::VfxPlaybackService>()
                : nullptr;
        }

        bool ReadProfileReference(
            const GameProjectVfxProfileReference& reference,
            AssetGuid& outGuid,
            std::string& outOutputId,
            std::string_view& outFailureReason) {
            std::string guid{};
            std::string path{};
            outGuid = {};
            outOutputId.clear();
            outFailureReason = "invalid_profile_reference";
            if (!ReadFixedText(reference.asset.assetGuid, guid)) {
                outFailureReason = "asset_guid_not_terminated";
                return false;
            }
            if (!ReadFixedText(reference.asset.assetPath, path)) {
                // Never include the authoring path in diagnostics.  It can
                // contain machine-local or otherwise sensitive information.
                outFailureReason = "asset_path_not_terminated";
                return false;
            }
            if (!ReadFixedText(reference.outputId, outOutputId)) {
                outFailureReason = "output_id_not_terminated";
                return false;
            }
            if (!IsValidAssetGuid(guid)) {
                outFailureReason = "invalid_asset_guid";
                return false;
            }
            if (outOutputId.empty()) {
                outFailureReason = "missing_output_id";
                return false;
            }
            outGuid = AssetGuid{ std::move(guid) };
            return outGuid.IsValid();
        }

        bool ResolveProfileForPresentation(
            World& world,
            const GameProjectVfxProfileReference& reference,
            VFX::VfxSpecialOutputType expectedType,
            VFX::VfxSpecialOutputData& outOutput) {
            AssetGuid guid{};
            std::string outputId{};
            std::string_view failureReason{};
            const char* expectedTypeName =
                ToSpecialOutputTypeName(expectedType);
            if (!ReadProfileReference(
                    reference,
                    guid,
                    outputId,
                    failureReason)) {
                EmitVfxBridgeDiagnostic(
                    "profile_resolve_failed",
                    failureReason,
                    {},
                    guid.value,
                    outputId,
                    expectedTypeName);
                return false;
            }
            VFX::VfxPlaybackService* playback =
                FindVfxPlaybackService(&world);
            if (playback == nullptr) {
                EmitVfxBridgeDiagnostic(
                    "profile_resolve_failed",
                    "playback_service_unavailable",
                    {},
                    guid.value,
                    outputId,
                    expectedTypeName);
                return false;
            }
            if (!playback->ResolveSpecialOutput(
                    guid,
                    outputId,
                    expectedType,
                    outOutput)) {
                // ResolveSpecialOutput intentionally returns only a value
                // view at the public bridge seam.  Keep the failure reason
                // explicit here without exposing resolver text or paths.
                EmitVfxBridgeDiagnostic(
                    "profile_resolve_failed",
                    "compiled_output_missing_or_type_mismatch",
                    {},
                    guid.value,
                    outputId,
                    expectedTypeName);
                return false;
            }
            return true;
        }

    } // namespace

    VfxView GameProjectSdkAccess::MakeVfxView(
        World& world) noexcept {
        try {
            return VfxView(&world);
        }
        catch (...) {
            return {};
        }
    }

    VfxView WorldView::GetVfx() const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        return world != nullptr
            ? GameProjectSdkAccess::MakeVfxView(*world)
            : VfxView{};
    }

    GameProjectVfxPlaybackHandle VfxView::Play(
        const GameProjectVfxPlayRequest& request) const noexcept {
        try {
            if (world_ == nullptr) {
                EmitVfxBridgeDiagnostic(
                    "play_rejected",
                    "world_unavailable");
                return {};
            }
            if (HasNonZeroReserved(
                    request.reserved,
                    sizeof(request.reserved))) {
                EmitVfxBridgeDiagnostic(
                    "play_rejected",
                    "reserved_bytes_nonzero");
                return {};
            }
            if (!IsValidTransform(request.transform)) {
                EmitVfxBridgeDiagnostic(
                    "play_rejected",
                    "invalid_transform");
                return {};
            }
            if (request.asset.assetGuid[0] == '\0') {
                EmitVfxBridgeDiagnostic(
                    "play_rejected",
                    "missing_asset_guid");
                return {};
            }
            if (request.parameters.count >
                kGameProjectVfxMaximumParameterOverrides) {
                EmitVfxBridgeDiagnostic(
                    "play_rejected",
                    "parameter_override_limit_exceeded");
                return {};
            }

            std::string guid{};
            std::string authoringPath{};
            if (!ReadFixedText(request.asset.assetGuid, guid)) {
                EmitVfxBridgeDiagnostic(
                    "play_rejected",
                    "asset_guid_not_terminated");
                return {};
            }
            if (!ReadFixedText(request.asset.assetPath, authoringPath)) {
                // The path remains an authoring-only fallback and must never
                // become part of a runtime diagnostic or identity.
                EmitVfxBridgeDiagnostic(
                    "play_rejected",
                    "asset_path_not_terminated",
                    {},
                    guid);
                return {};
            }
            if (!IsValidAssetGuid(guid)) {
                // The source path is intentionally an authoring fallback.
                // Shipping playback must resolve the stable GUID through the
                // runtime AssetRegistry configured on VfxPlaybackService.
                EmitVfxBridgeDiagnostic(
                    "play_rejected",
                    "invalid_asset_guid");
                return {};
            }

            VFX::VfxPlaybackService* service =
                FindVfxPlaybackService(static_cast<World*>(world_));
            if (service == nullptr) {
                EmitVfxBridgeDiagnostic(
                    "play_rejected",
                    "playback_service_unavailable",
                    {},
                    guid);
                return {};
            }

            VFX::VfxPlayRequest runtimeRequest{};
            runtimeRequest.assetGuid = AssetGuid{ std::move(guid) };
            runtimeRequest.transform = ToVfxTransform(request.transform);
            runtimeRequest.randomSeed = request.randomSeed;
            if (request.ownerObjectHandle != 0u) {
                const RuntimeObjectHandle owner = DecodeRuntimeHandle(
                    request.ownerObjectHandle);
                World* world = static_cast<World*>(world_);
                if (!owner.IsValid()) {
                    EmitVfxBridgeDiagnostic(
                        "play_rejected",
                        "invalid_owner_handle",
                        {},
                        runtimeRequest.assetGuid.value);
                    return {};
                }
                if (world->FindObject(owner) == nullptr) {
                    EmitVfxBridgeDiagnostic(
                        "play_rejected",
                        "owner_object_not_found",
                        {},
                        runtimeRequest.assetGuid.value);
                    return {};
                }
                runtimeRequest.ownerObject = owner;
            }
            switch (request.loopMode) {
            case GameProjectVfxLoopMode::UseAssetDefault:
                break;
            case GameProjectVfxLoopMode::OneShot:
                runtimeRequest.loopOverride = false;
                break;
            case GameProjectVfxLoopMode::Loop:
                runtimeRequest.loopOverride = true;
                break;
            default:
                EmitVfxBridgeDiagnostic(
                    "play_rejected",
                    "invalid_loop_mode",
                    {},
                    runtimeRequest.assetGuid.value);
                return {};
            }
            if (!ToVfxParameters(
                    request.parameters,
                    runtimeRequest.parameters)) {
                EmitVfxBridgeDiagnostic(
                    "play_rejected",
                    "malformed_parameter_override",
                    {},
                    runtimeRequest.assetGuid.value);
                return {};
            }

            const VFX::VfxPlayResult result = service->PlayDetailed(
                runtimeRequest);
            if (!result.IsAccepted()) {
                // PlayDetailed carries the authoritative rejection code.  Do
                // not expose its free-form resolver text, which may contain
                // a machine-local path; log only a stable classification.
                LogPlayRejection(
                    result,
                    runtimeRequest.assetGuid.value);
                return {};
            }
            return ToProjectVfxHandle(
                result.handle,
                service->GetPlaybackNamespace());
        }
        catch (...) {
            EmitVfxBridgeDiagnostic(
                "play_rejected",
                "bridge_exception");
            return {};
        }
    }

    bool VfxView::Stop(
        GameProjectVfxPlaybackHandle handle) const noexcept {
        try {
            VFX::VfxPlaybackService* service =
                FindVfxPlaybackService(static_cast<World*>(world_));
            if (service == nullptr) {
                return false;
            }
            VFX::VfxPlaybackHandle runtimeHandle{};
            return ToRuntimeVfxHandle(
                    handle,
                    service->GetPlaybackNamespace(),
                    runtimeHandle) &&
                service->Stop(runtimeHandle);
        }
        catch (...) {
            return false;
        }
    }

    bool VfxView::Update(
        GameProjectVfxPlaybackHandle handle,
        const GameProjectVfxUpdateRequest& request) const noexcept {
        try {
            if (world_ == nullptr) {
                EmitVfxBridgeDiagnostic(
                    "update_rejected",
                    "world_unavailable");
                return false;
            }
            if (HasNonZeroReserved(
                    request.reserved,
                    sizeof(request.reserved))) {
                EmitVfxBridgeDiagnostic(
                    "update_rejected",
                    "reserved_bytes_nonzero");
                return false;
            }
            if (request.updateTransform == 0u &&
                request.updateParameters == 0u) {
                EmitVfxBridgeDiagnostic(
                    "update_rejected",
                    "update_flags_empty");
                return false;
            }
            if (request.updateTransform > 1u ||
                request.updateParameters > 1u) {
                EmitVfxBridgeDiagnostic(
                    "update_rejected",
                    "update_flag_invalid");
                return false;
            }
            if (request.updateParameters == 0u &&
                request.parameters.count != 0u) {
                EmitVfxBridgeDiagnostic(
                    "update_rejected",
                    "parameters_without_update_flag");
                return false;
            }
            if (request.parameters.count >
                kGameProjectVfxMaximumParameterOverrides) {
                EmitVfxBridgeDiagnostic(
                    "update_rejected",
                    "parameter_override_limit_exceeded");
                return false;
            }
            VFX::VfxPlaybackService* service =
                FindVfxPlaybackService(static_cast<World*>(world_));
            if (service == nullptr) {
                EmitVfxBridgeDiagnostic(
                    "update_rejected",
                    "playback_service_unavailable");
                return false;
            }
            VFX::VfxPlaybackHandle runtimeHandle{};
            if (ToRuntimeVfxHandle(
                    handle,
                    service->GetPlaybackNamespace(),
                    runtimeHandle)) {
                if (!service->IsActive(runtimeHandle)) {
                    EmitVfxBridgeDiagnostic(
                        "update_rejected",
                        "inactive_handle");
                    return false;
                }
            }
            else {
                EmitVfxBridgeDiagnostic(
                    "update_rejected",
                    "invalid_handle");
                return false;
            }
            std::vector<VFX::VfxParameterOverride> parameters{};
            if (request.updateParameters != 0u &&
                !ToVfxParameters(request.parameters, parameters)) {
                EmitVfxBridgeDiagnostic(
                    "update_rejected",
                    "malformed_parameter_override");
                return false;
            }
            if (request.updateTransform != 0u &&
                !IsValidTransform(request.transform)) {
                EmitVfxBridgeDiagnostic(
                    "update_rejected",
                    "invalid_transform");
                return false;
            }
            // Validate and apply parameter overrides before touching the
            // transform.  SetParameters can reject an unknown input or an
            // incompatible value; in that case the transform remains
            // unchanged even though the service reports the rejected
            // parameter batch.  The fixed-buffer conversion above also
            // ensures malformed caller data never reaches the service.
            if (request.updateParameters != 0u &&
                !service->SetParameters(runtimeHandle, parameters)) {
                EmitVfxBridgeDiagnostic(
                    "update_rejected",
                    "parameter_update_rejected");
                return false;
            }
            if (request.updateTransform != 0u &&
                !service->SetTransform(
                    runtimeHandle,
                    ToVfxTransform(request.transform))) {
                EmitVfxBridgeDiagnostic(
                    "update_rejected",
                    "transform_update_rejected");
                return false;
            }
            return true;
        }
        catch (...) {
            EmitVfxBridgeDiagnostic(
                "update_rejected",
                "bridge_exception");
            return false;
        }
    }

    bool VfxView::IsActive(
        GameProjectVfxPlaybackHandle handle) const noexcept {
        try {
            VFX::VfxPlaybackService* service =
                FindVfxPlaybackService(static_cast<World*>(world_));
            VFX::VfxPlaybackHandle runtimeHandle{};
            return service != nullptr &&
                ToRuntimeVfxHandle(
                    handle,
                    service->GetPlaybackNamespace(),
                    runtimeHandle) &&
                service->IsActive(runtimeHandle);
        }
        catch (...) {
            return false;
        }
    }

    bool PresentationVfxView::SubmitWeaponTrailProfiled(
        const GameProjectWeaponTrailProfiledRequest& request)
        const noexcept {
        try {
            const GameProjectWeaponTrailSample& sample = request.sample;
            if (world_ == nullptr) {
                EmitVfxBridgeDiagnostic(
                    "weapon_trail_profiled_rejected",
                    "world_unavailable");
                return false;
            }
            if (sample.sourceId == 0u || sample.reserved != 0u ||
                !IsFinite(sample.bladeBaseWorld) ||
                !IsFinite(sample.bladeTipWorld) ||
                !IsFiniteNonNegative(sample.bodyColor) ||
                !IsFiniteNonNegative(sample.edgeColor) ||
                !IsFinitePositive(sample.lifetimeUnscaledSeconds) ||
                !IsFiniteNonNegative(sample.emissiveIntensity) ||
                !IsFiniteNonNegative(sample.opacity) ||
                sample.opacity > 1.0f ||
                !IsFiniteNonNegative(sample.flowStrength)) {
                EmitVfxBridgeDiagnostic(
                    "weapon_trail_profiled_rejected",
                    "malformed_public_input");
                return false;
            }
            World* world = static_cast<World*>(world_);
            const RuntimeObjectHandle owner = DecodeRuntimeHandle(
                sample.ownerObjectHandle);
            if (!owner.IsValid()) {
                EmitVfxBridgeDiagnostic(
                    "weapon_trail_profiled_rejected",
                    "invalid_owner_handle");
                return false;
            }
            if (world->FindObject(owner) == nullptr) {
                EmitVfxBridgeDiagnostic(
                    "weapon_trail_profiled_rejected",
                    "owner_object_not_found");
                return false;
            }
            VFX::CombatPresentationService* combat =
                world->Services().Find<VFX::CombatPresentationService>();
            VFX::VfxSpecialOutputData output{};
            if (combat == nullptr) {
                EmitVfxBridgeDiagnostic(
                    "weapon_trail_profiled_rejected",
                    "combat_presentation_service_unavailable");
                return false;
            }
            if (!ResolveProfileForPresentation(
                    *world,
                    request.profile,
                    VFX::VfxSpecialOutputType::WeaponPathDistortion,
                    output)) {
                return false;
            }
            VFX::WeaponTrailSampleRequest runtimeRequest{};
            runtimeRequest.ownerObject = owner;
            runtimeRequest.sourceId = sample.sourceId;
            runtimeRequest.bladeBaseWorld = ToRuntime(sample.bladeBaseWorld);
            runtimeRequest.bladeTipWorld = ToRuntime(sample.bladeTipWorld);
            runtimeRequest.bodyColor = ToRuntime(sample.bodyColor);
            runtimeRequest.edgeColor = ToRuntime(sample.edgeColor);
            runtimeRequest.lifetimeUnscaledSeconds =
                sample.lifetimeUnscaledSeconds;
            runtimeRequest.opacity = sample.opacity;
            runtimeRequest.emissiveIntensity = sample.emissiveIntensity;
            runtimeRequest.flowStrength = sample.flowStrength;
            return combat->SubmitWeaponTrail(
                runtimeRequest,
                output,
                TIME::GetFrameContext().frameIndex);
        }
        catch (...) {
            EmitVfxBridgeDiagnostic(
                "weapon_trail_profiled_rejected",
                "bridge_exception");
            return false;
        }
    }

    bool PresentationVfxView::TriggerPerfectGuardWaveProfiled(
        const GameProjectPerfectGuardWaveProfiledRequest& request)
        const noexcept {
        try {
            const GameProjectPerfectGuardWaveRequest& wave = request.request;
            if (world_ == nullptr) {
                EmitVfxBridgeDiagnostic(
                    "perfect_guard_profiled_rejected",
                    "world_unavailable");
                return false;
            }
            if (wave.sourceId == 0u ||
                (wave.flags &
                    ~kGameProjectPerfectGuardWaveKnownFlags) != 0u ||
                !IsFinite(wave.worldCenter) ||
                !IsFiniteNonNegative(wave.color) ||
                !IsFinitePositive(wave.durationUnscaledSeconds) ||
                !IsFinitePositive(wave.maximumScreenRadius) ||
                !IsFinitePositive(wave.ringThickness) ||
                !IsFiniteNonNegative(wave.distortionStrength) ||
                !IsFiniteNonNegative(wave.chromaticAberration) ||
                !IsFiniteNonNegative(wave.flashIntensity) ||
                !IsFinitePositive(wave.intensity)) {
                EmitVfxBridgeDiagnostic(
                    "perfect_guard_profiled_rejected",
                    "malformed_public_input");
                return false;
            }
            World* world = static_cast<World*>(world_);
            VFX::ActionPostReactionService* reaction =
                world->Services().Find<VFX::ActionPostReactionService>();
            VFX::VfxSpecialOutputData output{};
            if (reaction == nullptr) {
                EmitVfxBridgeDiagnostic(
                    "perfect_guard_profiled_rejected",
                    "action_post_reaction_service_unavailable");
                return false;
            }
            if (!ResolveProfileForPresentation(
                    *world,
                    request.profile,
                    VFX::VfxSpecialOutputType::ScreenImpulse,
                    output)) {
                return false;
            }
            VFX::ActionPostReactionService::PerfectGuardShockwaveRequest
                runtimeRequest{};
            runtimeRequest.sourceId = wave.sourceId;
            runtimeRequest.sequence = wave.sequence;
            runtimeRequest.frameIndex = TIME::GetFrameContext().frameIndex;
            runtimeRequest.worldPosition = ToRuntime(wave.worldCenter);
            runtimeRequest.color = ToRuntime(wave.color);
            runtimeRequest.durationSeconds = wave.durationUnscaledSeconds;
            runtimeRequest.maximumScreenRadius = wave.maximumScreenRadius;
            runtimeRequest.ringThickness = wave.ringThickness;
            runtimeRequest.distortionStrength = wave.distortionStrength;
            runtimeRequest.chromaticShift = wave.chromaticAberration;
            runtimeRequest.flashIntensity = wave.flashIntensity;
            runtimeRequest.intensity = wave.intensity;
            runtimeRequest.flags = wave.flags;
            return reaction->TriggerPerfectGuardShockwave(
                runtimeRequest,
                output);
        }
        catch (...) {
            EmitVfxBridgeDiagnostic(
                "perfect_guard_profiled_rejected",
                "bridge_exception");
            return false;
        }
    }

    bool PresentationVfxView::CaptureAfterimageProfiled(
        const GameProjectAfterimageProfiledRequest& request)
        const noexcept {
        try {
            const GameProjectCombatWorldEffectRequest& effect =
                request.request;
            if (world_ == nullptr) {
                EmitVfxBridgeDiagnostic(
                    "afterimage_profiled_rejected",
                    "world_unavailable");
                return false;
            }
            if (effect.sourceId == 0u ||
                effect.effect != GameProjectCombatEffectType::DodgeAfterimage ||
                HasNonZeroReserved(
                    effect.reserved,
                    sizeof(effect.reserved)) ||
                !IsValidTransform(effect.transform) ||
                !IsFiniteNonZeroDirection(effect.direction) ||
                !IsFinitePositive(effect.scale) ||
                !IsFinitePositive(effect.intensity)) {
                EmitVfxBridgeDiagnostic(
                    "afterimage_profiled_rejected",
                    "malformed_public_input");
                return false;
            }
            World* world = static_cast<World*>(world_);
            const RuntimeObjectHandle owner = DecodeRuntimeHandle(
                effect.ownerObjectHandle);
            if (!owner.IsValid()) {
                EmitVfxBridgeDiagnostic(
                    "afterimage_profiled_rejected",
                    "invalid_owner_handle");
                return false;
            }
            if (world->FindObject(owner) == nullptr) {
                EmitVfxBridgeDiagnostic(
                    "afterimage_profiled_rejected",
                    "owner_object_not_found");
                return false;
            }
            VFX::CombatAfterimageService* afterimages =
                world->Services().Find<VFX::CombatAfterimageService>();
            VFX::VfxSpecialOutputData output{};
            if (afterimages == nullptr) {
                EmitVfxBridgeDiagnostic(
                    "afterimage_profiled_rejected",
                    "combat_afterimage_service_unavailable");
                return false;
            }
            if (!ResolveProfileForPresentation(
                    *world,
                    request.profile,
                    VFX::VfxSpecialOutputType::SkinnedAfterimage,
                    output)) {
                return false;
            }
            VFX::CombatAfterimageRequest runtimeRequest{};
            runtimeRequest.ownerObject = owner;
            runtimeRequest.sourceId = effect.sourceId;
            runtimeRequest.sequence =
                static_cast<uint64_t>(effect.randomSeed);
            runtimeRequest.randomSeed = effect.randomSeed;
            runtimeRequest.frameIndex = TIME::GetFrameContext().frameIndex;
            runtimeRequest.intensity = effect.intensity;
            runtimeRequest.uniformScale = effect.scale;
            runtimeRequest.silhouetteCount =
                effect.intensity >= 1.35f ? 5u : 4u;
            runtimeRequest.lifetimeUnscaledSeconds = std::clamp(
                0.24f + effect.intensity * 0.055f,
                0.22f,
                0.36f);
            runtimeRequest.opacity = std::clamp(
                0.50f + effect.intensity * 0.11f,
                0.46f,
                0.78f);
            runtimeRequest.fresnelPower = 2.75f;
            runtimeRequest.dissolveStrength = 1.15f;
            runtimeRequest.shellOffsetWorld = 0.009f;
            runtimeRequest.color = { 0.08f, 0.66f, 1.0f };
            return afterimages->Capture(*world, runtimeRequest, output);
        }
        catch (...) {
            EmitVfxBridgeDiagnostic(
                "afterimage_profiled_rejected",
                "bridge_exception");
            return false;
        }
    }

} // namespace HIKARI
