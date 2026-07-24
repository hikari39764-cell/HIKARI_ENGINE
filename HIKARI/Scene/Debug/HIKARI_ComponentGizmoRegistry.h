#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    class GameObject;
    class World;

    struct ComponentGizmoState {
        bool showComponentGizmos = true;
        bool showOnlySelectedObject = false;
        std::unordered_map<std::string, bool> providerVisibility{};
        std::unordered_set<uint64_t> lockedObjectIds{};

        bool IsProviderVisible(
            std::string_view providerId,
            bool defaultVisible) const;
        void SetProviderVisible(
            std::string providerId,
            bool visible);
    };

    struct ComponentGizmoDrawContext {
        SceneObjectId selectedObjectId{};
        float cameraAspect = 1.0f;
        const World* world = nullptr;
    };

    using DrawComponentGizmoFn = std::function<void(
        const GameObject& object,
        const ComponentGizmoDrawContext& context)>;

    struct ComponentGizmoProvider {
        std::string providerId{};
        std::string displayName{};
        bool defaultVisible = true;
        DrawComponentGizmoFn draw{};
    };

    class ComponentGizmoRegistry {
    public:
        bool Register(ComponentGizmoProvider provider);
        const ComponentGizmoProvider* Find(
            std::string_view providerId) const noexcept;
        const std::vector<ComponentGizmoProvider>&
            GetProviders() const noexcept;

    private:
        std::vector<ComponentGizmoProvider> providers_{};
    };

} // namespace HIKARI
