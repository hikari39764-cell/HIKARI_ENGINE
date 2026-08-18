#include "Scene/Vfx/HIKARI_GhostPossessionSystem.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

#include "Core/HIKARI_FrameContext.h"
#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
    namespace {
        bool EqualFloat4(
            const DirectX::XMFLOAT4& lhs,
            const DirectX::XMFLOAT4& rhs) noexcept {

            constexpr float kEpsilon = 1.0e-6f;
            return std::abs(lhs.x - rhs.x) <= kEpsilon &&
                std::abs(lhs.y - rhs.y) <= kEpsilon &&
                std::abs(lhs.z - rhs.z) <= kEpsilon &&
                std::abs(lhs.w - rhs.w) <= kEpsilon;
        }

        bool EqualValues(
            const DirectX::XMFLOAT4
                (&lhs)[VFX::kMaterialFxUserCount],
            const DirectX::XMFLOAT4
                (&rhs)[VFX::kMaterialFxUserCount]) noexcept {

            for (size_t index = 0u;
                index < VFX::kMaterialFxUserCount;
                ++index) {
                if (!EqualFloat4(lhs[index], rhs[index])) {
                    return false;
                }
            }
            return true;
        }

        float MoveToward(
            float current,
            float target,
            float duration,
            float deltaSeconds) noexcept {

            if (duration <= 1.0e-6f) {
                return target;
            }
            const float step = std::max(deltaSeconds, 0.0f) / duration;
            if (current < target) {
                return std::min(current + step, target);
            }
            return std::max(current - step, target);
        }

        void RequestPlay(
            VfxPlayerComponent* player,
            VfxSlotId slotId) {

            if (player != nullptr && slotId.IsValid()) {
                (void)player->RequestPlay(slotId);
            }
        }

        void RequestStop(
            VfxPlayerComponent* player,
            VfxSlotId slotId) {

            if (player != nullptr && slotId.IsValid()) {
                (void)player->RequestStop(slotId);
            }
        }
    }

    void GhostPossessionSystem::OnWorldAttached(World& world) {
        states_.clear();
        runtimePlayState_ = world.Services().Find<
            RuntimePlayStateService>();
    }

    void GhostPossessionSystem::OnWorldDetached(World& world) {
        for (auto& [handle, state] : states_) {
            GameObject* object = world.FindObject(handle);
            if (object == nullptr) {
                continue;
            }
            if (MaterialFxComponent* materialFx =
                    object->GetComponent<MaterialFxComponent>()) {
                RestoreSurface(*materialFx, state);
            }
        }
        states_.clear();
        runtimePlayState_ = nullptr;
    }

    void GhostPossessionSystem::Update(
        World& world,
        const FrameContext& frame) {

        const bool runtimeActive =
            runtimePlayState_ != nullptr &&
            runtimePlayState_->IsActive();
        float deltaSeconds = runtimeActive
            ? frame.gameDt
            : frame.unscaledDt;
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f) {
            deltaSeconds = 0.0f;
        }

        std::unordered_set<RuntimeObjectHandle, RuntimeHandleHash> seen{};
        world.ForEachObjectWith<
            GhostPossessionComponent,
            MaterialFxComponent>(
            [this, runtimeActive, deltaSeconds, &seen](
                GameObject& object,
                GhostPossessionComponent& component,
                MaterialFxComponent& materialFx) {

                const GhostPossessionSettings& settings =
                    component.GetSettings();
                const bool requested =
                    settings.enabled && settings.possessed;
                const bool surfaceTarget = requested &&
                    (runtimeActive || settings.previewInEditor);
                const bool vfxTarget = requested && runtimeActive;
                const RuntimeObjectHandle handle =
                    object.GetRuntimeHandle();
                auto found = states_.find(handle);
                if (!surfaceTarget && !vfxTarget &&
                    found == states_.end()) {
                    return;
                }

                if (found == states_.end()) {
                    RuntimeState state{};
                    CaptureSurface(materialFx, state);
                    found = states_.emplace(
                        handle,
                        std::move(state)).first;
                }
                seen.insert(handle);
                RuntimeState& state = found->second;
                state.finished = false;

                VfxPlayerComponent* player =
                    object.GetComponent<VfxPlayerComponent>();
                UpdateVfx(
                    player,
                    settings,
                    vfxTarget,
                    runtimeActive,
                    state);

                const float targetWeight = surfaceTarget ? 1.0f : 0.0f;
                const float duration = surfaceTarget
                    ? settings.blendInSeconds
                    : settings.blendOutSeconds;
                state.blendWeight = MoveToward(
                    state.blendWeight,
                    targetWeight,
                    duration,
                    deltaSeconds);

                if (surfaceTarget || state.blendWeight > 0.0f) {
                    ApplySurface(materialFx, settings, state);
                } else {
                    RestoreSurface(materialFx, state);
                    state.finished = true;
                }
            });

        for (auto found = states_.begin(); found != states_.end();) {
            const bool disappeared = !seen.contains(found->first);
            if (!disappeared && !found->second.finished) {
                ++found;
                continue;
            }

            if (disappeared) {
                if (GameObject* object = world.FindObject(found->first)) {
                    StopRemovedVfx(
                        object->GetComponent<VfxPlayerComponent>(),
                        runtimeActive,
                        found->second);
                    if (MaterialFxComponent* materialFx =
                            object->GetComponent<MaterialFxComponent>()) {
                        RestoreSurface(*materialFx, found->second);
                    }
                }
            }
            found = states_.erase(found);
        }
    }

    void GhostPossessionSystem::CaptureSurface(
        const MaterialFxComponent& materialFx,
        RuntimeState& state) {

        state.previousProfileId = materialFx.GetProfileId();
        state.previousValuesInitialized =
            materialFx.AreValuesInitialized();
        const auto& values = materialFx.GetParamValues();
        for (size_t index = 0u;
            index < VFX::kMaterialFxUserCount;
            ++index) {
            state.previousValues[index] = values[index];
        }
    }

    void GhostPossessionSystem::ApplySurface(
        MaterialFxComponent& materialFx,
        const GhostPossessionSettings& settings,
        RuntimeState& state) {

        if (settings.materialFxProfileId.empty()) {
            RestoreSurface(materialFx, state);
            return;
        }
        if (materialFx.GetProfileId() !=
            settings.materialFxProfileId) {
            materialFx.SetProfileId(settings.materialFxProfileId);
        }

        DirectX::XMFLOAT4 desired[VFX::kMaterialFxUserCount]{};
        ResolveGhostPossessionMaterialValues(
            settings,
            state.blendWeight,
            desired);
        if (!materialFx.AreValuesInitialized() ||
            !EqualValues(materialFx.GetParamValues(), desired)) {
            materialFx.SetParamValues(desired, true);
        }
    }

    void GhostPossessionSystem::RestoreSurface(
        MaterialFxComponent& materialFx,
        RuntimeState& state) {

        if (materialFx.GetProfileId() != state.previousProfileId) {
            materialFx.SetProfileId(state.previousProfileId);
        }
        if (materialFx.AreValuesInitialized() !=
                state.previousValuesInitialized ||
            !EqualValues(
                materialFx.GetParamValues(),
                state.previousValues)) {
            materialFx.SetParamValues(
                state.previousValues,
                state.previousValuesInitialized);
        }
    }

    void GhostPossessionSystem::UpdateVfx(
        VfxPlayerComponent* player,
        const GhostPossessionSettings& settings,
        bool desiredActive,
        bool runtimeActive,
        RuntimeState& state) {

        if (!runtimeActive) {
            state.vfxActive = false;
            state.enterSlotId = settings.enterSlotId;
            state.loopSlotId = settings.loopSlotId;
            state.exitSlotId = settings.exitSlotId;
            return;
        }

        if (desiredActive != state.vfxActive) {
            if (desiredActive) {
                RequestPlay(player, settings.enterSlotId);
                if (settings.loopSlotId != settings.enterSlotId) {
                    RequestPlay(player, settings.loopSlotId);
                }
            } else {
                RequestStop(player, state.loopSlotId);
                RequestPlay(player, settings.exitSlotId);
            }
            state.vfxActive = desiredActive;
        } else if (desiredActive &&
            state.loopSlotId != settings.loopSlotId) {
            RequestStop(player, state.loopSlotId);
            RequestPlay(player, settings.loopSlotId);
        }

        state.enterSlotId = settings.enterSlotId;
        state.loopSlotId = settings.loopSlotId;
        state.exitSlotId = settings.exitSlotId;
    }

    void GhostPossessionSystem::StopRemovedVfx(
        VfxPlayerComponent* player,
        bool runtimeActive,
        RuntimeState& state) {

        if (runtimeActive && state.vfxActive) {
            RequestStop(player, state.loopSlotId);
            RequestPlay(player, state.exitSlotId);
        }
        state.vfxActive = false;
    }

    size_t GhostPossessionSystem::RuntimeHandleHash::operator()(
        RuntimeObjectHandle handle) const noexcept {

        return static_cast<size_t>(handle.ToValue());
    }

} // namespace HIKARI
