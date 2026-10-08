#include "HIKARI/Project/EnvironmentView.h"
#include "HIKARI/Project/WorldView.h"

#include <cmath>
#include <cstring>
#include <string>
#include <type_traits>

#include "Assets/RenderStyle/HIKARI_RenderStyleAssetStore.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/Environment/HIKARI_EnvironmentRuntime.h"

namespace HIKARI {
namespace {
using ENVIRONMENT::EnvironmentRuntime;
using PROJECT::INTERNAL::MakeWorldResult;

// 存档の中身。内部の状態をそのまま写すので、同じエンジン版の間だけ互換。
struct SnapshotPayload {
    ENVIRONMENT::EnvironmentSnapshot state{};
    char weatherId[128]{};
    char renderStyleId[64]{};
};
static_assert(std::is_trivially_copyable_v<SnapshotPayload>);
static_assert(sizeof(SnapshotPayload) <= kGameProjectEnvironmentSnapshotCapacity);
constexpr uint32_t kSnapshotLayoutVersion = 3u;
constexpr double kMaximumTimeScale = 100000.0;

GameProjectWorldResult Resolve(GameProjectWorldHandle identity, EnvironmentRuntime*& runtime) noexcept {
    RuntimeWorldAccessResult access{};
    World* world = World::ResolveRuntimeWorld(identity.value, &access);
    if (world == nullptr) {
        return PROJECT::INTERNAL::MakeWorldAccessFailure(access);
    }
    if (GameProjectSdkAccess::IsWorldClosing(*world)) {
        return MakeWorldResult(GameProjectWorldResultCode::WorldClosing);
    }
    runtime = world->Services().Find<EnvironmentRuntime>();
    return MakeWorldResult(runtime != nullptr ? GameProjectWorldResultCode::Success
                                              : GameProjectWorldResultCode::Unsupported);
}

GameProjectFloat3 ToFloat3(const MATH::Vec3& value) noexcept { return {value.x, value.y, value.z}; }
} // namespace

EnvironmentView WorldView::GetEnvironment() const noexcept { return EnvironmentView(world_); }

GameProjectWorldResult EnvironmentView::GetState(GameProjectEnvironmentState& outState) const noexcept {
    EnvironmentRuntime* runtime = nullptr;
    GameProjectWorldResult result = Resolve(world_, runtime);
    if (!result.Succeeded()) {
        return result;
    }
    const ENVIRONMENT::EnvironmentFrameState state = runtime->Simulation().GetFrameState();
    const EnvironmentLightingFrame frame =
        ENVIRONMENT::BuildLightingFrame(runtime->GetProfile(), state, runtime->Simulation().GetSnapshot().location);
    outState = {};
    outState.time = {state.time.year, state.time.month, state.time.day, state.time.localHours};
    outState.timeScale = runtime->Simulation().GetSnapshot().timeScale;
    outState.sunDirection = ToFloat3(frame.sun.toLight);
    outState.sunIlluminance = frame.sun.illuminance * frame.sun.horizonVisibility;
    outState.moonDirection = ToFloat3(frame.moon.toLight);
    outState.moonIlluminance = frame.moon.illuminance * frame.moon.horizonVisibility;
    outState.sunAltitudeDegrees = state.celestial.sunAltitudeDegrees;
    outState.surfaceWetness = state.surface.wetness;
    outState.surfacePuddles = state.surface.puddles;
    outState.surfaceSnowCover = state.surface.snowCover;
    outState.weatherTransition = state.weatherTransitionProgress;
    outState.lightningFlash = state.lightningFlash;
    outState.moonIsPrimary = frame.moonIsPrimary;
    return result;
}

GameProjectWorldResult EnvironmentView::SetTime(const GameProjectEnvironmentTime& time) const noexcept {
    EnvironmentRuntime* runtime = nullptr;
    GameProjectWorldResult result = Resolve(world_, runtime);
    if (!result.Succeeded()) {
        return result;
    }
    const ENVIRONMENT::CalendarTime calendar{time.year, time.month, time.day, time.localHours};
    if (!std::isfinite(time.localHours) || time.localHours < 0.0 || time.localHours >= 24.0 ||
        !ENVIRONMENT::IsValidCalendarTime(calendar)) {
        return MakeWorldResult(GameProjectWorldResultCode::InvalidRequest);
    }
    runtime->Simulation().SetTime(calendar);
    return result;
}

GameProjectWorldResult EnvironmentView::SetTimeScale(double timeScale) const noexcept {
    EnvironmentRuntime* runtime = nullptr;
    GameProjectWorldResult result = Resolve(world_, runtime);
    if (!result.Succeeded()) {
        return result;
    }
    if (!std::isfinite(timeScale) || timeScale < 0.0 || timeScale > kMaximumTimeScale) {
        return MakeWorldResult(GameProjectWorldResultCode::InvalidRequest);
    }
    runtime->Simulation().SetTimeScale(timeScale);
    return result;
}

GameProjectWorldResult EnvironmentView::RequestWeather(std::string_view weatherId,
                                                       float transitionSeconds) const noexcept {
    EnvironmentRuntime* runtime = nullptr;
    GameProjectWorldResult result = Resolve(world_, runtime);
    if (!result.Succeeded()) {
        return result;
    }
    if (weatherId.empty() || !MATH::IsFinite(transitionSeconds)) {
        return MakeWorldResult(GameProjectWorldResultCode::InvalidRequest);
    }
    try {
        std::string error;
        if (!runtime->RequestWeather(weatherId, transitionSeconds, error)) {
            return MakeWorldResult(GameProjectWorldResultCode::InvalidRequest);
        }
    } catch (...) {
        return MakeWorldResult(GameProjectWorldResultCode::InternalError);
    }
    return result;
}

std::string_view EnvironmentView::GetWeatherId() const noexcept {
    EnvironmentRuntime* runtime = nullptr;
    return Resolve(world_, runtime).Succeeded() ? std::string_view(runtime->GetWeatherId()) : std::string_view{};
}

GameProjectWorldResult EnvironmentView::SetRenderStyle(std::string_view styleId) const noexcept {
    EnvironmentRuntime* runtime = nullptr;
    GameProjectWorldResult result = Resolve(world_, runtime);
    if (!result.Succeeded()) {
        return result;
    }
    try {
        RuntimeWorldAccessResult access{};
        World* world = World::ResolveRuntimeWorld(world_.value, &access);
        const auto* styles = world ? world->Services().Find<RENDERSTYLE::RenderStyleAssetStore>() : nullptr;
        std::string error;
        if (styles == nullptr) {
            return MakeWorldResult(GameProjectWorldResultCode::Unsupported);
        }
        if (styleId.empty() || !styles->Load(styleId, error)) {
            return MakeWorldResult(GameProjectWorldResultCode::InvalidRequest);
        }
        runtime->SetRenderStyle(styleId);
    } catch (...) {
        return MakeWorldResult(GameProjectWorldResultCode::InternalError);
    }
    return result;
}

std::string_view EnvironmentView::GetRenderStyleId() const noexcept {
    EnvironmentRuntime* runtime = nullptr;
    return Resolve(world_, runtime).Succeeded() ? std::string_view(runtime->GetRenderStyleId()) : std::string_view{};
}

GameProjectWorldResult EnvironmentView::CaptureSnapshot(GameProjectEnvironmentSnapshot& outSnapshot) const noexcept {
    EnvironmentRuntime* runtime = nullptr;
    GameProjectWorldResult result = Resolve(world_, runtime);
    if (!result.Succeeded()) {
        return result;
    }
    const std::string& weatherId = runtime->GetWeatherId();
    const std::string& renderStyleId = runtime->GetRenderStyleId();
    SnapshotPayload payload{};
    if (weatherId.size() >= sizeof(payload.weatherId) || renderStyleId.size() >= sizeof(payload.renderStyleId)) {
        return MakeWorldResult(GameProjectWorldResultCode::Unsupported);
    }
    payload.state = runtime->Simulation().GetSnapshot();
    std::memcpy(payload.weatherId, weatherId.data(), weatherId.size());
    std::memcpy(payload.renderStyleId, renderStyleId.data(), renderStyleId.size());
    outSnapshot = {};
    outSnapshot.layoutVersion = kSnapshotLayoutVersion;
    outSnapshot.byteCount = sizeof(payload);
    std::memcpy(outSnapshot.bytes, &payload, sizeof(payload));
    return result;
}

GameProjectWorldResult EnvironmentView::RestoreSnapshot(const GameProjectEnvironmentSnapshot& snapshot) const noexcept {
    EnvironmentRuntime* runtime = nullptr;
    GameProjectWorldResult result = Resolve(world_, runtime);
    if (!result.Succeeded()) {
        return result;
    }
    if (snapshot.structSize != sizeof(GameProjectEnvironmentSnapshot) ||
        snapshot.layoutVersion != kSnapshotLayoutVersion || snapshot.byteCount != sizeof(SnapshotPayload)) {
        return MakeWorldResult(GameProjectWorldResultCode::InvalidRequest);
    }
    SnapshotPayload payload{};
    std::memcpy(&payload, snapshot.bytes, sizeof(payload));
    payload.weatherId[sizeof(payload.weatherId) - 1u] = '\0';
    payload.renderStyleId[sizeof(payload.renderStyleId) - 1u] = '\0';
    try {
        if (!runtime->Restore(payload.state, payload.weatherId, payload.renderStyleId)) {
            return MakeWorldResult(GameProjectWorldResultCode::InvalidRequest);
        }
    } catch (...) {
        return MakeWorldResult(GameProjectWorldResultCode::InternalError);
    }
    return result;
}

} // namespace HIKARI
