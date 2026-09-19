#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Vfx/HIKARI_VfxProgram.h"
#include "Assets/Vfx/HIKARI_VfxValidation.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI::VFX {

    struct VfxPlaybackHandle {
        static constexpr uint32_t kInvalidSlot =
            (std::numeric_limits<uint32_t>::max)();

        uint32_t slot = kInvalidSlot;
        uint32_t generation = 0u;

        bool IsValid() const noexcept {
            return slot != kInvalidSlot && generation != 0u;
        }
        bool operator==(const VfxPlaybackHandle&) const noexcept = default;
    };

    struct VfxWorldTransform {
        MATH::Vec3 position{ 0.0f, 0.0f, 0.0f };
        MATH::Quat rotation = MATH::Quat::Identity();
        MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
    };

    // Scene-owned attachment lookup.  Runtime VFX can follow an animation
    // socket without teaching Render3D about GameObject, model, or pose
    // ownership.  The callback is installed by the world/system owner and
    // is never serialized into a playback request or a render frame source.
    using VfxAttachmentResolver = std::function<bool(
        RuntimeObjectHandle,
        std::string_view,
        VfxWorldTransform&)>;

    struct VfxParameterOverride {
        std::string inputId{};
        VfxValue value{};
    };

    struct VfxPlayRequest {
        AssetGuid assetGuid{};
        std::shared_ptr<const VfxProgram> program{};
        RuntimeObjectHandle ownerObject{};
        VfxWorldTransform transform{};
        std::optional<bool> loopOverride{};
        uint32_t randomSeed = 0u;
        std::vector<VfxParameterOverride> parameters{};
    };

    struct VfxPlaybackDiagnostic {
        VfxDiagnosticSeverity severity = VfxDiagnosticSeverity::Error;
        std::string code{};
        std::string message{};
    };

    struct VfxPlayResult {
        VfxPlaybackHandle handle{};
        std::vector<VfxPlaybackDiagnostic> diagnostics{};

        bool IsAccepted() const noexcept {
            return handle.IsValid();
        }
    };

    struct VfxSeekResult {
        bool accepted = false;
        bool complete = false;
        float currentSeconds = 0.0f;
        uint32_t executedSteps = 0u;
        std::vector<VfxPlaybackDiagnostic> diagnostics{};
    };

    // Light 特殊出力のサンプル。描画側はこの値だけで SceneLightProvider を
    // 組み立てられるようにし、再生側の内部状態へ触らせない。
    struct VfxLayerLightSample {
        VfxPlaybackHandle handle{};
        uint32_t emitterIndex = 0u;
        MATH::Vec3 position{ 0.0f, 0.0f, 0.0f };
        MATH::Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float intensity = 0.0f;
        float range = 0.0f;
        float sourceRadius = 0.0f;
        float flickerFrequency = 0.0f;
        float flickerAmplitude = 0.0f;
        // 点滅の位相に使うレイヤー内時刻。wall clock では停止やシークで
        // ちらつきが飛ぶため、再生側の時間をそのまま渡す。
        float ageSeconds = 0.0f;
        uint32_t priority = 0u;
        bool castsShadows = false;
        uint32_t outputIndex = 0u;
    };

    // Mesh 出力レイヤーのサンプル。描画側はこの値だけで scene render cache の
    // 一時オブジェクトを組み立てられる。
    struct VfxLayerMeshSample {
        uint64_t playbackNamespace = 0u;
        VfxPlaybackHandle handle{};
        uint32_t emitterIndex = 0u;
        std::string meshAssetGuid{};
        std::string materialAssetGuid{};
        VfxWorldTransform worldTransform{};
        uint32_t flags = 0u;
    };

} // namespace HIKARI::VFX
