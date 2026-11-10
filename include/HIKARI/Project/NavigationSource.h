#pragma once

#include <cstdint>
#include <span>

namespace HIKARI {

// モジュールの幾何を道案内の網の入力へ足す接口。World に登録して使う（WorldView::AddNavigationGeometrySource）。
// 塊ごとに版を持ち、変わった塊だけ作り直す。三角形は上から見て法線が +Y を向く順。
class INavigationGeometrySink {
public:
    virtual ~INavigationGeometrySink() = default;
    // xyz は世界座標の頂点（3 個ずつ）、triangles はこの呼び出しの頂点への番号（3 個ずつ）。
    virtual void Append(std::span<const float> xyz, std::span<const int32_t> triangles) = 0;
};

class INavigationGeometrySource {
public:
    virtual ~INavigationGeometrySource() = default;
    virtual uint32_t GetNavigationChunkCount() const = 0;
    virtual bool GetNavigationChunk(uint32_t index, int32_t& outX, int32_t& outZ, uint64_t& outRevision) const = 0;
    virtual void AppendNavigationChunk(int32_t x, int32_t z, INavigationGeometrySink& sink) const = 0;
};

} // namespace HIKARI
