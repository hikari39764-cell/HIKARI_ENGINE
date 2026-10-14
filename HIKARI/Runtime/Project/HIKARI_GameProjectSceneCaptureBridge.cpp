#include "HIKARI/Project/SceneCaptureView.h"
#include <cmath>
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

template <size_t Count>
bool HasNonZeroSceneCaptureReserved(const uint32_t (&values)[Count]) noexcept {
    for (uint32_t value : values) {
        if (value != 0u) {
            return true;
        }
    }
    return false;
}

bool ValidateSceneCaptureDesc(const GameProjectSceneCaptureDesc& desc) noexcept {
    constexpr uint32_t knownFlags = GameProjectSceneCaptureFlags(GameProjectSceneCaptureFlag::Enabled) |
                                    GameProjectSceneCaptureFlags(GameProjectSceneCaptureFlag::RenderSky) |
                                    GameProjectSceneCaptureFlags(GameProjectSceneCaptureFlag::RenderTransparent) |
                                    GameProjectSceneCaptureFlags(GameProjectSceneCaptureFlag::RenderVfx);
    return desc.structSize == kGameProjectSceneCaptureDescV1Size && desc.version == kGameProjectSceneCaptureVersion &&
           !HasNonZeroSceneCaptureReserved(desc.reserved) && desc.cameraObject.IsValid() &&
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
    constexpr TEXT::EnumName<GameProjectSceneCaptureStyle> styles[] = {{GameProjectSceneCaptureStyle::None, "None"},
                                                                       {GameProjectSceneCaptureStyle::Clean, "Clean"},
                                                                       {GameProjectSceneCaptureStyle::Pixel, "Pixel"},
                                                                       {GameProjectSceneCaptureStyle::Sketch, "Sketch"}};
    const auto inRange = [](float value, float low, float high) {
        return MATH::IsFinite(value) && value >= low && value <= high;
    };
    const auto validColor = [&](const GameProjectFloat3& color) {
        return inRange(color.x, 0.0f, 1.0f) && inRange(color.y, 0.0f, 1.0f) && inRange(color.z, 0.0f, 1.0f);
    };
    if (style.structSize != sizeof(GameProjectSceneCaptureStyleDesc) || style.version != 2u ||
        HasNonZeroSceneCaptureReserved(style.reserved) || !TEXT::IsNamedEnum(styles, style.style) ||
        !inRange(style.pixelSize, 1.0f, 32.0f) || !inRange(style.lineWidth, 0.0f, 4.0f) ||
        !inRange(style.shadowStrength, 0.0f, 1.0f) || !inRange(style.shadowOffsetX, -16.0f, 16.0f) ||
        !inRange(style.shadowOffsetY, -16.0f, 16.0f) || !inRange(style.backgroundLuminance, 0.0f, 1.0f) ||
        !inRange(style.brightness, 0.0f, 4.0f) || !validColor(style.lineColor) || !validColor(style.paperColor) ||
        !validColor(style.accentColor)) {
        return false;
    }
    try {
        World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
        auto* service = world != nullptr ? world->Services().Find<SCENE_CAPTURE::SceneCaptureService>() : nullptr;
        return service != nullptr && service->SetStyle(capture, style);
    } catch (...) {
        return false;
    }
}

bool SceneCaptureView::BindMaterial(const GameProjectSceneCaptureMaterialBindingDesc& binding) const noexcept {
    if (binding.structSize != kGameProjectSceneCaptureMaterialBindingDescV1Size ||
        binding.version != kGameProjectSceneCaptureVersion || HasNonZeroSceneCaptureReserved(binding.reserved)) {
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
            !binding.modelObject.IsValid() || HasNonZeroSceneCaptureReserved(binding.reserved) ||
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
