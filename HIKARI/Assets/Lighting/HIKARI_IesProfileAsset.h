#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// 配光（IES LM-63 の Type C）。灯具の下向き（鉛直角 0°）を光源のローカル +Z、水平角 0° をローカル +X に合わせる。
namespace HIKARI::ASSETS::LIGHTING {

// 描画が引く表の大きさ（鉛直 0..180° x 水平 0..360°）。
inline constexpr uint32_t kIesProfileVerticalSamples = 64u;
inline constexpr uint32_t kIesProfileHorizontalSamples = 32u;

struct IesProfileAsset {
    std::vector<float> verticalAngles;   // 度、昇順
    std::vector<float> horizontalAngles; // 度、昇順
    // horizontalAngles.size() x verticalAngles.size() の光度 [cd]（水平角ごとに鉛直の並び）。
    std::vector<float> candela;
    float maximumCandela = 0.0f;
};

// 描画が引く表。values は光度を最大値で割った 0..1 を鉛直が速い順に並べ（水平の対称性は展開済み）、peakToMean は
// 表の最大値と全球の平均の比（表を掛けた光源の全光束を元の光束へ揃える光度の倍率）。
struct IesProfileTable {
    std::vector<float> values;
    float peakToMean = 1.0f;
};

bool LoadIesProfileAsset(const std::filesystem::path& path, IesProfileAsset& out, std::string& error);
IesProfileTable SampleIesProfile(const IesProfileAsset& profile);

} // namespace HIKARI::ASSETS::LIGHTING
