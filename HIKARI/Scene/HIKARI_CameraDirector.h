#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Render3D/Core/HIKARI_RenderView.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    class World;

    enum class CameraBlendMode : uint8_t {
        Cut,
        EaseInOut,
    };

    struct CameraBlendDesc {
        CameraBlendMode mode = CameraBlendMode::Cut;
        float durationSeconds = 0.0f;
    };

    struct CameraActivationRequest {
        SceneObjectId cameraObjectId{};
        CameraBlendDesc blend{};
        int priority = 0;
        bool affectsControlBasis = false;
    };

    struct CameraOverrideToken {
        uint64_t value = 0;
        uint64_t epoch = 0;

        bool IsValid() const noexcept {
            return value != 0 && epoch != 0;
        }
    };

    struct CameraDirectorStatus {
        SceneObjectId baseCameraObjectId{};
        SceneObjectId activeSourceCameraObjectId{};
        size_t overrideCount = 0u;
        bool blending = false;
        bool activeOverrideAffectsControlBasis = false;
    };

    class CameraDirector {
    public:
        void Reset();
        void SetBaseCamera(SceneObjectId cameraObjectId);
        SceneObjectId GetBaseCamera() const noexcept;

        CameraOverrideToken PushOverride(const CameraActivationRequest& request);
        bool ReleaseOverride(CameraOverrideToken token);
        bool SetOverrideCamera(
            CameraOverrideToken token,
            const Camera3D& camera);
        bool ClearOverrideCamera(CameraOverrideToken token);
        bool HasActiveOverride() const noexcept;

        const RENDER3D::ResolvedCameraFrame& Resolve(
            const World& world,
            const Camera3D& fallbackCamera,
            float aspect,
            float deltaTime);

        const RENDER3D::ResolvedCameraFrame& GetResolvedFrame() const noexcept;
        const Camera3D& GetControlCamera() const noexcept;
        CameraDirectorStatus GetStatus() const noexcept;
        bool TryResolveCameraObject(
            const World& world,
            SceneObjectId cameraObjectId,
            float aspect,
            Camera3D& outCamera) const;

    private:
        struct OverrideEntry {
            CameraOverrideToken token{};
            CameraActivationRequest request{};
            Camera3D cameraOverride{};
            bool hasCameraOverride = false;
            uint64_t insertionOrder = 0;
        };

        const OverrideEntry* FindWinningOverride() const;
        bool BeginSourceTransition(
            SceneObjectId sourceCameraObjectId,
            const Camera3D& targetCamera,
            const CameraBlendDesc& blend);
        void AdvanceBlend(const Camera3D& targetCamera, float deltaTime);

        SceneObjectId baseCameraObjectId_{};
        SceneObjectId activeSourceCameraObjectId_{};
        CameraOverrideToken activeOverrideToken_{};
        std::vector<OverrideEntry> overrides_{};

        RENDER3D::ResolvedCameraFrame resolvedFrame_{};
        Camera3D controlCamera_{};
        Camera3D blendStartCamera_{};
        CameraBlendDesc activeBlend_{};
        float blendElapsedSeconds_ = 0.0f;
        bool blendActive_ = false;
        bool activeOverrideAffectsControlBasis_ = false;

        uint64_t revision_ = 0;
        uint64_t epoch_ = 1;
        uint64_t nextTokenValue_ = 1;
        uint64_t nextInsertionOrder_ = 1;
    };

} // namespace HIKARI
