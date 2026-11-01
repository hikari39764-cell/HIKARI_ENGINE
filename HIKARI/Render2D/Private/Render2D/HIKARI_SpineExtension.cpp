#include <spine/Extension.h>
#include <spine/SpineString.h>
#include "Core/Text/HIKARI_UtfPath.h"
#include <fstream>
#include <limits>

namespace spine {
namespace {
class HikariSpineExtension final : public DefaultSpineExtension {
    char* _readFile(const String& path, int* length) override {
        if (!length)
            return nullptr;
        *length = 0;
        std::ifstream input(HIKARI::TEXT::PathFromUtf8(path.buffer()), std::ios::binary | std::ios::ate);
        if (!input)
            return nullptr;
        const auto size = input.tellg();
        if (size <= 0 || size >= (std::numeric_limits<int>::max)())
            return nullptr;
        input.seekg(0);
        auto* data = SpineExtension::alloc<char>(static_cast<size_t>(size) + 1u, __FILE__, __LINE__);
        if (!data)
            return nullptr;
        if (!input.read(data, size)) {
            SpineExtension::free(data, __FILE__, __LINE__);
            return nullptr;
        }
        data[static_cast<size_t>(size)] = '\0';
        *length = static_cast<int>(size);
        return data;
    }
};
} // namespace
SP_API SpineExtension* getDefaultExtension() { return new HikariSpineExtension(); }
} // namespace spine
