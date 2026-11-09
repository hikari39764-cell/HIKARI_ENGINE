#pragma once

#include <cstdint>
#include <vector>

#include "HIKARI/Project/RenderSurface.h"
#include "TerrainKit/Data/HIKARI_TerrainDataSpline.h"

// 河と湖の水面：曲線の層から帯を作って描く。湖は範囲を行ごとの帯に切る。帯は層が変わった時だけ作り直す。
namespace HIKARI::TERRAINKIT {

class TerrainStore;

class TerrainRivers {
public:
    // 描画の前に毎フレーム一度。
    void Prepare(IRenderSurfaceResources& resources, const TerrainStore& store);
    bool HasWork() const noexcept { return !ribbons_.empty() && indexBuffer_ != 0u; }
    void Record(const RenderSurfaceView& view, IRenderSurfaceCommands& commands);
    void OnRenderResourcesLost() noexcept { indexBuffer_ = 0u; }
    // 編集データを替えた時。次の Prepare で層を読み直す（通し番号はデータごとなので偶然一致しうる）。
    void Invalidate() noexcept { revision_ = 0u; }
    size_t RibbonCount() const noexcept { return ribbons_.size(); }

private:
    struct CrossSection {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float left = 0.0f;
        float right = 0.0f;
        float tangentX = 0.0f;
        float tangentZ = 1.0f;
        float along = 0.0f;
    };
    struct Section {
        CrossSection a{};
        CrossSection b{};
    };
    struct Ribbon {
        std::vector<Section> sections{};
        MATH::Vec3 color{};
        float clarity = 1.0f;
        float flowSpeed = 0.0f;
        bool worldAligned = false; // 波を世界座標で作る（湖。河は曲線の座標）
    };

    void Rebuild(const std::vector<TERRAIN::DATA::SplineLayerSettings>& waters);
    void AppendRiver(const TERRAIN::DATA::PreparedSpline& spline, Ribbon& ribbon) const;
    void AppendLake(const TERRAIN::DATA::PreparedSpline& spline, Ribbon& ribbon) const;

    uint64_t revision_ = 0u;
    std::vector<TERRAIN::DATA::SplineLayerSettings> built_{};
    std::vector<Ribbon> ribbons_{};
    std::vector<Section> visible_{};
    RenderSurfaceResourceId indexBuffer_ = 0u;
};

} // namespace HIKARI::TERRAINKIT
