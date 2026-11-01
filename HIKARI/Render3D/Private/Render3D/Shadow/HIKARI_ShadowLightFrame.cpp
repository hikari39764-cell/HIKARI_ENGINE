#include "Render3D/Shadow/HIKARI_ShadowLightFrame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

namespace HIKARI::SHADOW {

namespace {
float SnapShadowAnchorValue(float value, float grid) {
    if (grid <= 0.0001f) {
        return value;
    }
    return std::round(value / grid) * grid;
}

float ResolveShadowAnchorGrid(float orthoSize, uint32_t resolution) {
    if (!RENDER3D::GetRenderQualitySettings().directionalShadow.stabilize || resolution == 0u) {
        return 0.0f;
    }

    constexpr float kSnapTexels = 1.0f;
    const float texelWorldSize = orthoSize / static_cast<float>((std::max)(1u, resolution));
    return (std::max)(texelWorldSize * kSnapTexels, 0.0001f);
}

// 影を落とす主光源（昼は太陽、夜は月）の光線の向き。
MATH::Vec3 ResolveLightDirection(const SceneEnvironment& environment) {
    MATH::Vec3 lightDir = MATH::Normalize(environment.frame.Primary().toLight * -1.0f);
    if (MATH::Length(lightDir) <= 1e-6f) {
        lightDir = MATH::Normalize(MATH::Vec3{0.4f, -1.0f, -0.6f});
    }
    return lightDir;
}

MATH::Vec3 SafeCameraForward(const Camera3D& camera) {
    MATH::Vec3 forward = MATH::Normalize(camera.GetTarget() - camera.GetPosition());
    if (MATH::Length(forward) <= 1e-6f) {
        forward = {0.0f, 0.0f, 1.0f};
    }
    return forward;
}

ShadowLightFrame BuildCascadeFrame(const SceneEnvironment& environment, const Camera3D& camera, uint32_t resolution,
                                   float splitNear, float splitFar) {
    const MATH::Vec3 cameraForward = SafeCameraForward(camera);
    // Frustum の外接半径はカメラ空間で求め、ワールド座標の丸めでスケールを変えない。
    const float tanHalfFov = std::tan(camera.GetFovYRad() * 0.5f);
    const float halfDepth = (splitFar - splitNear) * 0.5f;
    const float farHalfHeight = camera.IsOrthographic() ? camera.GetOrthographicHeight() * 0.5f : splitFar * tanHalfFov;
    const float farHalfWidth = farHalfHeight * camera.GetAspect();
    const MATH::Vec3 center = camera.GetPosition() + cameraForward * ((splitNear + splitFar) * 0.5f);
    float radius = (std::max)(
        1.0f, std::sqrt(halfDepth * halfDepth + farHalfHeight * farHalfHeight + farHalfWidth * farHalfWidth));
    radius = std::ceil(radius * 16.0f) / 16.0f;

    const MATH::Vec3 lightDir = ResolveLightDirection(environment);
    MATH::Vec3 up{0.0f, 1.0f, 0.0f};
    if (std::abs(MATH::Dot(lightDir, up)) > 0.95f) {
        up = {1.0f, 0.0f, 0.0f};
    }
    const auto basis = MATH::BuildViewBasisRH(lightDir, up);
    const MATH::Vec3 right = basis.right;
    const MATH::Vec3 actualUp = basis.up;

    const float baseOrthoSize = radius * 2.0f;
    // 5x5 PCF の最大 2*radius、線形比較の半 Texel、Snap の半 Texel を確保する。
    const float guardTexels = (std::max)(
        2.0f,
        std::ceil(2.0f * std::clamp(RENDER3D::GetRenderQualitySettings().directionalShadow.pcfRadius, 0.0f, 4.0f)) +
            1.0f);
    const float usableFraction = 1.0f - 2.0f * guardTexels / static_cast<float>((std::max)(32u, resolution));
    const float orthoSize = baseOrthoSize / usableFraction;
    const float anchorGrid = ResolveShadowAnchorGrid(orthoSize, resolution);
    MATH::Vec3 anchor = center;
    if (anchorGrid > 0.0f) {
        anchor = right * SnapShadowAnchorValue(MATH::Dot(center, right), anchorGrid) +
                 actualUp * SnapShadowAnchorValue(MATH::Dot(center, actualUp), anchorGrid) +
                 lightDir * SnapShadowAnchorValue(MATH::Dot(center, lightDir), anchorGrid);
    }

    const float depthPadding =
        (std::max)(radius, RENDER3D::GetRenderQualitySettings().directionalShadow.farPlane * 0.25f);
    const float lightDistance = radius + depthPadding;
    const float nearPlane = (std::max)(0.01f, RENDER3D::GetRenderQualitySettings().directionalShadow.nearPlane);
    const float farPlane = (std::max)(nearPlane + 1.0f, lightDistance * 2.0f + radius);
    const MATH::Vec3 lightPos = anchor - lightDir * lightDistance;

    ShadowLightFrame frame{};
    // 原点で固定基底を作り、遠方の eye/target の差分による回転誤差を避ける。
    frame.view = MATH::Mat4::LookAtRH({}, lightDir, actualUp) * MATH::Mat4::Translate(lightPos * -1.0f);
    frame.viewProj = MATH::Mat4::OrthoRH_ZO(orthoSize, orthoSize, nearPlane, farPlane) * frame.view;
    frame.anchor = anchor;
    frame.lightPosition = lightPos;
    frame.lightDirection = lightDir;
    frame.right = right;
    frame.up = actualUp;
    frame.anchorGrid = anchorGrid;
    frame.splitNear = splitNear;
    frame.splitFar = splitFar;
    frame.orthoSize = orthoSize;
    frame.lightNear = nearPlane;
    frame.lightFar = farPlane;
    return frame;
}
} // namespace

float GetShadowCasterMinimumDepth(const ShadowLightFrame& frame, const MATH::Vec4& bounds) noexcept {
    if (!MATH::IsFinite(bounds) || bounds.w < 0.0f)
        return frame.lightNear;
    const MATH::Vec3 relative = MATH::Vec3{bounds.x, bounds.y, bounds.z} - frame.anchor;
    const float half = frame.orthoSize * 0.5f + bounds.w;
    if (std::abs(MATH::Dot(relative, frame.right)) > half || std::abs(MATH::Dot(relative, frame.up)) > half)
        return frame.lightNear;
    const auto view = frame.view.TransformPoint({bounds.x, bounds.y, bounds.z, 1.0f});
    const float minimumDepth = -view.z - bounds.w;
    return std::isfinite(minimumDepth) ? (std::min)(frame.lightNear, minimumDepth) : frame.lightNear;
}

void ExtendShadowCasterDepth(ShadowLightFrame& frame, float minimumDepth) noexcept {
    if (!std::isfinite(minimumDepth) || minimumDepth >= frame.lightNear)
        return;
    const float quantum = (std::max)(1.0f, frame.anchorGrid);
    const float shift = std::ceil((frame.lightNear - minimumDepth) / quantum) * quantum;
    const float expandedFar = frame.lightFar + shift;
    if (!std::isfinite(expandedFar) || expandedFar <= frame.lightNear)
        return;
    frame.depthBiasScale *= (frame.lightFar - frame.lightNear) / (expandedFar - frame.lightNear);
    frame.lightFar = expandedFar;
    frame.lightPosition = frame.lightPosition - frame.lightDirection * shift;
    // Z 行だけを動かし、XY の Texel 固定を浮動小数の再計算から保護する。
    frame.view.m[3][2] -= shift;
    frame.viewProj =
        MATH::Mat4::OrthoRH_ZO(frame.orthoSize, frame.orthoSize, frame.lightNear, frame.lightFar) * frame.view;
}

LocalLightShadowFrameSet BuildLocalLightShadowFrameSet(const RENDER3D::LIGHTING::SceneLightProvider& light,
                                                       uint32_t faceResolution) noexcept {
    using namespace RENDER3D::LIGHTING;
    LocalLightShadowFrameSet result{};
    if (!IsLocalLightUploadable(light) || faceResolution < 32u)
        return result;
    // 管と長方形も中心から点光源と同じ六面で影を取る。
    const bool point = light.type != SceneLightType::Spot;
    const float nearPlane = (std::max)(1e-4f, (std::min)(.05f, light.range * .001f));
    if (light.range <= nearPlane)
        return result;
    const float halfAngle = point ? std::numbers::pi_v<float> * .25f
                                  : ResolveSpotLightCone(light).outerAngleDegrees * std::numbers::pi_v<float> / 180.f;
    // 二 Texel の PCF、線形比較と狭い cone の行列丸め用に四 Texel を確保。
    const float usable = 1.f - 8.f / float(faceResolution);
    const float fov = 2.f * std::atan(std::tan(halfAngle) / usable);
    const auto projection = MATH::Mat4::PerspectiveFovRH_ZO(fov, 1.f, nearPlane, light.range);
    constexpr std::array<MATH::Vec3, 6> directions{
        {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    constexpr std::array<MATH::Vec3, 6> upVectors{{{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}}};
    result.faceCount = point ? 6u : 1u;
    for (uint32_t index = 0; index < result.faceCount; ++index) {
        auto& frame = result.faces[index];
        frame.lightPosition = light.position;
        frame.lightDirection = point ? directions[index] : ResolveSpotLightDirection(light);
        auto up = point ? upVectors[index] : MATH::Vec3{0, 1, 0};
        if (std::abs(MATH::Dot(up, frame.lightDirection)) > .95f)
            up = {1, 0, 0};
        // 固定原点の基底を使い、大きいワールド座標で方向を再計算しない。
        frame.view = MATH::Mat4::LookAtRH({}, frame.lightDirection, up) * MATH::Mat4::Translate(light.position * -1.f);
        frame.viewProj = projection * frame.view;
        if (!MATH::IsFinite(frame.viewProj))
            return {};
        frame.lightNear = nearPlane;
        frame.lightFar = light.range;
        const auto basis = MATH::BuildViewBasisRH(frame.lightDirection, up);
        frame.right = basis.right;
        frame.up = basis.up;
    }
    return result;
}

uint32_t ResolveShadowResolution(uint32_t resolution) {
    if (resolution <= 1024) {
        return 1024;
    }
    if (resolution <= 2048) {
        return 2048;
    }
    return 4096;
}

DirectionalShadowFrameSet BuildDirectionalShadowFrameSet(const SceneEnvironment& environment, const Camera3D& camera,
                                                         uint32_t cascadeResolution) {
    DirectionalShadowFrameSet result{};
    const RENDER3D::DirectionalShadowQualitySettings& quality = RENDER3D::GetRenderQualitySettings().directionalShadow;
    result.cascadeCount = std::clamp(quality.cascadeCount, 1u, kMaxDirectionalShadowCascades);
    const float nearDistance = (std::max)(0.01f, camera.GetNearZ());
    const float farDistance =
        (std::max)(nearDistance + 0.01f, (std::min)(camera.GetFarZ(), environment.shadows.distance));
    const float lambda = std::clamp(quality.cascadeSplitLambda, 0.0f, 1.0f);

    const float blend = std::clamp(quality.cascadeBlend, 0.0f, 0.3f);
    std::array<float, kMaxDirectionalShadowCascades> splitDistances{};
    float splitNear = nearDistance;
    float fitNear = nearDistance;
    for (uint32_t cascadeIndex = 0; cascadeIndex < result.cascadeCount; ++cascadeIndex) {
        const float ratio = static_cast<float>(cascadeIndex + 1u) / static_cast<float>(result.cascadeCount);
        const float logarithmic = nearDistance * std::pow(farDistance / nearDistance, ratio);
        const float uniform = nearDistance + (farDistance - nearDistance) * ratio;
        const float splitFar =
            cascadeIndex + 1u == result.cascadeCount ? farDistance : uniform * (1.0f - lambda) + logarithmic * lambda;
        result.cascades[cascadeIndex] = BuildCascadeFrame(environment, camera, cascadeResolution, fitNear, splitFar);
        // Shader は前の区間の末尾から次 Cascade を混ぜるため、その帯域も次の Frustum に含める。
        fitNear = splitFar - (splitFar - (cascadeIndex == 0u ? 0.0f : splitNear)) * blend;
        splitDistances[cascadeIndex] = splitFar;
        splitNear = splitFar;
    }
    for (uint32_t cascadeIndex = result.cascadeCount; cascadeIndex < kMaxDirectionalShadowCascades; ++cascadeIndex) {
        result.cascades[cascadeIndex] = result.cascades[result.cascadeCount - 1u];
        splitDistances[cascadeIndex] = farDistance;
    }
    result.splitDistances = {splitDistances[0], splitDistances[1], splitDistances[2], splitDistances[3]};
    return result;
}

} // namespace HIKARI::SHADOW
