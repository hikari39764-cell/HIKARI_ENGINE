#include "HIKARI/Project/DebugView.h"
#include "HIKARI/Project/WorldView.h"
#include "Runtime/Diagnostics/HIKARI_RuntimeDebugService.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_World.h"
#include <cstring>

namespace HIKARI {
    DebugView GameProjectSdkAccess::MakeDebugView(World& world) noexcept {return DebugView(&world);}
    DebugView WorldView::GetDebug() const noexcept {
        auto* world=GameProjectSdkAccess::GetWorld(*this);
        return world ? GameProjectSdkAccess::MakeDebugView(*world) : DebugView{};
    }
    bool DebugView::Execute(std::string_view command,GameProjectDebugReply& reply) const noexcept {
        reply={};
        try {
            auto* world=static_cast<World*>(world_);
            if (!world || !world->IsOwnerThread() || GameProjectSdkAccess::IsWorldClosing(*world)) return false;
            auto* service=world->Services().Find<RUNTIME::RuntimeDebugService>();
            if (!service) return false;
            const auto result=service->Execute(*world,command);
            if (result.text.size()>=sizeof(reply.text)) return false;
            reply.success=result.success ? 1u : 0u;
            std::memcpy(reply.text,result.text.c_str(),result.text.size()+1);
            return true;
        } catch (...) {return false;}
    }
}
