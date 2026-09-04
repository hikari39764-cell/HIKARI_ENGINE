#pragma once

#include <string_view>

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
        // Additive C++ SDK entry point. The request layout remains identical
        // to the legacy Submit path; when nodeName is valid, only the first
        // case-sensitive matching model node and its descendants are
        // rendered. The name is limited to the engine's 255-byte node-name
        // admission limit. An empty node name is rejected by this entry
        // point.
        HIKARI_GAME_PROJECT_SDK_API bool SubmitExclusiveNode(
            const GameProjectPresentationModelRequest& request,
            std::string_view nodeName) const noexcept;
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
