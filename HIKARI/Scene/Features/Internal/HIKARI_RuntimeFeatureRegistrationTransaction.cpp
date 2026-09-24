#include "Scene/Features/Internal/HIKARI_RuntimeFeatureRegistrationTransaction.h"

#include <type_traits>
#include <utility>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/HIKARI_WorldMassStore.h"

namespace HIKARI::INTERNAL {

bool RegisterRuntimeFeatureTransactional(IRuntimeFeature& feature, RuntimeFeatureContext& context) {
    static_assert(std::is_nothrow_move_assignable_v<ComponentRegistry>);
    static_assert(std::is_nothrow_move_assignable_v<WorldMassSchemaRegistry>);
    static_assert(std::is_nothrow_move_assignable_v<SystemTypeRegistry>);
    static_assert(std::is_nothrow_move_assignable_v<ComponentSystemPolicy>);

    ComponentRegistry& components = RuntimeFeatureContextAccess::Components(context);
    WorldMassSchemaRegistry& massSchemas = RuntimeFeatureContextAccess::MassSchemas(context);
    SystemTypeRegistry& systems = RuntimeFeatureContextAccess::Systems(context);
    ComponentSystemPolicy& policy = RuntimeFeatureContextAccess::Policy(context);

    try {
        ComponentRegistry componentCheckpoint = components;
        WorldMassSchemaRegistry massSchemaCheckpoint = massSchemas;
        SystemTypeRegistry systemCheckpoint = systems;
        ComponentSystemPolicy policyCheckpoint = policy;
        try {
            if (feature.Register(context)) {
                return true;
            }
        } catch (...) {
            // The failed feature owns no committed registry state.
        }
        components = std::move(componentCheckpoint);
        massSchemas = std::move(massSchemaCheckpoint);
        systems = std::move(systemCheckpoint);
        policy = std::move(policyCheckpoint);
    } catch (...) {
        // Checkpoint allocation failed before feature mutation began.
    }
    return false;
}

} // namespace HIKARI::INTERNAL
