#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <json.hpp>

#include "Editor/ShaderPlayground/HIKARI_ShaderPlaygroundSpec.h"

// ShaderPlayground：試験の設定（HIKARI_ShaderPlaygroundSpec.h）の着色器をエディタとは別の D3D12 デバイスで
// タイルの一覧に描き、値の統計・問題の画素・前回や比較対象との差を書き出す。編集器の操作 shader.play と
// コマンドラインの --shader-playground が同じ RunShaderPlayground を呼ぶ。
namespace HIKARI::EDITOR::PLAYGROUND {

// 一つの着色器（試す対象 a と比較対象 b）を描いた結果。画素は RGBA の float で行ごとに詰める。
struct ProgramRender {
    std::string id;
    std::string source;
    bool compiled = false;
    std::string diagnostics;
    std::vector<float> raw;     // Play の戻り値
    std::vector<float> display; // 背景に重ねた表示色、a は被写体の画素なら 1
    double compileMilliseconds = 0.0;
    uint64_t dxilBytes = 0u;
    double gpuMilliseconds = -1.0;
};

struct SheetRender {
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t rows = 0u;
    std::string adapter;
    bool software = false;
    std::vector<ProgramRender> programs;
};

// engineShaderRoot はエンジンの HIKARI/Shaders（Include/ を持つ所）。
bool RenderSheet(const Spec& spec, const std::filesystem::path& engineShaderRoot, SheetRender& outSheet,
                 std::string& outError);

// 統計・画像・result.json を outputDirectory へ書き、要約を返す（HIKARI_ShaderPlaygroundReport.cpp）。
bool WriteReport(const Spec& spec, const SheetRender& sheet, const std::filesystem::path& outputDirectory,
                 nlohmann::json& outSummary, std::string& outError);

// WriteReport が outputDirectory に残す A の生の値（last.f32、次回の「前回との差」と結果の窓の読み値に使う）。
bool ReadRawResult(const std::filesystem::path& outputDirectory, uint32_t& outWidth, uint32_t& outHeight,
                   std::vector<float>& outRaw);

// 設定を読み、描き、outputRoot/<name>/ へ書く。着色器の編訳の失敗も要約に入れて false を返す。
bool RunShaderPlayground(const std::filesystem::path& specPath, const std::filesystem::path& outputRoot,
                         nlohmann::json& outSummary, std::string& outError);

} // namespace HIKARI::EDITOR::PLAYGROUND
