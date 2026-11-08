#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "TerrainKit/Data/HIKARI_TerrainDataBrush.h"
#include "TerrainKit/Data/HIKARI_TerrainDataImage.h"

// 印章と層のマスク。印章は高さ画像を手彫り層へ一度だけ押す（入力は世界座標と設定だけなので、人と智能体で同じ）。
namespace HIKARI::TERRAIN::DATA {

enum class StampBlend : uint8_t {
    Add = 0, // 層の値へ足す
    Max = 1, // 層の値と大きい方を取る
    Min = 2, // 層の値と小さい方を取る（谷や窪みを刻む）
};

inline constexpr TEXT::EnumName<StampBlend> kStampBlendNames[] = {
    {StampBlend::Add, "add"},
    {StampBlend::Max, "max"},
    {StampBlend::Min, "min"},
};

struct StampSettings {
    MATH::Vec2 center{0.0f, 0.0f};
    float radiusMeters = 128.0f;
    float heightMeters = 60.0f; // 画像の値 1 の高さ
    float rotationDegrees = 0.0f;
    float edgeFalloff = 0.2f; // 半径に対する、縁で 0 へ落とす幅の割合
    StampBlend blend = StampBlend::Add;
    bool operator==(const StampSettings&) const = default;
};

inline constexpr auto kStampFields = [](auto&& field) {
    using S = StampSettings;
    using namespace JSONFIELDS;
    field("radiusMeters", &S::radiusMeters, Range{1.0, 8192.0}, Label{"Radius"}, Unit{"m"});
    field("heightMeters", &S::heightMeters, Range{-20000.0, 20000.0}, Label{"Height"}, Unit{"m"});
    field("rotationDegrees", &S::rotationDegrees, Range{-3600.0, 3600.0}, Label{"Rotation"}, Unit{"deg"});
    field("edgeFalloff", &S::edgeFalloff, Slider(0.0, 1.0), Label{"Edge Falloff"});
    field("blend", &S::blend, kStampBlendNames, Label{"Blend"});
};

// 引擎に頼らず作れる内蔵の印章の名前。
std::span<const std::string_view> BuiltinStampNames() noexcept;
// 内蔵の印章（正方形、値はおよそ -1..1）。名前が違えば false。
bool MakeBuiltinStamp(std::string_view name, uint32_t size, HeightImage& out);

bool ApplyStamp(TerrainData& data, uint32_t layerId, const HeightImage& image, const StampSettings& settings,
                TerrainEditRecorder* recorder, BrushEditResult& result, std::string& error);

enum class MaskBrushOp : uint8_t {
    Paint = 0, // 1 へ寄せる（層が効く）
    Erase = 1, // 0 へ寄せる（層が消える）
};

inline constexpr TEXT::EnumName<MaskBrushOp> kMaskBrushOpNames[] = {
    {MaskBrushOp::Paint, "paint"},
    {MaskBrushOp::Erase, "erase"},
};

struct MaskBrushSettings {
    MaskBrushOp op = MaskBrushOp::Erase;
    float radiusMeters = 32.0f;
    float strength = 0.5f;
    float hardness = 0.5f;
    float spacing = 0.25f;
    bool operator==(const MaskBrushSettings&) const = default;
};

inline constexpr auto kMaskBrushFields = [](auto&& field) {
    using S = MaskBrushSettings;
    using namespace JSONFIELDS;
    field("op", &S::op, kMaskBrushOpNames, Label{"Operation"});
    field("radiusMeters", &S::radiusMeters, Range{0.25, 4096.0}, Label{"Radius"}, Unit{"m"});
    field("strength", &S::strength, Slider(0.0, 1.0), Label{"Strength"});
    field("hardness", &S::hardness, Slider(0.0, 1.0), Label{"Hardness"});
    field("spacing", &S::spacing, Range{0.02, 4.0}, Label{"Spacing"});
};

// 層のマスクを塗る。層の masked が切れていれば入れる。
bool ApplyMaskBrush(TerrainData& data, uint32_t layerId, const MaskBrushSettings& settings,
                    std::span<const MATH::Vec2> path, TerrainEditRecorder* recorder, BrushEditResult& result,
                    std::string& error);

// 画像（値 0..1、画素 (0,0) が originMeters）を矩形の範囲で層のマスクへ写す。範囲の外は変えない。
bool ImportLayerMask(TerrainData& data, uint32_t layerId, const HeightImage& image, MATH::Vec2 originMeters,
                     MATH::Vec2 sizeMeters, TerrainEditRecorder* recorder, BrushEditResult& result, std::string& error);
// 矩形の層のマスクを画像で返す（書き出し用）。
bool ExportLayerMask(const TerrainData& data, uint32_t layerId, const SampleRect& rect, HeightImage& out,
                     std::string& error);

} // namespace HIKARI::TERRAIN::DATA
