#include "Editor/Workspaces/LookDev/HIKARI_LookDevDocument.h"

#include <algorithm>

#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI::EDITOR {

namespace {
constexpr float kDegreesToRadians = 0.0174532925f;
}

SceneComponentData* FindLookDevComponent(SceneObjectData& object, std::string_view type) noexcept {
    const auto found = std::find_if(object.components.begin(), object.components.end(),
                                    [type](const SceneComponentData& component) { return component.type == type; });
    return found != object.components.end() ? &*found : nullptr;
}

void SyncLookDevRuntimeObject(DocumentSceneBase& scene, const SceneObjectData& object) {
    GameObject* runtime = scene.GetWorld().FindObject(object.id);
    if (runtime == nullptr) {
        return;
    }
    const MATH::Vec3& euler = object.transform.rotationEulerDeg;
    (void)runtime->SetLocalPosition(object.transform.position);
    (void)runtime->SetLocalRotation(MATH::Quat::FromEulerXYZ(euler.x * kDegreesToRadians, euler.y * kDegreesToRadians,
                                                             euler.z * kDegreesToRadians));
    (void)runtime->SetLocalScale(object.transform.scale);
    for (size_t index = 0; index < object.components.size(); ++index) {
        const SceneComponentData& component = object.components[index];
        size_t ordinal = static_cast<size_t>(
            std::count_if(object.components.begin(), object.components.begin() + static_cast<std::ptrdiff_t>(index),
                          [&](const SceneComponentData& other) { return other.type == component.type; }));
        for (const auto& entry : runtime->GetComponents()) {
            if (entry && entry->GetTypeName() == component.type && ordinal-- == 0u) {
                entry->Deserialize(component.properties);
                break;
            }
        }
    }
}

MATH::Vec3 LookDevParentLocalVector(DocumentSceneBase& scene, const SceneObjectData& object, const MATH::Vec3& world) {
    if (!object.parent) {
        return world;
    }
    const GameObject* parent = scene.GetWorld().FindObject(*object.parent);
    if (parent == nullptr) {
        return world;
    }
    const MATH::Vec4 local =
        MATH::Inverse(parent->GetTransform().GetWorldMatrix()).TransformPoint({world.x, world.y, world.z, 0.0f});
    return {local.x, local.y, local.z};
}

MATH::Mat4 LookDevWorldMatrix(DocumentSceneBase& scene, SceneObjectId id) {
    const GameObject* object = scene.GetWorld().FindObject(id);
    return object != nullptr ? object->GetTransform().GetWorldMatrix() : MATH::Mat4::Identity();
}

} // namespace HIKARI::EDITOR
