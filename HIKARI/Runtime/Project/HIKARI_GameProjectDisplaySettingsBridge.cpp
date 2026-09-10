#include "HIKARI/Project/DisplaySettingsView.h"
#include "HIKARI/Project/GameFlowViews.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Runtime/Presentation/HIKARI_GamePresentationService.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_World.h"
#include <algorithm>

namespace HIKARI {
    namespace {
        World* ResolveDisplayWorld(const WorldView& view) noexcept {
            World* world = GameProjectSdkAccess::GetWorld(view);
            return world != nullptr && RuntimeSceneContext::GetCurrentWorld() == world &&
                !GameProjectSdkAccess::IsWorldClosing(*world) ? world : nullptr;
        }

        bool ValidValues(const GameProjectDisplaySettings& value) noexcept {
            return static_cast<uint8_t>(value.antiAliasing) <= 4 &&
                static_cast<uint8_t>(value.renderResolution) <= 4 &&
                static_cast<uint8_t>(value.windowResolution) >= 1 &&
                static_cast<uint8_t>(value.windowResolution) <= 4 &&
                static_cast<uint8_t>(value.windowMode) <= 2 &&
                static_cast<uint8_t>(value.dlssQuality) <= 3 &&
                value.frameGeneration <= 1 && value.vSync <= 1 && value.reserved == 0;
        }

        static_assert(static_cast<int>(GameProjectAntiAliasing::DLSS) ==
            static_cast<int>(RENDER3D::RenderAntiAliasingMode::DLSS));
        static_assert(static_cast<int>(GameProjectDisplayResolution::P2160) ==
            static_cast<int>(RENDER3D::RenderResolutionPreset::P2160));
        static_assert(static_cast<int>(GameProjectWindowMode::Fullscreen) ==
            static_cast<int>(RENDER3D::WindowPresentationMode::Fullscreen));
        static_assert(static_cast<int>(GameProjectDlssQuality::UltraPerformance) ==
            static_cast<int>(RENDER3D::DlssQualityMode::UltraPerformance));
    }

    DisplaySettingsView WorldView::GetDisplaySettings() const noexcept {
        return DisplaySettingsView(*this);
    }

    bool DisplaySettingsView::GetSettings(GameProjectDisplaySettings& out) const noexcept {
        out = {};
        try {
            World* world = ResolveDisplayWorld(world_);
            if (world == nullptr || !world->IsOwnerThread()) return false;
            const auto& settings = RENDER3D::GetRenderQualitySettings();
            out.antiAliasing = static_cast<GameProjectAntiAliasing>(settings.antiAliasingMode);
            out.renderResolution = static_cast<GameProjectDisplayResolution>(settings.sceneResolution);
            out.windowResolution = static_cast<GameProjectDisplayResolution>(settings.windowSize);
            out.windowMode = static_cast<GameProjectWindowMode>(settings.windowMode);
            out.dlssQuality = static_cast<GameProjectDlssQuality>(settings.dlssQualityMode);
            out.frameGeneration = settings.frameGenerationMode == RENDER3D::RenderFrameGenerationMode::Dlss;
            out.vSync = settings.vSync;
            return true;
        } catch (...) { return false; }
    }

    bool DisplaySettingsView::GetCapabilities(GameProjectDisplayCapabilities& out) const noexcept {
        out = {};
        try {
            World* world = ResolveDisplayWorld(world_);
            if (world == nullptr || !world->IsOwnerThread()) return false;
            for (uint32_t mode = 0; mode <= 4; ++mode) {
                if (RENDER3D::IsAntiAliasingModeAvailable(static_cast<RENDER3D::RenderAntiAliasingMode>(mode)))
                    out.antiAliasingMask |= 1u << mode;
            }
            out.frameGenerationSupported = RENDER3D::IsFrameGenerationModeAvailable(
                RENDER3D::RenderFrameGenerationMode::Dlss);
            out.gameWindowActive = SERVICES::IsGamePresentationActive();
            out.outputWidth = static_cast<uint32_t>((std::max)(0, SERVICES::GetLogicalScreenWidth()));
            out.outputHeight = static_cast<uint32_t>((std::max)(0, SERVICES::GetLogicalScreenHeight()));
            return true;
        } catch (...) { return false; }
    }

    GameProjectDisplayApplyResult DisplaySettingsView::Apply(const GameProjectDisplaySettings& value) const noexcept {
        using Result = GameProjectDisplayApplyResult;
        try {
            World* world = ResolveDisplayWorld(world_);
            if (world == nullptr) return Result::InvalidWorld;
            if (!world->IsOwnerThread()) return Result::WrongThread;
            if (!world_.GetRuntimeSession().IsPlayActive() || !SERVICES::IsGamePresentationActive())
                return Result::NotPlaying;
            if (!ValidValues(value)) return Result::InvalidValue;
            GameProjectDisplayCapabilities capabilities{};
            if (!GetCapabilities(capabilities)) return Result::InvalidWorld;
            if (!capabilities.Supports(value.antiAliasing) ||
                (value.frameGeneration && !capabilities.frameGenerationSupported)) return Result::Unsupported;
            auto settings = RENDER3D::GetRenderQualitySettings();
            settings.antiAliasingMode = static_cast<RENDER3D::RenderAntiAliasingMode>(value.antiAliasing);
            settings.sceneResolution = static_cast<RENDER3D::RenderResolutionPreset>(value.renderResolution);
            settings.windowSize = static_cast<RENDER3D::RenderResolutionPreset>(value.windowResolution);
            settings.windowMode = static_cast<RENDER3D::WindowPresentationMode>(value.windowMode);
            settings.dlssQualityMode = static_cast<RENDER3D::DlssQualityMode>(value.dlssQuality);
            settings.frameGenerationMode = value.frameGeneration ? RENDER3D::RenderFrameGenerationMode::Dlss
                : RENDER3D::RenderFrameGenerationMode::Off;
            settings.vSync = value.vSync != 0;
            return SERVICES::QueueRenderQualitySettings(settings) ? Result::Accepted : Result::NotPlaying;
        } catch (...) { return Result::InvalidWorld; }
    }
}
