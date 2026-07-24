#include "Scene/Debug/HIKARI_ComponentGizmoRegistry.h"

#include <algorithm>
#include <utility>

namespace HIKARI {

    bool ComponentGizmoState::IsProviderVisible(
        std::string_view providerId,
        bool defaultVisible) const {

        const auto found = providerVisibility.find(
            std::string(providerId));
        return found != providerVisibility.end()
            ? found->second
            : defaultVisible;
    }

    void ComponentGizmoState::SetProviderVisible(
        std::string providerId,
        bool visible) {

        if (!providerId.empty()) {
            providerVisibility[std::move(providerId)] = visible;
        }
    }

    bool ComponentGizmoRegistry::Register(
        ComponentGizmoProvider provider) {

        if (provider.providerId.empty() ||
            provider.displayName.empty() ||
            !provider.draw ||
            Find(provider.providerId) != nullptr) {
            return false;
        }
        providers_.push_back(std::move(provider));
        return true;
    }

    const ComponentGizmoProvider* ComponentGizmoRegistry::Find(
        std::string_view providerId) const noexcept {

        const auto found = std::find_if(
            providers_.begin(),
            providers_.end(),
            [providerId](const ComponentGizmoProvider& provider) {
                return provider.providerId == providerId;
            });
        return found != providers_.end() ? &*found : nullptr;
    }

    const std::vector<ComponentGizmoProvider>&
        ComponentGizmoRegistry::GetProviders() const noexcept {
        return providers_;
    }

} // namespace HIKARI
