#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <DirectXTex.h>
#include <combaseapi.h>

#include "HIKARI_TerrainKitWorkspacePages.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;
using namespace TERRAIN::DATA;

constexpr char kProjectBrushDirectory[] = "Assets/TerrainBrushes";
constexpr uint32_t kShapeSize = 128u;
constexpr uint32_t kThumbnailSize = 64u;
constexpr float kShapeRowHeight = 36.0f;

float Hash2(int x, int y) noexcept {
    uint32_t value = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    value = (value ^ (value >> 13u)) * 1274126177u;
    return static_cast<float>((value ^ (value >> 16u)) & 0xFFFFFFu) / 16777215.0f;
}

// 値の噪声（周期なしで十分）。
float ValueNoise(float x, float y) noexcept {
    const int cellX = static_cast<int>(std::floor(x));
    const int cellY = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(cellX);
    const float fy = y - static_cast<float>(cellY);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float top = Hash2(cellX, cellY) + (Hash2(cellX + 1, cellY) - Hash2(cellX, cellY)) * sx;
    const float bottom = Hash2(cellX, cellY + 1) + (Hash2(cellX + 1, cellY + 1) - Hash2(cellX, cellY + 1)) * sx;
    return top + (bottom - top) * sy;
}

float Smooth(float low, float high, float value) noexcept {
    const float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// 内蔵の形：丸以外の自然な塗りの三種。
BrushShapeEntry MakeBuiltin(const char* name, float (*function)(float, float)) {
    BrushShapeEntry entry{};
    entry.name = name;
    entry.shape.size = kShapeSize;
    entry.shape.values.resize(static_cast<size_t>(kShapeSize) * kShapeSize);
    for (uint32_t y = 0u; y < kShapeSize; ++y) {
        for (uint32_t x = 0u; x < kShapeSize; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kShapeSize) * 2.0f - 1.0f;
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kShapeSize) * 2.0f - 1.0f;
            entry.shape.values[static_cast<size_t>(y) * kShapeSize + x] = function(u, v);
        }
    }
    return entry;
}

// 大きな塊がまばらに残る（草むらの点在に向く）。
float Patches(float u, float v) noexcept {
    const float noise = ValueNoise(u * 3.0f + 7.0f, v * 3.0f + 3.0f) * 0.65f + ValueNoise(u * 7.0f, v * 7.0f) * 0.35f;
    return Smooth(0.46f, 0.62f, noise);
}

// 細かい粒が散る（砂利や花の混じりに向く）。
float Speckle(float u, float v) noexcept { return Smooth(0.58f, 0.7f, ValueNoise(u * 16.0f, v * 16.0f)); }

// 一方向に流れる筋（風で倒れた草の帯や、流れの跡に向く）。
float Streaks(float u, float v) noexcept { return Smooth(0.45f, 0.65f, ValueNoise(u * 2.0f + 11.0f, v * 12.0f)); }

void MakeThumbnail(BrushShapeEntry& entry) {
    entry.thumbnail.resize(static_cast<size_t>(kThumbnailSize) * kThumbnailSize * 4u);
    for (uint32_t y = 0u; y < kThumbnailSize; ++y) {
        for (uint32_t x = 0u; x < kThumbnailSize; ++x) {
            float value = 1.0f;
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kThumbnailSize) * 2.0f - 1.0f;
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kThumbnailSize) * 2.0f - 1.0f;
            if (entry.shape.size > 1u) {
                const uint32_t column = x * entry.shape.size / kThumbnailSize;
                const uint32_t row = y * entry.shape.size / kThumbnailSize;
                value = entry.shape.values[static_cast<size_t>(row) * entry.shape.size + column];
            }
            // 筆は丸く切れるので絵も丸く見せる。
            value *= 1.0f - Smooth(0.85f, 1.0f, std::sqrt(u * u + v * v));
            const auto shade = static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 235.0f + 20.0f);
            uint8_t* pixel = entry.thumbnail.data() + (static_cast<size_t>(y) * kThumbnailSize + x) * 4u;
            pixel[0] = shade;
            pixel[1] = shade;
            pixel[2] = shade;
            pixel[3] = 255u;
        }
    }
}

// project の画像を読む。明るいほど強く効く。
bool LoadShapeImage(const std::filesystem::path& file, BrushShapeEntry& entry) {
    DirectX::ScratchImage image{};
    if (FAILED(DirectX::LoadFromWICFile(file.c_str(), DirectX::WIC_FLAGS_IGNORE_SRGB, nullptr, image))) {
        return false;
    }
    DirectX::ScratchImage converted{};
    if (FAILED(DirectX::Convert(*image.GetImage(0u, 0u, 0u), DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT,
                                DirectX::TEX_THRESHOLD_DEFAULT, converted))) {
        return false;
    }
    DirectX::ScratchImage resized{};
    if (FAILED(DirectX::Resize(*converted.GetImage(0u, 0u, 0u), kShapeSize, kShapeSize, DirectX::TEX_FILTER_LINEAR,
                               resized))) {
        return false;
    }
    const DirectX::Image* pixels = resized.GetImage(0u, 0u, 0u);
    entry.name = file.stem().string();
    entry.shape.size = kShapeSize;
    entry.shape.values.resize(static_cast<size_t>(kShapeSize) * kShapeSize);
    for (uint32_t y = 0u; y < kShapeSize; ++y) {
        const uint8_t* row = pixels->pixels + y * pixels->rowPitch;
        for (uint32_t x = 0u; x < kShapeSize; ++x) {
            entry.shape.values[static_cast<size_t>(y) * kShapeSize + x] = static_cast<float>(row[x * 4u]) / 255.0f;
        }
    }
    return true;
}

struct ShapeCache {
    std::filesystem::path directory{};
    std::filesystem::file_time_type directoryTime{};
    std::vector<BrushShapeEntry> entries{};
};

ShapeCache& Cache() {
    static ShapeCache cache{};
    return cache;
}

} // namespace

const std::vector<BrushShapeEntry>& ListBrushShapes(const TerrainStore& store) {
    ShapeCache& cache = Cache();
    const std::filesystem::path directory = store.ProjectRoot() / kProjectBrushDirectory;
    std::error_code ec{};
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(directory, ec);
    if (!cache.entries.empty() && cache.directory == directory && cache.directoryTime == time) {
        return cache.entries;
    }
    cache.directory = directory;
    cache.directoryTime = time;
    cache.entries.clear();
    BrushShapeEntry round{};
    round.name = "Round";
    cache.entries.push_back(std::move(round));
    cache.entries.push_back(MakeBuiltin("Patches", Patches));
    cache.entries.push_back(MakeBuiltin("Speckle", Speckle));
    cache.entries.push_back(MakeBuiltin("Streaks", Streaks));
    for (const auto& file : std::filesystem::directory_iterator(directory, ec)) {
        BrushShapeEntry entry{};
        if (file.is_regular_file(ec) && LoadShapeImage(file.path(), entry)) {
            cache.entries.push_back(std::move(entry));
        }
    }
    for (BrushShapeEntry& entry : cache.entries) {
        MakeThumbnail(entry);
    }
    return cache.entries;
}

const BrushShape* CurrentBrushShape(const EditorShared& shared) {
    if (shared.brushShape.empty()) {
        return nullptr;
    }
    for (const BrushShapeEntry& entry : Cache().entries) {
        if (entry.name == shared.brushShape && entry.shape.size != 0u) {
            return &entry.shape;
        }
    }
    return nullptr;
}

void DrawBrushShapes(IEditorWorkspaceUi& ui, const TerrainStore& store, EditorShared& shared) {
    const std::vector<BrushShapeEntry>& shapes = ListBrushShapes(store);
    for (size_t index = 0u; index < shapes.size(); ++index) {
        const BrushShapeEntry& entry = shapes[index];
        const bool selected = index == 0u ? shared.brushShape.empty() : shared.brushShape == entry.name;
        if (ui.ImageSelectable("brushShape" + entry.name, entry.name, entry.thumbnail.data(), kThumbnailSize,
                               kThumbnailSize, index + 1u, selected, kShapeRowHeight)) {
            shared.brushShape = index == 0u ? std::string{} : entry.name;
        }
    }
    ui.Tooltip("Add your own: put a grayscale image in Assets/TerrainBrushes. Brighter paints stronger.");
}

} // namespace HIKARI::TERRAINKIT
