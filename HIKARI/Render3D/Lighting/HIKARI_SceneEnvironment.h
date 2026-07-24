#pragma once

#include <string>
#include <vector>
#include <DirectXMath.h>
#include "Render3D/Debug/HIKARI_RenderDebugView.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    struct AmbientLight {
        MATH::Vec3 color{ 1.0f, 1.0f, 1.0f };
        float intensity = 0.25f;
        bool useSkyColor = false;
        float skyBlend = 1.0f;
    };

    struct DirectionalLight {
        bool enabled = true;
        MATH::Vec3 direction{ 0.4f, -1.0f, -0.6f };
        float intensity = 1.0f;
        MATH::Vec3 color{ 1.0f, 1.0f, 1.0f };
    };

    struct DirectionalShadowSettings {
        bool enabled = true;
        uint32_t resolution = 2048;
        float orthoSize = 30.0f;
        float nearPlane = 0.1f;
        float farPlane = 80.0f;
        float depthBias = 0.001f;
        float normalBias = 0.02f;
        float strength = 0.75f;
        bool pcfEnabled = true;
        float pcfRadius = 1.0f;
        bool stabilize = true;
        bool showDebugTexture = false;
        float shadowDistance = 30.0f;
        bool showDebugFrustum = false;
    };

    struct PointLight {
        bool enabled = true;
        MATH::Vec3 position{ 0.0f, 2.0f, 0.0f };
        float range = 5.0f;
        MATH::Vec3 color{ 1.0f, 1.0f, 1.0f };
        float intensity = 1.0f;
    };

    enum class SkyMode {
        None = 0,
        Gradient,
        Cubemap,
        Texture2D,
    };

    struct SkySettings {
        bool enabled = true;
        SkyMode mode = SkyMode::Gradient;
        std::string skyAsset = "DefaultSky";
        float scale = 1.0f;
        float yaw = 0.0f;
        float exposure = 1.0f;
        MATH::Vec3 tint{ 1.0f, 1.0f, 1.0f };
        bool followCamera = true;
        MATH::Vec3 zenithColor{ 0.08f, 0.22f, 0.55f };
        MATH::Vec3 horizonColor{ 0.65f, 0.78f, 0.95f };
        MATH::Vec3 groundColor{ 0.08f, 0.08f, 0.10f };
        float horizonPower = 1.5f;
        bool showSunDisk = true;
        float sunDiskIntensity = 0.0f;
        float sunDiskSize = 0.04f;
        float ambientFromSky = 1.0f;
        float reflectionIntensity = 1.0f;
        bool showDebugTexture = false;
    };

    enum class ReflectionProbeInfluenceShape {
        Sphere,
        Box,
    };

    enum class ReflectionProbeProjectionShape {
        Infinite,
        Box,
    };

    struct ReflectionProbeSettings {
        bool enabled = false;
        std::string sourceCubemapAsset{};
        MATH::Vec3 position{ 0.0f, 2.0f, 0.0f };
        float radius = 8.0f;
        float intensity = 1.0f;
        // Influence は probe の有効範囲、Projection は cubemap の視差補正に使う。
        ReflectionProbeInfluenceShape influenceShape = ReflectionProbeInfluenceShape::Sphere;
        MATH::Vec3 influenceBoxCenter{ 0.0f, 2.0f, 0.0f };
        MATH::Vec3 influenceBoxSize{ 8.0f, 4.0f, 8.0f };
        ReflectionProbeProjectionShape projectionShape = ReflectionProbeProjectionShape::Infinite;
        MATH::Vec3 projectionBoxCenter{ 0.0f, 2.0f, 0.0f };
        MATH::Vec3 projectionBoxSize{ 8.0f, 4.0f, 8.0f };
        float blendDistance = 1.0f;
        int priority = 0;
    };

    enum class SsaoMode {
        Off = 0,
        Reference,
        OptimizedHigh,
        Balanced,
    };

    struct AmbientOcclusionSettings {
        bool enabled = false;
        SsaoMode mode = SsaoMode::Balanced;
        float radius = 0.6f;
        float bias = 0.025f;
        float strength = 1.0f;
        float power = 1.5f;
        float diffuseStrength = 1.0f;
        float specularStrength = 0.5f;
        uint32_t sampleCount = 16;
        uint32_t blurIterations = 1;
        bool editorViewportSuppressed = false;
    };

    struct BloomSettings {
        bool enabled = true;
        float threshold = 1.0f;
        float intensity = 0.6f;
        float radius = 1.0f;
        uint32_t downsampleCount = 3;
    };

    struct FogSettings {
        bool enabled = false;
        bool volumetric = true;
        MATH::Vec3 color{ 0.55f, 0.65f, 0.75f };
        float density = 0.02f;
        float startDistance = 10.0f;
        float endDistance = 80.0f;
        float heightFalloff = 0.0f;
        float anisotropy = 0.2f;
        float temporalWeight = 0.9f;
        bool useSkyHorizonColor = false;
    };

    struct ToneMappingSettings {
        bool enabled = true;
        float exposure = 1.0f;
        float gamma = 2.2f;
        int mode = 1; // 0=None, 1=Reinhard, 2=ACES approximate
    };

    struct ScenePostSettings {
        bool enabled = false;
        std::string globalPostProfileId{};
        DirectX::XMFLOAT4 paramValues[16]{};
        bool valuesInitialized = false;
    };

    struct SceneEnvironment {
        AmbientLight ambient{};
        DirectionalLight directional{};
        DirectionalShadowSettings directionalShadow{};
        std::vector<PointLight> pointLights{};
        SkySettings sky{};
        ReflectionProbeSettings reflectionProbe{};
        AmbientOcclusionSettings ambientOcclusion{};
        BloomSettings bloom{};
        FogSettings fog{};
        ToneMappingSettings toneMapping{};
        float specularIntensity = 0.2f;
        float specularPower = 32.0f;
        bool showLightDebug = true;
        bool showPointLightMarkers = true;
        bool showSkyDebugInfo = false;
        ScenePostSettings post{};
    };

} // namespace HIKARI
