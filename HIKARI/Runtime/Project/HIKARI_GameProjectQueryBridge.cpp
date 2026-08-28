#include "HIKARI/Project/WorldView.h"

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

// The SDK host linker forces this C symbol so the archive member containing
// the private VisitComponents export cannot be discarded as otherwise unused.
extern "C" void HIKARI_LinkGameProjectQueryBridge() noexcept {
}

namespace HIKARI {

    bool GameProjectSdkAccess::VisitIndexedComponents(
        World& world,
        ComponentTypeId typeId,
        void* context,
        IndexedComponentVisitorFn visitor) noexcept {
        return world.VisitIndexedComponents(
            typeId,
            context,
            visitor);
    }

    bool WorldView::VisitComponents(
        ComponentTypeId typeId,
        void* context,
        VisitorFn visitor) const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        if (world == nullptr ||
            !typeId.IsValid() ||
            visitor == nullptr) {
            return false;
        }
        struct VisitContext {
            World* world = nullptr;
            void* clientContext = nullptr;
            VisitorFn clientVisitor = nullptr;
        } visitContext{ world, context, visitor };
        const bool completed =
            GameProjectSdkAccess::VisitIndexedComponents(
                *world,
                typeId,
                &visitContext,
                [](void* opaque,
                    RuntimeObjectHandle objectHandle,
                    IComponent* component) noexcept {
                    auto& visit = *static_cast<VisitContext*>(opaque);
                    GameObject* object =
                        visit.world->FindObject(objectHandle);
                    auto* adapter = dynamic_cast<
                        GameProjectComponentAdapter*>(component);
                    return object != nullptr &&
                        adapter != nullptr &&
                        visit.clientVisitor(
                            visit.clientContext,
                            GameProjectSdkAccess::MakeObjectView(
                                *object),
                            adapter->GetGameComponent());
                });
        (void)world->FlushQueuedCommands(WorldCommandPhase::QueryEnd);
        return completed;
    }

} // namespace HIKARI
