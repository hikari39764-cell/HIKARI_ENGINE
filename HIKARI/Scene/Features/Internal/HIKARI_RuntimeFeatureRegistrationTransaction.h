#pragma once

namespace HIKARI {
class IRuntimeFeature;
class RuntimeFeatureContext;
} // namespace HIKARI

namespace HIKARI::INTERNAL {

bool RegisterRuntimeFeatureTransactional(IRuntimeFeature& feature, RuntimeFeatureContext& context);

} // namespace HIKARI::INTERNAL
