#pragma once

#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

inline constexpr uint32_t kGameProjectEnvironmentApiVersion = 1u;

// 地点の現地時刻（シーンの地点の UTC 時差が基準）。
struct GameProjectEnvironmentTime {
    int32_t year = 2026;
    uint32_t month = 6;
    uint32_t day = 21;
    // [0, 24)
    double localHours = 12.0;
};

// このフレームの環境の状態。方向はワールド空間の光源へ向かう単位ベクトル。
struct GameProjectEnvironmentState {
    uint32_t structSize = sizeof(GameProjectEnvironmentState);
    uint32_t apiVersion = kGameProjectEnvironmentApiVersion;
    GameProjectEnvironmentTime time{};
    // 1 で実時間、60 で実時間 1 分がゲーム内 1 時間。0 で時刻が止まる。
    double timeScale = 0.0;
    GameProjectFloat3 sunDirection{0.0f, 1.0f, 0.0f};
    // 大気外の法線照度 [lux]（地平線の下では 0）。
    float sunIlluminance = 0.0f;
    GameProjectFloat3 moonDirection{0.0f, -1.0f, 0.0f};
    float moonIlluminance = 0.0f;
    float sunAltitudeDegrees = 0.0f;
    // 0..1。地表の濡れ・水たまり・積雪（天気に時間をかけて追従する）。
    float surfaceWetness = 0.0f;
    float surfacePuddles = 0.0f;
    float surfaceSnowCover = 0.0f;
    // 0..1。天気の遷移の進み（1 で遷移先の天気）。
    float weatherTransition = 1.0f;
    // 0..1。落雷の閃光。
    float lightningFlash = 0.0f;
    // 影を落とす主光源が月か（太陽が十分沈んだ夜）。
    bool moonIsPrimary = false;
    uint8_t reserved[7]{};
};

static_assert(sizeof(GameProjectEnvironmentState) == 104u);
static_assert(std::is_trivially_copyable_v<GameProjectEnvironmentState>);

inline constexpr uint32_t kGameProjectEnvironmentSnapshotCapacity = 448u;

// 存档用の時刻・天気の状態（固定長）。同じエンジン版で保存したものだけ復元できる。
struct GameProjectEnvironmentSnapshot {
    uint32_t structSize = sizeof(GameProjectEnvironmentSnapshot);
    uint32_t layoutVersion = 0u;
    uint32_t byteCount = 0u;
    uint32_t reserved = 0u;
    uint8_t bytes[kGameProjectEnvironmentSnapshotCapacity]{};
};

static_assert(sizeof(GameProjectEnvironmentSnapshot) == 464u);
static_assert(std::is_trivially_copyable_v<GameProjectEnvironmentSnapshot>);

} // namespace HIKARI
