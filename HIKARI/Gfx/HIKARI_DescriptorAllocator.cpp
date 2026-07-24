#include "Gfx/HIKARI_DescriptorAllocator.h"

#include <cassert>

namespace HIKARI::GFX {

    void DescriptorAllocator::Initialize(UINT begin, UINT count) {
        begin_ = begin;
        count_ = count;
        Reset();
    }

    void DescriptorAllocator::Reset() {
        next_ = 0;
        usedCount_ = 0;
        freeList_.clear();
        allocated_.assign(count_, false);
    }

    DescriptorSlot DescriptorAllocator::Allocate() {
        if (!freeList_.empty()) {
            const UINT index = freeList_.back();
            freeList_.pop_back();

            const UINT localIndex = index - begin_;
            if (static_cast<size_t>(localIndex) >= allocated_.size()) {
                return {};
            }

            allocated_[localIndex] = true;
            ++usedCount_;
            return { index };
        }

        if (next_ >= count_) {
            return {};
        }

        const UINT localIndex = next_++;
        allocated_[localIndex] = true;
        ++usedCount_;
        return { begin_ + localIndex };
    }

    void DescriptorAllocator::Free(DescriptorSlot slot) {
        if (!Owns(slot)) {
            return;
        }

        const UINT localIndex = slot.index - begin_;
        if (static_cast<size_t>(localIndex) >= allocated_.size()) {
            return;
        }

        if (!allocated_[localIndex]) {
            assert(false && "DescriptorAllocator::Free received a duplicate free.");
            return;
        }

        allocated_[localIndex] = false;
        if (usedCount_ > 0) {
            --usedCount_;
        }
        freeList_.push_back(slot.index);
    }

    bool DescriptorAllocator::Owns(DescriptorSlot slot) const {
        if (!slot.IsValid()) {
            return false;
        }

        return slot.index >= begin_ && slot.index < begin_ + count_;
    }

    bool DescriptorAllocator::IsAllocated(DescriptorSlot slot) const {
        if (!Owns(slot)) {
            return false;
        }

        const UINT localIndex = slot.index - begin_;
        if (static_cast<size_t>(localIndex) >= allocated_.size()) {
            return false;
        }

        return allocated_[localIndex];
    }

    UINT DescriptorAllocator::GetBegin() const {
        return begin_;
    }

    UINT DescriptorAllocator::GetCount() const {
        return count_;
    }

    UINT DescriptorAllocator::GetUsedCount() const {
        return usedCount_;
    }

    UINT DescriptorAllocator::GetFreeCount() const {
        return count_ - usedCount_;
    }

} // namespace HIKARI::GFX
