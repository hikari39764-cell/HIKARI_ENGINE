#pragma once

#include <cstddef>
#include <limits>
#include <vector>

namespace HIKARI::GFX {

struct UploadPageRange {
    size_t capacity = 0u;
    size_t offset = 0u;
};

struct UploadPageReservation {
    size_t pageIndex = 0u;
    size_t offset = 0u;
    size_t size = 0u;
};

inline bool TryAlignUploadSize(size_t size, size_t alignment, size_t& outAlignedSize) noexcept {
    if (size == 0u || alignment == 0u || (alignment & (alignment - 1u)) != 0u ||
        size > std::numeric_limits<size_t>::max() - (alignment - 1u)) {
        outAlignedSize = 0u;
        return false;
    }
    outAlignedSize = (size + alignment - 1u) & ~(alignment - 1u);
    return true;
}

inline bool TryReserveUploadPageRange(std::vector<UploadPageRange>& pages, size_t& activePageIndex, size_t size,
                                      size_t alignment, UploadPageReservation& outReservation) noexcept {
    size_t alignedSize = 0u;
    if (!TryAlignUploadSize(size, alignment, alignedSize)) {
        outReservation = {};
        return false;
    }

    for (size_t pageIndex = activePageIndex; pageIndex < pages.size(); ++pageIndex) {
        UploadPageRange& page = pages[pageIndex];
        if (alignedSize > page.capacity || page.offset > page.capacity - alignedSize) {
            continue;
        }
        outReservation = {
            pageIndex,
            page.offset,
            alignedSize,
        };
        page.offset += alignedSize;
        activePageIndex = pageIndex;
        return true;
    }

    activePageIndex = pages.size();
    outReservation = {};
    return false;
}

inline void ResetUploadPageRanges(std::vector<UploadPageRange>& pages, size_t& activePageIndex) noexcept {
    for (UploadPageRange& page : pages) {
        page.offset = 0u;
    }
    activePageIndex = 0u;
}

} // namespace HIKARI::GFX
