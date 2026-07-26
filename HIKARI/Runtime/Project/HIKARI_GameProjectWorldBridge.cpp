#include "HIKARI/Project/WorldView.h"

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    ObjectView GameProjectSdkAccess::MakeObjectView(
        GameObject& object) noexcept {
        return ObjectView(&object);
    }

    WorldView GameProjectSdkAccess::MakeWorldView(
        World& world) noexcept {
        return WorldView(&world);
    }

    GameObject* GameProjectSdkAccess::GetObject(
        const ObjectView& view) noexcept {
        return static_cast<GameObject*>(view.object_);
    }

    World* GameProjectSdkAccess::GetWorld(
        const WorldView& view) noexcept {
        return static_cast<World*>(view.world_);
    }

    uint64_t ObjectView::GetHandle() const noexcept {
        const GameObject* object =
            GameProjectSdkAccess::GetObject(*this);
        return object != nullptr
            ? object->GetRuntimeHandle().ToValue()
            : 0u;
    }

    std::string_view ObjectView::GetName() const noexcept {
        const GameObject* object =
            GameProjectSdkAccess::GetObject(*this);
        return object != nullptr
            ? std::string_view(object->GetName())
            : std::string_view{};
    }

    GameProjectFloat3 ObjectView::GetLocalPosition() const noexcept {
        const GameObject* object =
            GameProjectSdkAccess::GetObject(*this);
        if (object == nullptr) {
            return {};
        }
        const MATH::Vec3& position =
            object->GetTransform().position;
        return { position.x, position.y, position.z };
    }

    bool ObjectView::SetLocalPosition(
        GameProjectFloat3 position) const noexcept {
        GameObject* object =
            GameProjectSdkAccess::GetObject(*this);
        return object != nullptr &&
            object->SetLocalPosition({
                position.x,
                position.y,
                position.z
            });
    }

    ObjectView WorldView::FindObject(
        uint64_t handle) const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        if (world == nullptr || handle == 0u) {
            return {};
        }
        RuntimeObjectHandle runtimeHandle{};
        runtimeHandle.slot =
            static_cast<uint32_t>((handle & 0xFFFFFFFFu) - 1u);
        runtimeHandle.generation =
            static_cast<uint32_t>(handle >> 32u);
        GameObject* object = world->FindObject(runtimeHandle);
        return object != nullptr
            ? GameProjectSdkAccess::MakeObjectView(*object)
            : ObjectView{};
    }

    bool WorldView::VisitComponents(
        std::string_view typeName,
        void* context,
        VisitorFn visitor) const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        if (world == nullptr ||
            typeName.empty() ||
            visitor == nullptr) {
            return false;
        }
        for (const auto& object : world->GetObjects()) {
            if (!object) {
                continue;
            }
            for (const auto& component :
                    object->GetComponents()) {
                auto* adapter = dynamic_cast<
                    GameProjectComponentAdapter*>(
                        component.get());
                if (adapter == nullptr ||
                    adapter->GetTypeName() != typeName) {
                    continue;
                }
                if (!visitor(
                        context,
                        GameProjectSdkAccess::MakeObjectView(*object),
                        adapter->GetGameComponent())) {
                    return false;
                }
            }
        }
        return true;
    }

} // namespace HIKARI
