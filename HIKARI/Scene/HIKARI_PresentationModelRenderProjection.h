#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"

namespace HIKARI {

    class AssetRegistry;
    class Material;
    class ModelManager;
    class PresentationModelService;
    class PresentationTransformService;
    class World;

    namespace ANIMATION {
        class AnimationPoseService;
    }

    namespace RENDER3D::RUNTIME {
        class RenderModelCache;
        class SceneRenderCache;
    }

    namespace PRESENTATION_MODEL {

        // Runtime materials and their raw-pointer descriptors are owned by
        // the SceneRenderCache bucket that references them. Keeping this
        // state per cache prevents a second view's sync from invalidating
        // material pointers in the first view. Identity changes rebuild the
        // owned values; unchanged records reuse them for the frame.
        struct MaterialStorage {
            std::string modelAssetGuid{};
            std::string materialSetAssetGuid{};
            const AssetRegistry* assetRegistry = nullptr;
            uint64_t assetRegistryInstanceId = 0u;
            uint64_t assetRegistryRevision = 0u;
            std::vector<std::unique_ptr<Material>> owned{};
            std::vector<RENDER3D::RUNTIME::SceneRenderMaterialOverride>
                overrides{};
        };
        using MaterialStorageByRenderObject =
            std::unordered_map<uint64_t, MaterialStorage>;

        // Projects render-only presentation records into the supplied cache.
        // The service owns record freshness; this owner resolves assets,
        // sockets, transforms, bounds, shadows, and formal material sets.
        void SyncRenderObjects(
            World& world,
            const PresentationModelService* presentationModels,
            RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
            RENDER3D::RUNTIME::SceneRenderCache& sceneRenderCache,
            const AssetRegistry* assetRegistry,
            ModelManager* modelManager,
            const std::filesystem::path& projectRoot,
            const PresentationTransformService* presentationTransforms,
            const ANIMATION::AnimationPoseService* animationPoses,
            MaterialStorageByRenderObject& materialStorage,
            std::unordered_set<std::string>& missingAssetDiagnostics,
            std::unordered_set<std::string>& missingNodeDiagnostics,
            std::unordered_set<std::string>& attachmentDiagnostics,
            std::unordered_set<std::string>& materialDiagnostics);

    } // namespace PRESENTATION_MODEL
} // namespace HIKARI
