#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "Scene/Features/HIKARI_RuntimeExtension.h"

namespace HIKARI::RUNTIME {

    // GameProject のビルド時合成境界。Host は一つの実装だけをリンクし、
    // Runtime は拡張を受け取るだけで Project の実装には依存しない。
    class IGameProjectModule {
    public:
        virtual ~IGameProjectModule() = default;

        virtual std::string_view GetModuleId() const noexcept = 0;
        virtual std::vector<std::unique_ptr<IRuntimeExtension>>
            CreateRuntimeExtensions() const = 0;
    };

    // 現在の Host にリンクされた GameProject target が実装する。
    std::unique_ptr<IGameProjectModule> CreateGameProjectModule();

} // namespace HIKARI::RUNTIME
