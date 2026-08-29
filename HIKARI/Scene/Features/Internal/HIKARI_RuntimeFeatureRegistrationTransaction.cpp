#include "Scene/Features/Internal/HIKARI_RuntimeFeatureRegistrationTransaction.h"

#include <type_traits>
#include <utility>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

namespace HIKARI::INTERNAL {

    bool RegisterRuntimeFeatureTransactional(
        IRuntimeFeature& feature,
        RuntimeFeatureContext& context) {

        static_assert(std::is_nothrow_move_assignable_v<
            ComponentRegistry>);
        static_assert(std::is_nothrow_move_assignable_v<
            SystemTypeRegistry>);
        static_assert(std::is_nothrow_move_assignable_v<
            ComponentSystemPolicy>);

        ComponentRegistry& components =
            RuntimeFeatureContextAccess::Components(context);
        SystemTypeRegistry& systems =
            RuntimeFeatureContextAccess::Systems(context);
        ComponentSystemPolicy& policy =
            RuntimeFeatureContextAccess::Policy(context);

        ComponentRegistry componentCheckpoint = components;
        SystemTypeRegistry systemCheckpoint = systems;
        ComponentSystemPolicy policyCheckpoint = policy;
        try {
            if (feature.Register(context)) {
                return true;
            }
        }
        catch (...) {
            // The failed feature owns no committed registry state.
        }

        components = std::move(componentCheckpoint);
        systems = std::move(systemCheckpoint);
        policy = std::move(policyCheckpoint);
        return false;
    }

} // namespace HIKARI::INTERNAL
