#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "HIKARI/Project/RuntimeExtension.h"

namespace HIKARI::RUNTIME {

class IGameProjectModule {
public:
    virtual ~IGameProjectModule() = default;

    virtual std::string_view GetModuleId() const noexcept = 0;
    virtual std::vector<std::unique_ptr<IRuntimeExtension>> CreateRuntimeExtensions() const = 0;
};

std::unique_ptr<IGameProjectModule> CreateGameProjectModule();

} // namespace HIKARI::RUNTIME
