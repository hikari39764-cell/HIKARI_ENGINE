#pragma once

#include <memory>

namespace HIKARI {

    class IRuntimeFeature;

    std::unique_ptr<IRuntimeFeature> CreateWaterRuntimeFeature();

} // namespace HIKARI
