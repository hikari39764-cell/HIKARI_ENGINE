#pragma once

#include <memory>

namespace HIKARI {

    class IRuntimeFeature;

    std::unique_ptr<IRuntimeFeature> CreateLightingRuntimeFeature();

} // namespace HIKARI
