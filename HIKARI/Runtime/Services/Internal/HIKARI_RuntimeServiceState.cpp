#include "Runtime/Services/Internal/HIKARI_RuntimeServiceState.h"

namespace HIKARI::SERVICES::INTERNAL {
RuntimeServiceState& GetRuntimeServiceState() {
    static RuntimeServiceState state{};
    return state;
}
} // namespace HIKARI::SERVICES::INTERNAL
