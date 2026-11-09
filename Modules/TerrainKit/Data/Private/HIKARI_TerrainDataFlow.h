#pragma once

#include <array>
#include <cstdint>
#include <vector>

// 水の流れの計算で共有する部品。
namespace HIKARI::TERRAIN::DATA {

inline constexpr std::array<int, 8> kFlowNeighborX{-1, 0, 1, -1, 1, -1, 0, 1};
inline constexpr std::array<int, 8> kFlowNeighborZ{-1, -1, -1, 0, 0, 1, 1, 1};
inline constexpr std::array<float, 8> kFlowNeighborDistance{1.41421356f, 1.0f,        1.41421356f, 1.0f,
                                                            1.0f,        1.41421356f, 1.0f,        1.41421356f};

// Priority-Flood（Barnes 2014）：窪地を埋め、全ての格子が外周へ流れる高さを作る。order は低い順の格子。
void FillDepressions(const std::vector<float>& heights, uint32_t width, uint32_t height, std::vector<float>& filled,
                     std::vector<uint32_t>& order);

} // namespace HIKARI::TERRAIN::DATA
