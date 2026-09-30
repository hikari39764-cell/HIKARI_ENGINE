#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace HIKARI {

// UTF-8 path length accepted by project Data readers and Editor documents.
inline constexpr size_t kMaximumProjectDataPathBytes = 512u;
inline constexpr size_t kMaximumProjectDataTextBytes = 4u * 1024u * 1024u;

enum class ProjectDataReadCode : uint32_t {
    Ok,
    InvalidWorld,
    WrongThread,
    InvalidPath,
    ReparsePoint,
    NotFound,
    TooLarge,
    InvalidUtf8,
    IoFailure,
    CallbackFailed,
};

struct ProjectDataReadResult {
    ProjectDataReadCode code = ProjectDataReadCode::InvalidWorld;
    uint64_t byteCount = 0u;

    constexpr bool Succeeded() const noexcept { return code == ProjectDataReadCode::Ok; }
};

static_assert(std::is_standard_layout_v<ProjectDataReadResult>);
static_assert(std::is_trivially_copyable_v<ProjectDataReadResult>);
static_assert(sizeof(ProjectDataReadResult) == 16u);

} // namespace HIKARI
