#include "HIKARI_TerrainKitRivers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "HIKARI_TerrainKitFrustum.h"
#include "HIKARI_TerrainKitStore.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace TERRAIN::DATA;

constexpr char kRiverProgram[] = "HIKARI.EngineShader.Render3D_TerrainKitRiver";
// 帯の横の分割（曲線の外側は幅をそのまま、内側は曲がりに合わせて縮む）。
constexpr uint32_t kAcrossCells = 4u;
// 帯の一区切りが曲線の何区切り分か（曲線の区切りは約 2 m）。
constexpr size_t kSectionStride = 2u;

struct RiverDrawConstants {
    float color[3]{};
    float clarity = 1.0f;
    float flowSpeed = 0.0f;
    float acrossCells = static_cast<float>(kAcrossCells);
    float reserved[2]{};
};

} // namespace

void TerrainRivers::Rebuild(const std::vector<SplineLayerSettings>& rivers) {
    ribbons_.clear();
    for (const SplineLayerSettings& settings : rivers) {
        const PreparedSpline spline = PrepareSpline(settings);
        if (spline.sections.size() < 2u) {
            continue;
        }
        // 帯の断面の列。両端は半幅だけ先へ伸ばし、河の丸い端まで水面を届かせる（岸に当たって消える）。
        std::vector<CrossSection> crosses{};
        const auto append = [&](const SplineSection& section, float offset) {
            const float half = section.halfWidth;
            CrossSection cross{};
            cross.x = section.x + section.tangentX * offset;
            cross.y = section.y;
            cross.z = section.z + section.tangentZ * offset;
            const float extent = half + RiverBankMargin(half);
            cross.left = extent;
            cross.right = extent;
            cross.tangentX = section.tangentX;
            cross.tangentZ = section.tangentZ;
            cross.along = section.along + offset;
            crosses.push_back(cross);
        };
        append(spline.sections.front(), -spline.sections.front().halfWidth);
        for (size_t index = 0u; index < spline.sections.size(); index += kSectionStride) {
            append(spline.sections[index], 0.0f);
        }
        append(spline.sections.back(), 0.0f);
        append(spline.sections.back(), spline.sections.back().halfWidth);
        // 内側の幅を曲がりの半径に収める（帯が折れ返って水が二重にならないように）。
        std::vector<CrossSection> limited = crosses;
        for (size_t index = 1u; index + 1u < crosses.size(); ++index) {
            const CrossSection& previous = crosses[index - 1u];
            const CrossSection& next = crosses[index + 1u];
            const float turn = std::atan2(previous.tangentX * next.tangentZ - previous.tangentZ * next.tangentX,
                                          previous.tangentX * next.tangentX + previous.tangentZ * next.tangentZ);
            const float run = next.along - previous.along;
            if (std::abs(turn) < 1.0e-4f || run <= 0.0f) {
                continue;
            }
            const float radius = run / std::abs(turn) * 0.92f;
            // 接線 (0, 1) から (1, 0) へ曲がると turn は負で、内側は right（+X）の方。
            for (size_t around = index >= 2u ? index - 2u : 0u; around < (std::min)(index + 3u, crosses.size());
                 ++around) {
                float& extent = turn < 0.0f ? limited[around].right : limited[around].left;
                extent = (std::min)(extent, radius);
            }
        }
        Ribbon ribbon{};
        ribbon.color = settings.waterColor;
        ribbon.clarity = settings.clarityMeters;
        ribbon.flowSpeed = settings.flowSpeed;
        ribbon.sections.reserve(limited.size());
        for (size_t index = 0u; index + 1u < limited.size(); ++index) {
            if (limited[index + 1u].along > limited[index].along) {
                ribbon.sections.push_back(Section{limited[index], limited[index + 1u]});
            }
        }
        ribbons_.push_back(std::move(ribbon));
    }
}

void TerrainRivers::Prepare(IRenderSurfaceResources& resources, const TerrainStore& store) {
    if (indexBuffer_ == 0u) {
        std::array<uint16_t, kAcrossCells * 6u> indices{};
        const uint16_t columns = kAcrossCells + 1u;
        for (uint16_t column = 0u; column < kAcrossCells; ++column) {
            const size_t at = static_cast<size_t>(column) * 6u;
            const auto a = column;
            const auto b = static_cast<uint16_t>(column + 1u);
            const auto c = static_cast<uint16_t>(column + columns);
            const auto d = static_cast<uint16_t>(column + columns + 1u);
            indices[at + 0u] = a;
            indices[at + 1u] = c;
            indices[at + 2u] = b;
            indices[at + 3u] = b;
            indices[at + 4u] = c;
            indices[at + 5u] = d;
        }
        indexBuffer_ = resources.CreateIndexBuffer(indices);
    }
    const TerrainData& data = store.Data();
    if (data.Revision() == revision_) {
        return;
    }
    revision_ = data.Revision();
    std::vector<SplineLayerSettings> rivers{};
    for (const HeightLayer& layer : data.Stack().layers) {
        if (layer.kind == HeightLayerKind::Spline && layer.enabled && layer.spline.kind == SplineKind::River) {
            rivers.push_back(layer.spline);
        }
    }
    if (rivers != built_) {
        Rebuild(rivers);
        built_ = std::move(rivers);
    }
}

void TerrainRivers::Record(const RenderSurfaceView& view, IRenderSurfaceCommands& commands) {
    if (!HasWork()) {
        return;
    }
    const FrustumPlanes frustum(view.viewProjection);
    for (const Ribbon& ribbon : ribbons_) {
        visible_.clear();
        for (const Section& section : ribbon.sections) {
            const float reach =
                (std::max)((std::max)(section.a.left, section.a.right), (std::max)(section.b.left, section.b.right));
            const float minimum[3]{(std::min)(section.a.x, section.b.x) - reach,
                                   (std::min)(section.a.y, section.b.y) - 0.5f,
                                   (std::min)(section.a.z, section.b.z) - reach};
            const float maximum[3]{(std::max)(section.a.x, section.b.x) + reach,
                                   (std::max)(section.a.y, section.b.y) + 0.5f,
                                   (std::max)(section.a.z, section.b.z) + reach};
            if (frustum.IntersectsBox(minimum, maximum)) {
                visible_.push_back(section);
            }
        }
        if (visible_.empty()) {
            continue;
        }
        RiverDrawConstants constants{};
        constants.color[0] = ribbon.color.x;
        constants.color[1] = ribbon.color.y;
        constants.color[2] = ribbon.color.z;
        constants.clarity = ribbon.clarity;
        constants.flowSpeed = ribbon.flowSpeed;
        RenderSurfaceDraw draw{};
        draw.programId = kRiverProgram;
        draw.indexBuffer = indexBuffer_;
        draw.indexCount = kAcrossCells * 6u;
        draw.instances = visible_.data();
        draw.instanceStride = sizeof(Section);
        draw.instanceCount = static_cast<uint32_t>(visible_.size());
        draw.constants = &constants;
        draw.constantBytes = sizeof(constants);
        (void)commands.Draw(draw);
    }
}

} // namespace HIKARI::TERRAINKIT
