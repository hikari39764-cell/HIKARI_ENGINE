#pragma once

#include <cstdint>
#include <vector>

// 地形の成り立ちを読む。材質と色の分布を噪声ではなく「水がどこに集まるか」「尾根か谷か」で決めるための値。
//   湿り：地形湿潤指数（Beven & Kirkby 1979、ln(集水面積 / 傾き)）。集水面積は多方向流（Freeman 1991）。
//   位置：地形位置指数（周りの平均との高さの差）。正が谷、負が尾根。
//   岩屑：上流の面積のうち急な岩の割合。水の筋に沿って岩の色が麓まで運ばれる。
namespace HIKARI::TERRAIN::DATA {

struct TerrainAnalysis {
    uint32_t width = 0u;
    uint32_t height = 0u;
    double originX = 0.0; // 格子 (0, 0) の世界座標
    double originZ = 0.0;
    float cellMeters = 8.0f;
    std::vector<float> heights{};  // 粗い高さ（入力）
    std::vector<float> wetness{};  // 0（乾いた尾根）.. 1（水が集まる谷底）
    std::vector<float> position{}; // -1（尾根）.. 1（谷底）
    std::vector<float> debris{};   // 0 .. 1（水の筋に運ばれた岩屑）

    bool Valid() const noexcept {
        return width >= 2u && height >= 2u && wetness.size() == static_cast<size_t>(width) * height &&
               position.size() == wetness.size() && debris.size() == wetness.size();
    }
    float WetnessAt(double worldX, double worldZ) const noexcept;
    float PositionAt(double worldX, double worldZ) const noexcept;
    float DebrisAt(double worldX, double worldZ) const noexcept;
};

// heights と寸法から wetness、position、debris を求める。
void BuildTerrainAnalysis(TerrainAnalysis& analysis);

} // namespace HIKARI::TERRAIN::DATA
