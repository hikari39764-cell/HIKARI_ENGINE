#pragma once

#include <cstdint>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/PrefabTypes.h"

namespace HIKARI {

class ObjectView;
struct GameProjectSdkAccess;

// Non-owning facade over prefab requests and instances in one World. Request
// handles are readable until released; instance and request handles become
// invalid when their World ends.
class PrefabView {
public:
    PrefabView() = default;

    HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API GameProjectPrefabSubmission RequestInstantiate(
        std::string_view prefabAssetGuid, const GameProjectPrefabInstantiateOptions& options = {}) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ReadRequestResult(GameProjectPrefabRequestHandle request,
                                                       GameProjectPrefabRequestResult& outResult) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool CancelRequest(GameProjectPrefabRequestHandle request) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ReleaseRequest(GameProjectPrefabRequestHandle request) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API ObjectView FindInstanceObject(GameProjectPrefabInstanceHandle instance,
                                                              uint64_t prefabLocalObjectId) const noexcept;
    // Destroys every still-live object belonging to this instance. Destroying
    // one member through WorldView::DestroyObject does not destroy its peers.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    DestroyInstance(GameProjectPrefabInstanceHandle instance) const noexcept;

private:
    friend struct GameProjectSdkAccess;

    explicit PrefabView(GameProjectWorldHandle world) noexcept : world_(world) {}

    GameProjectWorldHandle world_{};
};

} // namespace HIKARI
