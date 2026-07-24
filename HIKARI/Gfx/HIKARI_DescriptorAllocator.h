#pragma once

#include <climits>
#include <vector>

#include <d3d12.h>

namespace HIKARI::GFX {

    struct DescriptorSlot {
        UINT index = UINT_MAX;

        bool IsValid() const {
            return index != UINT_MAX;
        }
    };

    class DescriptorAllocator {
    public:
        void Initialize(UINT begin, UINT count);
        void Reset();

        DescriptorSlot Allocate();
        void Free(DescriptorSlot slot);

        bool Owns(DescriptorSlot slot) const;
        bool IsAllocated(DescriptorSlot slot) const;
        UINT GetBegin() const;
        UINT GetCount() const;
        UINT GetUsedCount() const;
        UINT GetFreeCount() const;

    private:
        UINT begin_ = 0;
        UINT count_ = 0;
        UINT next_ = 0;
        UINT usedCount_ = 0;
        std::vector<UINT> freeList_;
        std::vector<bool> allocated_;
    };

} // namespace HIKARI::GFX
