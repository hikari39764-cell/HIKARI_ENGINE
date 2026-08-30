#include "HIKARI/Project/WorldView.h"

#include <array>

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
        std::span<const ComponentTypeId> typeIds,
        void* context,
        IndexedComponentVisitorFn visitor) noexcept {
        const WorldComponentQueryPlan plan{
            typeIds,
            WorldComponentQueryMatch::All,
            typeIds.size() == 1u
                ? WorldComponentQueryCardinality::AllEntries
                : WorldComponentQueryCardinality::FirstPerObject,
            typeIds.size() == 1u
                ? WorldComponentQueryAnchor::FirstRequiredType
                : WorldComponentQueryAnchor::SmallestUniqueBucket,
            WorldComponentQueryOrder::AnchorAttachment
        };
        return world.VisitIndexedComponents(
            plan,
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
        const std::array typeIds{ typeId };
        const bool completed =
            GameProjectSdkAccess::VisitIndexedComponents(
                *world,
                typeIds,
                &visitContext,
                [](void* opaque,
                    RuntimeObjectHandle objectHandle,
                    IComponent* const* components,
                    uint32_t componentCount) noexcept {
                    auto& visit = *static_cast<VisitContext*>(opaque);
                    if (components == nullptr || componentCount != 1u) {
                        return false;
                    }
                    GameObject* object =
                        visit.world->FindObject(objectHandle);
                    auto* adapter = dynamic_cast<
                        GameProjectComponentAdapter*>(
                            components[0]);
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
