#pragma once

#include "Render3D/ScreenSpace/HIKARI_ScreenReadability.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Scene/Capture/HIKARI_SceneCaptureService.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_RuntimeObjectActivityService.h"

namespace HIKARI::SCENE_CAPTURE {

inline RENDER3D::ScreenReadabilityWorldFrame CollectScreenReadability(
    World& world, SceneCaptureService& captures) {
    RENDER3D::ScreenReadabilityWorldFrame frame{};
    const auto* presentation = world.Services().Find<PresentationTransformService>();
    const auto* activity = world.Services().Find<RuntimeObjectActivityService>();
    for (const auto& binding : captures.Bindings()) {
        if (!binding.applied || (binding.flags & GameProjectSceneCaptureMaterialBindingFlags(
                GameProjectSceneCaptureMaterialBindingFlag::ProtectReadability)) == 0u) continue;
        auto* object = world.FindObject(binding.modelObject);
        auto* model = object ? object->GetComponent<ModelComponent>() : nullptr;
        const auto* asset = model ? model->GetModelAsset() : nullptr;
        if (!model || !model->IsVisible() || !asset ||
            (activity && activity->Get(object->GetRuntimeHandle()) == GameProjectRuntimeActivity::SuspendedHidden)) continue;
        Transform3D transform{};
        if (!world.TryGetWorldTransform(object->GetRuntimeHandle(), transform)) continue;
        auto worldMatrix = transform.GetWorldMatrix();
        if (presentation) (void)presentation->TryGetWorldMatrix(object->GetRuntimeHandle(), worldMatrix);
        const auto visibility = model->ResolveRuntimeNodeVisibilityMask(*asset);
        for (size_t nodeIndex = 0; nodeIndex < asset->nodes.size(); ++nodeIndex) {
            const auto& node = asset->nodes[nodeIndex];
            if (node.meshIndex < 0 || static_cast<size_t>(node.meshIndex) >= asset->meshes.size() || node.skinIndex >= 0 ||
                (!visibility.empty() && !visibility[nodeIndex])) continue;
            for (const auto& primitive : asset->meshes[node.meshIndex].primitives) {
                if (primitive.materialIndex != binding.resolvedMaterialIndex || primitive.layout != VertexLayoutKind::StaticPNTT) continue;
                if (frame.count == RENDER3D::kScreenReadabilityRegionCount) return frame;
                const auto& bounds = primitive.bounds;
                const float low[]{bounds.min.x, bounds.min.y, bounds.min.z};
                const float high[]{bounds.max.x, bounds.max.y, bounds.max.z};
                const float extent[]{high[0]-low[0], high[1]-low[1], high[2]-low[2]};
                const int thin = static_cast<int>(std::min_element(extent, extent + 3) - extent);
                const int u = (thin + 1) % 3, v = (thin + 2) % 3;
                if (extent[u] < .00001f || extent[v] < .00001f || extent[thin] > (std::min)(extent[u],extent[v]) * .02f) continue;
                const auto matrix = worldMatrix * node.globalBindMatrix;
                constexpr float signs[4][2]{{-1,-1},{1,-1},{1,1},{-1,1}};
                for (uint32_t corner = 0; corner < 4; ++corner) {
                    float p[]{(low[0]+high[0])*.5f,(low[1]+high[1])*.5f,(low[2]+high[2])*.5f};
                    p[u] += extent[u] * .48f * signs[corner][0];
                    p[v] += extent[v] * .48f * signs[corner][1];
                    const auto point = matrix.TransformPoint({p[0],p[1],p[2],1});
                    frame.regions[frame.count][corner] = {point.x,point.y,point.z};
                }
                ++frame.count;
            }
        }
    }
    return frame;
}

}
