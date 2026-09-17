#pragma once
#include <array>

namespace HIKARI {
    class World;
    namespace RUNTIME {
        class RuntimeDebugService;
        class RuntimeDebugWindow final {
        public:
            void Draw(World& world, RuntimeDebugService& service);
            void Reset();
            void Open() { open_ = true; focus_ = true; }
        private:
            bool open_ = false, focus_ = false;
            std::array<char,513> command_{};
        };
    }
}
