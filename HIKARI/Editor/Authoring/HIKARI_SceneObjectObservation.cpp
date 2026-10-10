#include "Editor/Authoring/HIKARI_SceneObjectObservation.h"
#include "Editor/Authoring/HIKARI_SceneTransformAuthoring.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Assets/Models/HIKARI_ModelBounds.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/HIKARI_World.h"
#include <algorithm>

namespace HIKARI::EDITOR {
namespace {
nlohmann::json Vector(const MATH::Vec3& value) { return {value.x, value.y, value.z}; }
constexpr TEXT::EnumName<ModelAsset::State> kModelStates[] = {{ModelAsset::State::Unloaded, "unloaded"},
                                                              {ModelAsset::State::Loaded, "loaded"},
                                                              {ModelAsset::State::Failed, "failed"}};
} // namespace
Bounds ObserveSceneObjectBounds(const DocumentSceneBase& scene, SceneObjectId objectId) {
    const auto* object = scene.GetWorld().FindObject(objectId);
    if (!object)
        return {};
    // 程序化の形は今の設定から求める（同じ批量で作った直後や寸法を変えた直後でも描画を待たない）。
    if (const auto* shape = object->GetComponent<ProceduralMeshComponent>()) {
        const MATH::Vec3 half = GetProceduralMeshExtent(shape->GetSettings()) * 0.5f;
        return BOUNDS::TransformBounds({half * -1.0f, half}, object->GetTransform().GetWorldMatrix());
    }
    const auto* model = object->GetComponent<ModelComponent>();
    if (model && model->GetModelAsset() && BOUNDS::IsUsable(model->GetModelAsset()->bounds))
        return BOUNDS::TransformBounds(model->GetModelAsset()->bounds, object->GetTransform().GetWorldMatrix());
    const auto* render = RenderSubmissionSystem::GetSceneRenderCache().Find({objectId.value});
    if (render && BOUNDS::IsUsable(render->desc.localBounds))
        return BOUNDS::TransformBounds(render->desc.localBounds, object->GetTransform().GetWorldMatrix());
    const auto findBounds = [objectId](const auto& source) -> Bounds {
        const auto found = std::find_if(source.instances.begin(), source.instances.end(), [objectId](const auto& item) {
            return item.objectId == objectId.value && BOUNDS::IsUsable(item.worldBounds);
        });
        return found != source.instances.end() ? found->worldBounds : Bounds{};
    };
    const auto sprite = findBounds(RenderSubmissionSystem::GetWorldSpriteFrameSource());
    return BOUNDS::IsUsable(sprite) ? sprite : findBounds(RenderSubmissionSystem::GetWorldTextFrameSource());
}

nlohmann::json ObserveSceneObject(const DocumentSceneBase& scene, const SceneObjectData& authored) {
    nlohmann::json result{{"localTransform", DescribeSceneObjectTransform(authored)}};
    // 未ロードのモデルでも、文書に保存された参照は失わない。
    for (const auto& component : authored.components) {
        if (component.type != "ModelComponent")
            continue;
        const auto id = component.properties.value("assetId", std::string{});
        auto& model = result["model"];
        model = {{"assetId", id}, {"state", "unresolved"}};
        if (const auto* record = scene.GetAssetDatabase().FindByGuid(AssetGuid{id}))
            model["sourcePath"] = TEXT::PathToUtf8(record->sourcePath);
        model["materialSetAssetGuid"] = component.properties.value("materialSetAssetGuid", std::string{});
        break;
    }
    const auto* object = scene.GetWorld().FindObject(authored.id);
    result["runtimeAvailable"] = object != nullptr;
    if (!object)
        return result;
    const auto world = object->GetTransform().GetWorldMatrix();
    const MATH::Vec3 position{world.m[3][0], world.m[3][1], world.m[3][2]};
    result["worldPosition"] = Vector(position);
    if (const auto* component = object->GetComponent<ModelComponent>()) {
        auto& model = result["model"];
        model["assetId"] = component->GetAssetId();
        model["visible"] = component->IsVisible();
        if (const auto* asset = component->GetModelAsset()) {
            model["state"] = TEXT::EnumToName(kModelStates, asset->GetState());
            model["sourcePath"] = asset->GetSourcePath();
            model["meshCount"] = asset->meshes.size();
            model["nodeCount"] = asset->nodes.size();
            model["skinCount"] = asset->skins.size();
            model["animationCount"] = asset->animations.size();
            model["materialSlotCount"] = asset->materials.size();
            auto slots = nlohmann::json::array();
            for (const auto& material : asset->materials) {
                if (slots.size() == 16u)
                    break;
                nlohmann::json slot{{"slotId", material.slotId.value}, {"name", material.name}};
                for (const auto& override : component->GetMaterialOverrides())
                    if (override.slotId == material.slotId)
                        slot["overrideAssetGuid"] = override.materialAssetGuid.value;
                slots.push_back(std::move(slot));
            }
            model["materialSlots"] = std::move(slots);
            model["materialSlotsTruncated"] = asset->materials.size() > 16u;
        }
    }
    const auto bounds = ObserveSceneObjectBounds(scene, authored.id);
    const bool hasBounds = BOUNDS::IsUsable(bounds);
    result["worldBounds"] = hasBounds ? nlohmann::json{{"min", Vector(bounds.min)},
                                                       {"max", Vector(bounds.max)},
                                                       {"size", Vector(bounds.max - bounds.min)}}
                                      : nlohmann::json(nullptr);
    const auto& camera = scene.GetResolvedCameraFrame();
    if (camera.valid) {
        const auto center = hasBounds ? (bounds.min + bounds.max) * 0.5f : position;
        const auto clip = camera.camera.GetViewProj().TransformPoint({center.x, center.y, center.z, 1.0f});
        if (MATH::IsFinite(clip.x) && MATH::IsFinite(clip.y) && MATH::IsFinite(clip.z) && MATH::IsFinite(clip.w) &&
            clip.w > 1e-5f) {
            const float x = 0.5f + 0.5f * clip.x / clip.w;
            const float y = 0.5f - 0.5f * clip.y / clip.w;
            result["screenAnchor"] = {
                {"uv", {x, y}},
                {"insideFrustum", x >= 0 && x <= 1 && y >= 0 && y <= 1 && clip.z >= 0 && clip.z <= clip.w}};
        } else {
            result["screenAnchor"] = nullptr;
        }
    }
    return result;
}
} // namespace HIKARI::EDITOR
