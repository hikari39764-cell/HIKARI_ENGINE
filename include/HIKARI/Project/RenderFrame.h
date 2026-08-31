#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace HIKARI {

    class IGameSystem;

    inline constexpr uint32_t kGameProjectRenderFrameVersion = 1u;
    inline constexpr uint32_t kGameProjectRenderSurfaceDescV1Size = 96u;
    inline constexpr uint32_t kGameProjectRenderCameraFrameV1Size = 496u;
    inline constexpr uint32_t kGameProjectRenderFrameViewV1Size = 120u;
    inline constexpr uint32_t kGameProjectRenderObserverDescV1Size = 32u;
    inline constexpr uint32_t kGameProjectRenderMaximumSurfaceCount = 32u;

    enum class GameProjectRenderSurfaceSemantic : uint16_t {
        None = 0u,
        SceneDepth = 1u,
        SceneColorOpaque = 2u,
        SceneColorPostOpaque = 3u,
        SceneColorFinal = 4u,
        PackedSurface = 5u,
        SurfaceFeatureMask = 6u,
        MotionVectors = 7u,
        MotionMetadata = 8u,
        CurrentDepthPyramid = 9u,
        VisibilityDepthPyramid = 10u,
        HistoryColor = 11u,
        HistoryDepth = 12u,
        StylizedOutlineStyle = 13u,
        AmbientOcclusion = 14u,
        ReactiveMask = 15u,
        TransparencyMask = 16u,
        InvalidDepthMotionMask = 17u,
        ProjectDispatchOutput = 18u,
        Count = 19u,
    };

    enum class GameProjectRenderSurfacePhase : uint8_t {
        None = 0u,
        Imported = 1u,
        PreOpaque = 2u,
        Opaque = 3u,
        PostOpaque = 4u,
        Temporal = 5u,
        Final = 6u,
        HistoryRead = 7u,
        HistoryWrite = 8u,
        Count = 9u,
    };

    enum class GameProjectRenderSurfaceKind : uint8_t {
        Texture2D = 0u,
        DepthPyramid = 1u,
        Count = 2u,
    };

    enum class GameProjectRenderSurfaceFormat : uint16_t {
        Unknown = 0u,
        R8Unorm = 1u,
        R8G8B8A8Unorm = 2u,
        R16G16Float = 3u,
        R16G16B16A16Float = 4u,
        R32Float = 5u,
        R32G32Uint = 6u,
        Count = 7u,
    };

    enum class GameProjectRenderSurfaceAvailability : uint8_t {
        Unavailable = 0u,
        Imported = 1u,
        Produced = 2u,
        Alias = 3u,
        Count = 4u,
    };

    enum class GameProjectRenderSurfaceLifetime : uint8_t {
        FrameLocal = 0u,
        History = 1u,
        FrameOrHistory = 2u,
        Count = 3u,
    };

    enum class GameProjectRenderSurfaceFallback : uint8_t {
        RequiredFail = 0u,
        SkipProvider = 1u,
        NeutralBlack = 2u,
        NeutralWhite = 3u,
        NoHistory = 4u,
        Count = 5u,
    };

    enum class GameProjectRenderSurfaceAccess : uint32_t {
        None = 0u,
        Sampled = 1u << 0u,
        ReadDepth = 1u << 1u,
        ReadUav = 1u << 4u,
        CopySource = 1u << 6u,
    };

    enum class GameProjectRenderObserverStage : uint32_t {
        PostOpaque = 0u,
        Count = 1u,
    };

    enum class GameProjectRenderViewPurpose : uint32_t {
        Game = 0u,
        EditorScene = 1u,
        ToolPreview = 2u,
        ReflectionProbeCapture = 3u,
        LightProbeCapture = 4u,
        Count = 5u,
    };

    enum class GameProjectRenderExposureMode : uint32_t {
        Manual = 0u,
        Automatic = 1u,
        Count = 2u,
    };

    enum class GameProjectRenderCapability : uint32_t {
        None = 0u,
        SurfaceMetadata = 1u << 0u,
        Camera = 1u << 1u,
        Exposure = 1u << 2u,
        MultiView = 1u << 3u,
    };

    enum class GameProjectRenderFrameFlags : uint32_t {
        None = 0u,
        CameraCut = 1u << 0u,
        ProjectionChanged = 1u << 1u,
        PreviousCameraValid = 1u << 2u,
        Jittered = 1u << 3u,
        CameraValid = 1u << 4u,
        SurfaceListTruncated = 1u << 5u,
    };

    struct GameProjectRenderFrameToken {
        uint64_t frameIndex = 0u;
        uint64_t viewId = 0u;
        uint64_t generation = 0u;

        bool IsValid() const noexcept {
            return viewId != 0u && generation != 0u;
        }
        bool Matches(GameProjectRenderFrameToken expected) const noexcept {
            return IsValid() && expected.IsValid() &&
                frameIndex == expected.frameIndex &&
                viewId == expected.viewId &&
                generation == expected.generation;
        }
    };

    // Opaque frame-local identity. It is useful only inside the observer or a
    // future project pass receiving the matching frame token.
    struct GameProjectRenderSurfaceHandle {
        GameProjectRenderFrameToken frame{};
        uint64_t publicationGeneration = 0u;
        uint64_t storageGeneration = 0u;
        GameProjectRenderSurfaceSemantic semantic =
            GameProjectRenderSurfaceSemantic::None;
        GameProjectRenderSurfacePhase phase =
            GameProjectRenderSurfacePhase::None;
        uint8_t reserved0 = 0u;
        uint32_t reserved1 = 0u;

        bool IsValid() const noexcept {
            return frame.IsValid() &&
                publicationGeneration != 0u &&
                storageGeneration != 0u &&
                semantic > GameProjectRenderSurfaceSemantic::None &&
                semantic < GameProjectRenderSurfaceSemantic::Count &&
                phase > GameProjectRenderSurfacePhase::None &&
                phase < GameProjectRenderSurfacePhase::Count;
        }
    };

    struct GameProjectRenderSurfaceDesc {
        uint32_t structSize = kGameProjectRenderSurfaceDescV1Size;
        uint32_t version = kGameProjectRenderFrameVersion;
        GameProjectRenderSurfaceHandle handle{};
        GameProjectRenderSurfaceKind kind =
            GameProjectRenderSurfaceKind::Texture2D;
        GameProjectRenderSurfaceAvailability availability =
            GameProjectRenderSurfaceAvailability::Unavailable;
        GameProjectRenderSurfaceFormat format =
            GameProjectRenderSurfaceFormat::Unknown;
        GameProjectRenderSurfaceLifetime lifetime =
            GameProjectRenderSurfaceLifetime::FrameLocal;
        GameProjectRenderSurfaceFallback fallback =
            GameProjectRenderSurfaceFallback::SkipProvider;
        uint8_t sampleable = 0u;
        uint8_t reserved0 = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t mipCount = 0u;
        uint32_t readAccessMask = 0u;
        uint32_t reserved[4] = { 0u, 0u, 0u, 0u };
    };

    // Matrices use the engine's column-major m[col][row] convention.
    struct GameProjectRenderCameraFrame {
        uint32_t structSize = kGameProjectRenderCameraFrameV1Size;
        uint32_t version = kGameProjectRenderFrameVersion;
        float view[16]{};
        float projection[16]{};
        float viewProjection[16]{};
        float inverseViewProjection[16]{};
        float unjitteredViewProjection[16]{};
        float previousViewProjection[16]{};
        float position[3]{};
        float previousPosition[3]{};
        float jitterPixels[2]{};
        float previousJitterPixels[2]{};
        float nearZ = 0.0f;
        float farZ = 0.0f;
        float fovYRadians = 0.0f;
        float aspect = 1.0f;
        uint32_t renderWidth = 0u;
        uint32_t renderHeight = 0u;
        uint32_t outputWidth = 0u;
        uint32_t outputHeight = 0u;
        uint32_t flags = 0u;
        uint32_t reserved0 = 0u;
        uint64_t sourceCameraObjectId = 0u;
        uint64_t cameraRevision = 0u;
        uint32_t reserved[2] = { 0u, 0u };
    };

    // The frame, surface array, camera and all pointers are borrowed only for
    // the observer callback. Surface handles expire with token generation.
    struct GameProjectRenderFrameView {
        uint32_t structSize = kGameProjectRenderFrameViewV1Size;
        uint32_t version = kGameProjectRenderFrameVersion;
        GameProjectRenderFrameToken frame{};
        GameProjectRenderObserverStage stage =
            GameProjectRenderObserverStage::PostOpaque;
        GameProjectRenderViewPurpose purpose =
            GameProjectRenderViewPurpose::Game;
        uint32_t flags = 0u;
        uint32_t supportedCapabilityMask = 0u;
        uint32_t availableCapabilityMask = 0u;
        uint32_t validCapabilityMask = 0u;
        uint32_t surfaceCount = 0u;
        uint32_t reserved0 = 0u;
        const GameProjectRenderSurfaceDesc* surfaces = nullptr;
        const GameProjectRenderCameraFrame* camera = nullptr;
        float exposureMultiplier = 1.0f;
        float preExposure = 1.0f;
        GameProjectRenderExposureMode exposureMode =
            GameProjectRenderExposureMode::Manual;
        uint32_t exposureValid = 0u;
        uint64_t viewGeneration = 0u;
        uint64_t worldIdentity = 0u;
        uint32_t reserved[2] = { 0u, 0u };

        const GameProjectRenderSurfaceDesc* FindSurface(
            GameProjectRenderSurfaceSemantic semantic,
            GameProjectRenderSurfacePhase phase) const noexcept {
            if (surfaces == nullptr) {
                return nullptr;
            }
            for (uint32_t index = 0u; index < surfaceCount; ++index) {
                if (surfaces[index].handle.semantic == semantic &&
                    surfaces[index].handle.phase == phase) {
                    return &surfaces[index];
                }
            }
            return nullptr;
        }
    };

    struct GameProjectRenderObserverDesc {
        uint32_t structSize = kGameProjectRenderObserverDescV1Size;
        uint32_t version = kGameProjectRenderFrameVersion;
        GameProjectRenderObserverStage stage =
            GameProjectRenderObserverStage::PostOpaque;
        uint32_t flags = 0u;
        uint32_t reserved[4] = { 0u, 0u, 0u, 0u };
    };

    using ObserveGameProjectRenderFrameFn = bool (*)(
        const IGameSystem* system,
        const GameProjectRenderFrameView* frame) noexcept;

    static_assert(std::is_standard_layout_v<GameProjectRenderFrameToken>);
    static_assert(std::is_trivially_copyable_v<GameProjectRenderFrameToken>);
    static_assert(sizeof(GameProjectRenderFrameToken) == 24u);
    static_assert(std::is_standard_layout_v<GameProjectRenderSurfaceHandle>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectRenderSurfaceHandle>);
    static_assert(sizeof(GameProjectRenderSurfaceHandle) == 48u);
    static_assert(std::is_standard_layout_v<GameProjectRenderSurfaceDesc>);
    static_assert(std::is_trivially_copyable_v<GameProjectRenderSurfaceDesc>);
    static_assert(sizeof(GameProjectRenderSurfaceDesc) == 96u);
    static_assert(offsetof(GameProjectRenderSurfaceDesc, handle) == 8u);
    static_assert(offsetof(GameProjectRenderSurfaceDesc, width) == 64u);
    static_assert(std::is_standard_layout_v<GameProjectRenderCameraFrame>);
    static_assert(std::is_trivially_copyable_v<GameProjectRenderCameraFrame>);
    static_assert(sizeof(GameProjectRenderCameraFrame) == 496u);
    static_assert(offsetof(GameProjectRenderCameraFrame, view) == 8u);
    static_assert(offsetof(GameProjectRenderCameraFrame, flags) == 464u);
    static_assert(offsetof(
        GameProjectRenderCameraFrame, sourceCameraObjectId) == 472u);
    static_assert(offsetof(
        GameProjectRenderCameraFrame, cameraRevision) == 480u);
    static_assert(std::is_standard_layout_v<GameProjectRenderFrameView>);
    static_assert(std::is_trivially_copyable_v<GameProjectRenderFrameView>);
    static_assert(sizeof(GameProjectRenderFrameView) == 120u);
    static_assert(offsetof(GameProjectRenderFrameView, stage) == 32u);
    static_assert(offsetof(GameProjectRenderFrameView, purpose) == 36u);
    static_assert(offsetof(
        GameProjectRenderFrameView, supportedCapabilityMask) == 44u);
    static_assert(offsetof(
        GameProjectRenderFrameView, validCapabilityMask) == 52u);
    static_assert(offsetof(GameProjectRenderFrameView, surfaces) == 64u);
    static_assert(offsetof(
        GameProjectRenderFrameView, viewGeneration) == 96u);
    static_assert(offsetof(
        GameProjectRenderFrameView, worldIdentity) == 104u);
    static_assert(std::is_standard_layout_v<GameProjectRenderObserverDesc>);
    static_assert(std::is_trivially_copyable_v<GameProjectRenderObserverDesc>);
    static_assert(sizeof(GameProjectRenderObserverDesc) == 32u);
    static_assert(std::is_same_v<std::underlying_type_t<
        GameProjectRenderSurfaceSemantic>, uint16_t>);
    static_assert(std::is_same_v<std::underlying_type_t<
        GameProjectRenderObserverStage>, uint32_t>);

} // namespace HIKARI
