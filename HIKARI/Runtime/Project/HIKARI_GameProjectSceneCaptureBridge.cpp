#include "HIKARI/Project/SceneCaptureView.h"
#include "Core/Numeric/HIKARI_NonZero.h"
#include <cmath>
#include <cstring>
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Scene/Capture/HIKARI_SceneCaptureService.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "HIKARI/Project/WorldView.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectServiceBridgeInternal.h"

namespace HIKARI {
namespace {
using PROJECT::INTERNAL::ReadFixedUtf8;

bool ValidateSceneCaptureDesc(const GameProjectSceneCaptureDesc& desc) noexcept {
    constexpr uint32_t knownFlags = GameProjectSceneCaptureFlags(GameProjectSceneCaptureFlag::Enabled) |
                                    GameProjectSceneCaptureFlags(GameProjectSceneCaptureFlag::RenderSky) |
                                    GameProjectSceneCaptureFlags(GameProjectSceneCaptureFlag::RenderTransparent) |
                                    GameProjectSceneCaptureFlags(GameProjectSceneCaptureFlag::RenderVfx);
    return desc.structSize == kGameProjectSceneCaptureDescV1Size && desc.version == kGameProjectSceneCaptureVersion &&
           !NUMERIC::AnyNonZero(desc.reserved) && desc.cameraObject.IsValid() &&
           desc.width >= kGameProjectSceneCaptureMinimumExtent && desc.width <= kGameProjectSceneCaptureMaximumExtent &&
           desc.height >= kGameProjectSceneCaptureMinimumExtent &&
           desc.height <= kGameProjectSceneCaptureMaximumExtent && (desc.flags & ~knownFlags) == 0u;
}

bool ValidateSceneCaptureSourceRect(const GameProjectSceneCaptureSourceRect& rect) noexcept {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
        rect.x < 0.0f || rect.y < 0.0f || rect.width <= 0.0f || rect.height <= 0.0f) {
        return false;
    }
    constexpr float boundaryTolerance = 0.00001f;
    return rect.x + rect.width <= 1.0f + boundaryTolerance && rect.y + rect.height <= 1.0f + boundaryTolerance;
}

RuntimeObjectHandle ResolveSceneCaptureObject(World& world, GameProjectObjectHandle handle) noexcept {
    return PROJECT::INTERNAL::IsObjectHandleForWorld(handle, world) ? PROJECT::INTERNAL::DecodeRuntimeHandle(handle)
                                                                    : RuntimeObjectHandle{};
}

} // namespace

SceneCaptureView GameProjectSdkAccess::MakeSceneCaptureView(World& world) noexcept {
    return SceneCaptureView(GameProjectWorldHandle{world.GetRuntimeIdentity()});
}

SceneCaptureView WorldView::GetSceneCapture() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakeSceneCaptureView(*world) : SceneCaptureView{};
}

bool SceneCaptureView::IsValid() const noexcept { return PROJECT::INTERNAL::ResolveServiceWorld(world_) != nullptr; }

GameProjectSceneCaptureHandle SceneCaptureView::Create(const GameProjectSceneCaptureDesc& desc) const noexcept {
    try {
        World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
        if (world == nullptr || !ValidateSceneCaptureDesc(desc)) {
            return {};
        }
        const RuntimeObjectHandle camera = ResolveSceneCaptureObject(*world, desc.cameraObject);
        GameObject* cameraObject = world->FindObject(camera);
        if (cameraObject == nullptr || cameraObject->GetComponent<CameraComponent>() == nullptr) {
            return {};
        }
        SCENE_CAPTURE::SceneCaptureService* service = world->Services().Find<SCENE_CAPTURE::SceneCaptureService>();
        return service != nullptr ? service->Create(camera, desc) : GameProjectSceneCaptureHandle{};
    } catch (...) {
        return {};
    }
}

bool SceneCaptureView::Update(GameProjectSceneCaptureHandle capture,
                              const GameProjectSceneCaptureDesc& desc) const noexcept {
    try {
        World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
        if (world == nullptr || !capture.IsValid() || !ValidateSceneCaptureDesc(desc)) {
            return false;
        }
        const RuntimeObjectHandle camera = ResolveSceneCaptureObject(*world, desc.cameraObject);
        GameObject* cameraObject = world->FindObject(camera);
        SCENE_CAPTURE::SceneCaptureService* service = world->Services().Find<SCENE_CAPTURE::SceneCaptureService>();
        return service != nullptr && cameraObject != nullptr &&
               cameraObject->GetComponent<CameraComponent>() != nullptr && service->Update(capture, camera, desc);
    } catch (...) {
        return false;
    }
}

bool SceneCaptureView::Destroy(GameProjectSceneCaptureHandle capture) const noexcept {
    try {
        World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
        SCENE_CAPTURE::SceneCaptureService* service =
            world != nullptr ? world->Services().Find<SCENE_CAPTURE::SceneCaptureService>() : nullptr;
        return service != nullptr && capture.IsValid() && service->Destroy(capture);
    } catch (...) {
        return false;
    }
}

bool SceneCaptureView::SetStyle(GameProjectSceneCaptureHandle capture,
                                const GameProjectSceneCaptureStyleDesc& style) const noexcept {
    constexpr TEXT::EnumName<GameProjectSceneCaptureStyle> styles[] = {
        {GameProjectSceneCaptureStyle::None, "None"},
        {GameProjectSceneCaptureStyle::Clean, "Clean"},
        {GameProjectSceneCaptureStyle::Pixel, "Pixel"},
        {GameProjectSceneCaptureStyle::Sketch, "Sketch"}};
    const auto inRange = [](float value, float low, float high) {
        return MATH::IsFinite(value) && value >= low && value <= high;
    };
    const auto validColor = [&](const GameProjectFloat3& color) {
        return inRange(color.x, 0.0f, 1.0f) && inRange(color.y, 0.0f, 1.0f) && inRange(color.z, 0.0f, 1.0f);
    };
    // 版 2・3 は 80 byte（fillStrength まで）で、深度の欄を持たない。読める分だけ写し、深度の効果は既定（無効）にする。
    // 版 2 は塗りの濃さの欄が予約（0 でなければならない）で、既定の濃さを使う。版 3 以降は 0〜1 を受け付ける。
    // 版 4 は 140 byte（depthLineGap まで）で、背景を透かす欄を持たない（既定の 0 で読む）。版 5 は 144 byte で、
    // 色調の欄を持たない（既定の「変えない」で読む）。
    constexpr uint32_t kLegacySize = offsetof(GameProjectSceneCaptureStyleDesc, depthFocus);
    constexpr uint32_t kV4Size = offsetof(GameProjectSceneCaptureStyleDesc, transparentBackground);
    constexpr uint32_t kV5Size = offsetof(GameProjectSceneCaptureStyleDesc, toneTint);
    const bool legacy = style.version == 2u;
    const bool shortForm = style.version == 2u || style.version == 3u;
    const uint32_t expected = shortForm ? kLegacySize
                              : style.version == 4u ? kV4Size
                              : style.version == 5u ? kV5Size
                              : style.version == 6u ? static_cast<uint32_t>(sizeof(GameProjectSceneCaptureStyleDesc))
                                                    : 0u;
    if (expected == 0u || style.structSize != expected) {
        return false;
    }
    GameProjectSceneCaptureStyleDesc normalized{};
    std::memcpy(&normalized, &style, expected);
    if (legacy ? normalized.fillStrength != 0.0f : !inRange(normalized.fillStrength, 0.0f, 1.0f)) {
        return false;
    }
    if (legacy) {
        normalized.fillStrength = 0.32f;
    }
    normalized.structSize = sizeof(GameProjectSceneCaptureStyleDesc);
    normalized.version = 6u;
    const auto& v = normalized;
    if (!TEXT::IsNamedEnum(styles, v.style) ||
        !inRange(v.pixelSize, 1.0f, 32.0f) || !inRange(v.lineWidth, 0.0f, 4.0f) ||
        !inRange(v.shadowStrength, 0.0f, 1.0f) || !inRange(v.shadowOffsetX, -16.0f, 16.0f) ||
        !inRange(v.shadowOffsetY, -16.0f, 16.0f) || !inRange(v.backgroundLuminance, 0.0f, 1.0f) ||
        !inRange(v.brightness, 0.0f, 4.0f) || !validColor(v.lineColor) || !validColor(v.paperColor) ||
        !validColor(v.accentColor) || !inRange(v.depthFocus, 0.0f, 100000.0f) ||
        !inRange(v.backgroundDepth, 0.0f, 100000.0f) || !inRange(v.fogStrength, 0.0f, 1.0f) ||
        !inRange(v.fogDepth, 0.001f, 100000.0f) || !validColor(v.fogColor) || !inRange(v.occlusion, 0.0f, 1.0f) ||
        !inRange(v.occlusionRadius, 0.0f, 16.0f) || !inRange(v.occlusionDepth, 0.001f, 100000.0f) ||
        !inRange(v.depthLine, 0.0f, 1.0f) || !inRange(v.lightShadow, 0.0f, 1.0f) ||
        !inRange(v.lightSlope, 0.0f, 100000.0f) || !inRange(v.lightReach, 0.0f, 32.0f) ||
        !inRange(v.depthLineGap, 0.001f, 100000.0f) || !inRange(v.transparentBackground, 0.0f, 1.0f) ||
        !validColor(v.toneTint) || !inRange(v.toneTintStrength, 0.0f, 1.0f) || !inRange(v.toneHue, -0.5f, 0.5f) ||
        !inRange(v.toneSaturation, 0.0f, 2.0f) || !inRange(v.toneBrightness, 0.0f, 2.0f) || !inRange(v.toneContrast, 0.0f, 2.0f)) {
        return false;
    }
    try {
        World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
        auto* service = world != nullptr ? world->Services().Find<SCENE_CAPTURE::SceneCaptureService>() : nullptr;
        return service != nullptr && service->SetStyle(capture, normalized);
    } catch (...) {
        return false;
    }
}

bool SceneCaptureView::SetViews(GameProjectSceneCaptureHandle capture, const GameProjectSceneCaptureViewDesc* views,
                                uint32_t count) const noexcept {
    if (count > kGameProjectSceneCaptureMaximumViewCount || (count > 0u && views == nullptr)) {
        return false;
    }
    const auto finite3 = [](const GameProjectFloat3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    for (uint32_t index = 0u; index < count; ++index) {
        const GameProjectSceneCaptureViewDesc& view = views[index];
        const GameProjectFloat3& f = view.forward;
        const GameProjectFloat3& u = view.up;
        const float forwardLength = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
        const float upLength = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
        const float cx = f.y * u.z - f.z * u.y, cy = f.z * u.x - f.x * u.z, cz = f.x * u.y - f.y * u.x;
        // 視線と上が平行だと向きが決まらない。
        const bool oriented = forwardLength > 1.0e-6f && upLength > 1.0e-6f &&
                              std::sqrt(cx * cx + cy * cy + cz * cz) > 1.0e-4f * forwardLength * upLength;
        if (view.structSize != kGameProjectSceneCaptureViewDescV1Size ||
            view.version != kGameProjectSceneCaptureViewVersion || (view.flags & ~kGameProjectSceneCaptureKnownViewFlags) != 0u ||
            view.orthographic > 1u ||
            view.width == 0u || view.height == 0u || !finite3(view.eye) || !finite3(f) || !finite3(u) || !oriented ||
            !std::isfinite(view.left) || !std::isfinite(view.right) || !std::isfinite(view.bottom) ||
            !std::isfinite(view.top) || !std::isfinite(view.nearZ) || !std::isfinite(view.farZ) ||
            !(view.left < view.right) || !(view.bottom < view.top) || !(view.nearZ > 0.0f) ||
            !(view.farZ > view.nearZ)) {
            return false;
        }
        // 深度の範囲と投影の種類は全部の視点でそろえる（描き方が像全体を一つの深度の写し方で読む）。
        if (view.nearZ != views[0].nearZ || view.farZ != views[0].farZ || view.orthographic != views[0].orthographic) {
            return false;
        }
    }
    try {
        World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
        auto* service = world != nullptr ? world->Services().Find<SCENE_CAPTURE::SceneCaptureService>() : nullptr;
        return service != nullptr && service->SetViews(capture, views, count);
    } catch (...) {
        return false;
    }
}

bool SceneCaptureView::BindMaterial(const GameProjectSceneCaptureMaterialBindingDesc& binding) const noexcept {
    if (binding.structSize != kGameProjectSceneCaptureMaterialBindingDescV1Size ||
        binding.version != kGameProjectSceneCaptureVersion || NUMERIC::AnyNonZero(binding.reserved)) {
        return false;
    }
    GameProjectSceneCaptureMaterialRegionBindingDesc region{};
    region.capture = binding.capture;
    region.modelObject = binding.modelObject;
    std::copy(std::begin(binding.materialSlotName), std::end(binding.materialSlotName),
              std::begin(region.materialSlotName));
    region.fallbackMaterialIndex = binding.fallbackMaterialIndex;
    region.flags = binding.flags;
    return BindMaterialRegion(region);
}

bool SceneCaptureView::BindMaterialRegion(
    const GameProjectSceneCaptureMaterialRegionBindingDesc& binding) const noexcept {
    try {
        World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
        if (world == nullptr || binding.structSize != kGameProjectSceneCaptureMaterialRegionBindingDescV1Size ||
            binding.version != kGameProjectSceneCaptureMaterialRegionBindingVersion || !binding.capture.IsValid() ||
            !binding.modelObject.IsValid() || NUMERIC::AnyNonZero(binding.reserved) ||
            (binding.flags & ~kGameProjectSceneCaptureMaterialBindingKnownFlags) != 0u || binding.quarterTurns >= 4u ||
            !ValidateSceneCaptureSourceRect(binding.sourceRect)) {
            return false;
        }
        std::string materialSlotName{};
        if (!ReadFixedUtf8(binding.materialSlotName, materialSlotName) ||
            (materialSlotName.empty() && binding.fallbackMaterialIndex == kGameProjectInvalidMaterialIndex)) {
            return false;
        }
        const RuntimeObjectHandle modelHandle = ResolveSceneCaptureObject(*world, binding.modelObject);
        GameObject* modelObject = world->FindObject(modelHandle);
        SCENE_CAPTURE::SceneCaptureService* service = world->Services().Find<SCENE_CAPTURE::SceneCaptureService>();
        return service != nullptr && modelObject != nullptr && modelObject->GetComponent<ModelComponent>() != nullptr &&
               service->BindMaterial({
                   binding.capture,
                   modelHandle,
                   std::move(materialSlotName),
                   binding.fallbackMaterialIndex,
                   binding.flags,
                   binding.sourceRect,
                   binding.quarterTurns,
               });
    } catch (...) {
        return false;
    }
}

bool SceneCaptureView::ClearMaterialBinding(GameProjectSceneCaptureHandle capture,
                                            GameProjectObjectHandle modelObject) const noexcept {
    try {
        World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
        if (world == nullptr || !capture.IsValid()) {
            return false;
        }
        const RuntimeObjectHandle model = ResolveSceneCaptureObject(*world, modelObject);
        SCENE_CAPTURE::SceneCaptureService* service = world->Services().Find<SCENE_CAPTURE::SceneCaptureService>();
        return service != nullptr && model.IsValid() && service->ClearMaterialBinding(capture, model);
    } catch (...) {
        return false;
    }
}

bool SceneCaptureView::GetSnapshot(GameProjectSceneCaptureHandle capture,
                                   GameProjectSceneCaptureSnapshot& outSnapshot) const noexcept {
    outSnapshot = {};
    try {
        World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
        SCENE_CAPTURE::SceneCaptureService* service =
            world != nullptr ? world->Services().Find<SCENE_CAPTURE::SceneCaptureService>() : nullptr;
        return service != nullptr && capture.IsValid() && service->TryGetSnapshot(capture, outSnapshot);
    } catch (...) {
        outSnapshot = {};
        return false;
    }
}

} // namespace HIKARI
