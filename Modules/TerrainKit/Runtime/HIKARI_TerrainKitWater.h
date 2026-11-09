#pragma once

#include <cstdint>
#include <vector>

#include "HIKARI/Project/WaterSource.h"
#include "TerrainKit/Data/HIKARI_TerrainDataSpline.h"

// 地形の河と湖を世界の水の問い合わせへ出す（浮力、遊戯の SampleWater、道案内が同じ値を読む）。
// 曲線の層から作った形を、水の層が変わった時だけ作り直して持つ。
namespace HIKARI::TERRAINKIT {

class TerrainStore;

class TerrainWaterBodies {
public:
    bool Sample(const TerrainStore& store, float x, float z, GameProjectWaterSourceSample& out);

private:
    struct Body {
        TERRAIN::DATA::PreparedSpline spline{};
        float minimumX = 0.0f;
        float minimumZ = 0.0f;
        float maximumX = 0.0f;
        float maximumZ = 0.0f;
    };

    void Refresh(const TerrainStore& store);
    static bool SampleRiver(const Body& body, float x, float z, GameProjectWaterSourceSample& out);

    uint64_t revision_ = 0u;
    std::vector<TERRAIN::DATA::SplineLayerSettings> built_{};
    std::vector<Body> bodies_{};
};

} // namespace HIKARI::TERRAINKIT
