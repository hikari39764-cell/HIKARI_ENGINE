#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

namespace HIKARI::GFX {
    struct DirtyUploadRange {
        size_t first = 0;
        size_t end = 0;
    };

    // インデックス区間は半開区間。断片化時は最小の隙間だけを結合し、
    // コピー命令数を制限しながら遠く離れた少数の更新を全域転送にしない。
    class DirtyUploadRanges {
    public:
        static constexpr size_t kMaximumRanges = 64;

        void Clear() noexcept { ranges_.clear(); }
        bool Empty() const noexcept { return ranges_.empty(); }
        std::span<const DirtyUploadRange> Ranges() const noexcept { return ranges_; }

        void Add(size_t first, size_t end) {
            if (first >= end) return;
            auto begin = std::lower_bound(ranges_.begin(), ranges_.end(), first,
                [](const DirtyUploadRange& range, size_t value) { return range.end < value; });
            auto finish = begin;
            while (finish != ranges_.end() && finish->first <= end) {
                first = (std::min)(first, finish->first);
                end = (std::max)(end, finish->end);
                ++finish;
            }
            if (begin == finish) ranges_.insert(begin, {first, end});
            else {
                begin->first = first;
                begin->end = end;
                ranges_.erase(begin + 1, finish);
            }
            if (ranges_.size() <= kMaximumRanges) return;
            size_t best = 0;
            for (size_t i = 1; i + 1 < ranges_.size(); ++i)
                if (ranges_[i + 1].first - ranges_[i].end <
                        ranges_[best + 1].first - ranges_[best].end) best = i;
            ranges_[best].end = ranges_[best + 1].end;
            ranges_.erase(ranges_.begin() + best + 1);
        }

    private:
        std::vector<DirtyUploadRange> ranges_;
    };
}
