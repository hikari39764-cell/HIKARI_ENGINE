#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace HIKARI {

    struct ComponentTypeId {
        uint64_t value = 0u;

        constexpr bool IsValid() const noexcept {
            return value != 0u;
        }

        constexpr explicit operator bool() const noexcept {
            return IsValid();
        }

        friend constexpr bool operator==(
            ComponentTypeId lhs,
            ComponentTypeId rhs) noexcept {
            return lhs.value == rhs.value;
        }

        friend constexpr bool operator!=(
            ComponentTypeId lhs,
            ComponentTypeId rhs) noexcept {
            return !(lhs == rhs);
        }
    };

    // Stable FNV-1a over the exact type-name byte sequence. Type names are
    // case-sensitive serialized identities and must not be renamed casually.
    constexpr ComponentTypeId MakeComponentTypeId(
        std::string_view typeName) noexcept {
        if (typeName.empty()) {
            return {};
        }
        uint64_t hash = 14695981039346656037ull;
        for (const char value : typeName) {
            hash ^= static_cast<uint8_t>(value);
            hash *= 1099511628211ull;
        }
        return ComponentTypeId{ hash != 0u ? hash : 1u };
    }

    static_assert(std::is_standard_layout_v<ComponentTypeId>);
    static_assert(std::is_trivially_copyable_v<ComponentTypeId>);
    static_assert(sizeof(ComponentTypeId) == 8u);
    static_assert(
        MakeComponentTypeId("HIKARI.ComponentTypeId.Contract").value ==
        0x97c8217546ecd921ull);

} // namespace HIKARI
