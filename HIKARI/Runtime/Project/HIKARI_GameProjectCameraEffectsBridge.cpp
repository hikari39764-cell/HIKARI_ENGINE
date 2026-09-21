#include "HIKARI/Project/ActionViews.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {
uint64_t CameraView::PlayGameplayEffect(uint64_t handle,std::string_view id,float strength) const noexcept {
    try {
        auto* world=static_cast<World*>(world_);
        if(!world || !world->IsOwnerThread()) return 0;
        auto* object=world->FindObject(RuntimeObjectHandle::FromValue(handle));
        auto* follow=object?object->GetComponent<CameraFollowComponent>():nullptr;
        if(!follow || !follow->IsEnabled()) return 0;
        std::string error;
        return CAMERA::PlayGameplayCameraEffect(*object,*follow,id,strength,error);
    } catch(...) {return 0;}
}
bool CameraView::StopGameplayEffect(uint64_t handle,uint64_t token) const noexcept {
    try {
        auto* world=static_cast<World*>(world_);
        if(!world || !world->IsOwnerThread()) return false;
        auto* object=world->FindObject(RuntimeObjectHandle::FromValue(handle));
        auto* follow=object?object->GetComponent<CameraFollowComponent>():nullptr;
        return follow && CAMERA::StopGameplayCameraEffect(*follow,token);
    } catch(...) {return false;}
}
}
