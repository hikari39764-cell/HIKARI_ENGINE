#pragma once

#include <cstdint>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

// モジュールの水（地形の河や湖など）が一点で返す値。
struct GameProjectWaterSourceSample {
    float surfaceHeight = 0.0f;
    GameProjectFloat3 velocity{};
    // 水底の高さ。分からなければ hasBottom を 0 にする。
    float bottomHeight = 0.0f;
    uint32_t hasBottom = 0u;
};

// モジュールの水を引擎の水面の問い合わせへ足す接口。World に登録して使う（WorldView::AddWaterSource）。
// 所有スレッドからだけ呼ばれる。
class IWaterSource {
public:
    virtual ~IWaterSource() = default;
    // 水平位置 (x, z) に水があれば値を書いて true。
    virtual bool SampleWater(float x, float z, GameProjectWaterSourceSample& out) const = 0;
};

// 一点の水（引擎の水面とモジュールの水のうち、問い合わせの高さに一番近い水面）。
struct GameProjectWaterSample {
    uint32_t valid = 0u;
    float surfaceHeight = 0.0f;
    GameProjectFloat3 normal{0.0f, 1.0f, 0.0f};
    GameProjectFloat3 velocity{};
    // 水面からの深さ（水面より下が正、上なら 0）。
    float submergence = 0.0f;
    // 水面から水底までの深さ。分からなければ負。
    float columnDepth = -1.0f;
};

// 場面の海（相机を追う無限の水面）。
struct GameProjectOcean {
    float level = 0.0f;
    uint64_t sceneObjectId = 0u;
};

} // namespace HIKARI
