#pragma once

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/PresentationTypes.h"

namespace HIKARI {

    struct GameProjectSdkAccess;

    class PresentationModelView {
    public:
        PresentationModelView() = default;

        bool IsValid() const noexcept {
            return world_ != nullptr;
        }

        HIKARI_GAME_PROJECT_SDK_API bool Submit(
            const GameProjectPresentationModelRequest& request)
            const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool Remove(
            uint64_t ownerObjectHandle,
            uint64_t instanceId) const noexcept;

    private:
        friend struct GameProjectSdkAccess;

        explicit PresentationModelView(void* world) noexcept
            : world_(world) {}

        void* world_ = nullptr;
    };

} // namespace HIKARI
