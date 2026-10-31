#pragma once

#include <cassert>
#include <cstdio>
#include <cstdlib>

#if defined(NDEBUG)

namespace HIKARI::TESTS::INTERNAL {

[[noreturn]] inline void FailContractAssertion(const char* expression, const char* file, int line) noexcept {
    std::fprintf(stderr, "%s(%d): contract assertion failed: %s\n", file, line, expression);
    std::abort();
}

} // namespace HIKARI::TESTS::INTERNAL

#undef assert
#define assert(expression)                                                                                             \
    ((expression) ? static_cast<void>(0)                                                                               \
                  : HIKARI::TESTS::INTERNAL::FailContractAssertion(#expression, __FILE__, __LINE__))

#endif
