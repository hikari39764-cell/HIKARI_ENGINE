#ifndef HIKARI_SHADER_LIB_HLSLI
#define HIKARI_SHADER_LIB_HLSLI

// 着色器の共通関数ライブラリ。エンジンの着色器、プロジェクトの着色器、ShaderPlayground が同じものを使う。
// どれも引数だけで計算し、エンジンの定数バッファやテクスチャを読まない。
//
//   哈希と噪声  Include/Math/HIKARI_Noise.hlsli（周期付きの値・勾配・細胞噪声）、
//               Include/Post/HIKARI_PostHash.hlsli（HikariHash12、IGN）
//   有限値      Include/Math/HIKARI_FiniteMath.hlsli
//   PBR         Include/HIKARI_PbrCommon.hlsli（GGX、Fresnel、拡散と鏡面）
//   曲線        HIKARI_LibCurves.hlsli（remap、bias/gain、緩急、脈、量子化）
//   UV          HIKARI_LibUv.hlsli（回転、極座標、序列、ねじり）
//   形          HIKARI_LibShapes.hlsli（2D/3D の SDF と合わせ方）
//   色          HIKARI_LibColor.hlsli（sRGB、HSV、彩度とコントラスト、合成）
//   光          HIKARI_LibLighting.hlsli（Lambert、半 Lambert、包み込み、縁、卡通の段、MatCap）
//   抖動        HIKARI_LibDither.hlsli（Bayer）
//
// 新しく書く着色器はここを使う。地形（HikariTerrainHash21 / HikariTerrainValueNoise）、水面、絵画風の殻、
// 後処理の風格化（HikariStylizeValueNoise）、後処理の彩度（Rec.601 の HikariPostLuma / HikariApplySaturation）、
// 雲（範囲 0 を避ける HikariCloudRemap）にある似た関数は計算が少しずつ違い、合わせると既存の見た目が変わるので、
// それぞれの場所に残している。
#include "Include/Math/HIKARI_Noise.hlsli"
#include "Include/Math/HIKARI_FiniteMath.hlsli"
#include "Include/Post/HIKARI_PostHash.hlsli"
#include "Include/Lib/HIKARI_LibCurves.hlsli"
#include "Include/Lib/HIKARI_LibUv.hlsli"
#include "Include/Lib/HIKARI_LibShapes.hlsli"
#include "Include/Lib/HIKARI_LibColor.hlsli"
#include "Include/Lib/HIKARI_LibLighting.hlsli"
#include "Include/Lib/HIKARI_LibDither.hlsli"

#endif
