#pragma once

namespace HIKARI {

class AssetRegistry;
class Camera3D;
class ModelManager;

struct RuntimePlayStateService {
    const bool* active = nullptr;

    bool IsActive() const noexcept { return active != nullptr && *active; }
};

struct GameplayCameraService {
    Camera3D* camera = nullptr;
    const bool* runtimeSceneCameraActive = nullptr;

    bool IsRuntimeCameraActive() const noexcept {
        return runtimeSceneCameraActive != nullptr && *runtimeSceneCameraActive;
    }
};

// World-scoped borrowed asset context for narrow project authoring
// operations. ModelManager remains the sole loader/cache owner.
struct RuntimeAssetContextService {
    AssetRegistry* assetRegistry = nullptr;
    ModelManager* modelManager = nullptr;
};

} // namespace HIKARI
