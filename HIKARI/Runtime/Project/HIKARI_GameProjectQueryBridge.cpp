#include "HIKARI/Project/WorldView.h"

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

// The SDK host linker forces this C symbol so the archive member containing
// the private VisitComponents export cannot be discarded as otherwise unused.
extern "C" void HIKARI_LinkGameProjectQueryBridge() noexcept {
}

namespace HIKARI {

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
        bool completed = true;
        {
            const World::StructureReadScope structureRead =
                world->AcquireStructureReadScope();
            for (const auto& object : world->GetObjects()) {
                if (!object) {
                    continue;
                }
                for (const auto& component : object->GetComponents()) {
                    auto* adapter = dynamic_cast<
                        GameProjectComponentAdapter*>(component.get());
                    if (adapter == nullptr ||
                        adapter->GetTypeName() != typeName) {
                        continue;
                    }
                    if (!visitor(
                            context,
                            GameProjectSdkAccess::MakeObjectView(*object),
                            adapter->GetGameComponent())) {
                        completed = false;
                        break;
                    }
                }
                if (!completed) {
                    break;
                }
            }
        }
        (void)world->FlushQueuedCommands(WorldCommandPhase::QueryEnd);
        return completed;
    }

} // namespace HIKARI
