#include "Editor/ShaderPlayground/HIKARI_ShaderPlaygroundRunner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>

#include <DirectXTex.h>
#include <Windows.h>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Gfx/HIKARI_ShaderCompiler.h"

// 統計・画像・result.json の書き出しと、設定から書き出しまでの入口。
namespace HIKARI::EDITOR::PLAYGROUND {
namespace {
using Json = nlohmann::json;

constexpr char kBaselineMagic[4] = {'H', 'P', 'G', '1'};
constexpr const char* kBaselineFile = "last.f32";
// 前回の結果があれば上書きされるか消える書き出し。
constexpr const char* kOutputFiles[] = {"sheet.png", "sheet-b.png",   "issues.png",
                                        "diff.png",  "diff-last.png", "result.json"};

enum class Issue : uint8_t { None, NonFinite, Negative, OverMax };

struct Rect {
    uint32_t x, y, size;
};

Rect TileRect(const Spec& spec, uint32_t tile) {
    const uint32_t cell = spec.tileSize + spec.gap;
    return {(tile % spec.columns) * cell, (tile / spec.columns) * cell, spec.tileSize};
}

Issue Classify(const float* raw, float maxValue) {
    if (!std::isfinite(raw[0]) || !std::isfinite(raw[1]) || !std::isfinite(raw[2]) || !std::isfinite(raw[3]))
        return Issue::NonFinite;
    if (raw[0] < 0.0f || raw[1] < 0.0f || raw[2] < 0.0f)
        return Issue::Negative;
    if (maxValue > 0.0f && (raw[0] > maxValue || raw[1] > maxValue || raw[2] > maxValue))
        return Issue::OverMax;
    return Issue::None;
}

double Short(double value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.4g", value);
    return std::atof(text);
}

Json Vector(const double* values) {
    return Json::array({Short(values[0]), Short(values[1]), Short(values[2]), Short(values[3])});
}

uint8_t ToSrgb8(float linear) {
    const float v = std::isfinite(linear) ? std::clamp(linear, 0.0f, 1.0f) : 0.0f;
    const float encoded = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(std::lround(encoded * 255.0f));
}

void Heat(float t, uint8_t* out) {
    const float v = std::clamp(t, 0.0f, 1.0f);
    const float rgb[3] = {1.5f - std::abs(4.0f * v - 3.0f), 1.5f - std::abs(4.0f * v - 2.0f),
                          1.5f - std::abs(4.0f * v - 1.0f)};
    for (int channel = 0; channel < 3; ++channel)
        out[channel] = static_cast<uint8_t>(std::lround(std::clamp(rgb[channel], 0.0f, 1.0f) * 255.0f));
    out[3] = 255u;
}

bool WritePng(const std::filesystem::path& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba,
              std::string& error) {
    const DirectX::Image image{width,
                               height,
                               DXGI_FORMAT_R8G8B8A8_UNORM,
                               static_cast<size_t>(width) * 4u,
                               rgba.size(),
                               const_cast<uint8_t*>(rgba.data())};
    if (SUCCEEDED(DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_FORCE_SRGB,
                                         DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), path.c_str())))
        return true;
    error = "could not write " + TEXT::PathToUtf8(path);
    return false;
}

// 表示色を sRGB の PNG へ。値が有限でない画素は桃色で塗る。
std::vector<uint8_t> SheetPixels(const SheetRender& sheet, const ProgramRender& program) {
    std::vector<uint8_t> rgba(static_cast<size_t>(sheet.width) * sheet.height * 4u);
    for (size_t pixel = 0u; pixel < rgba.size() / 4u; ++pixel) {
        const float* raw = program.raw.data() + pixel * 4u;
        const float* shown = program.display.data() + pixel * 4u;
        const bool bad = shown[3] > 0.5f && Classify(raw, 0.0f) == Issue::NonFinite;
        for (int channel = 0; channel < 3; ++channel)
            rgba[pixel * 4u + channel] = bad ? (channel == 1 ? 0u : 255u) : ToSrgb8(shown[channel]);
        rgba[pixel * 4u + 3u] = 255u;
    }
    return rgba;
}

struct TileStats {
    uint64_t count = 0u;
    uint64_t issues[4]{};
    double sum[4]{};
    double minimum[4]{};
    double maximum[4]{};
};

Json DescribeProgram(const Spec& spec, const SheetRender& sheet, const ProgramRender& program,
                     std::vector<uint8_t>* issueImage, uint64_t (&totals)[4]) {
    Json result = {{"id", program.id}, {"source", program.source}, {"compiled", program.compiled}};
    if (!program.diagnostics.empty())
        result["diagnostics"] = program.diagnostics.substr(0u, 4000u);
    result["compileMilliseconds"] = Short(program.compileMilliseconds);
    if (!program.compiled)
        return result;
    result["dxilBytes"] = program.dxilBytes;
    if (program.gpuMilliseconds >= 0.0)
        result["gpuMilliseconds"] = Short(program.gpuMilliseconds);
    Json tiles = Json::array();
    for (uint32_t tile = 0u; tile < spec.tiles.size(); ++tile) {
        const Rect rect = TileRect(spec, tile);
        TileStats stats;
        std::fill(std::begin(stats.minimum), std::end(stats.minimum), std::numeric_limits<double>::infinity());
        std::fill(std::begin(stats.maximum), std::end(stats.maximum), -std::numeric_limits<double>::infinity());
        for (uint32_t y = rect.y; y < rect.y + rect.size; ++y) {
            for (uint32_t x = rect.x; x < rect.x + rect.size; ++x) {
                const size_t pixel = static_cast<size_t>(y) * sheet.width + x;
                if (program.display[pixel * 4u + 3u] <= 0.5f)
                    continue;
                const float* raw = program.raw.data() + pixel * 4u;
                const Issue issue = Classify(raw, spec.maxValue);
                ++stats.count;
                ++stats.issues[static_cast<size_t>(issue)];
                if (issueImage != nullptr && issue != Issue::None) {
                    static constexpr uint8_t kColors[4][3] = {{0, 0, 0}, {255, 0, 255}, {0, 220, 255}, {255, 150, 0}};
                    std::memcpy(issueImage->data() + pixel * 4u, kColors[static_cast<size_t>(issue)], 3u);
                }
                if (issue == Issue::NonFinite)
                    continue;
                for (int channel = 0; channel < 4; ++channel) {
                    stats.sum[channel] += raw[channel];
                    stats.minimum[channel] = (std::min)(stats.minimum[channel], static_cast<double>(raw[channel]));
                    stats.maximum[channel] = (std::max)(stats.maximum[channel], static_cast<double>(raw[channel]));
                }
            }
        }
        Json entry = {{"tile", tile},
                      {"label", spec.tiles[tile].label},
                      {"rect", Json::array({rect.x, rect.y, rect.size, rect.size})},
                      {"pixels", stats.count}};
        const uint64_t finite = stats.count - stats.issues[static_cast<size_t>(Issue::NonFinite)];
        if (finite > 0u) {
            double mean[4];
            for (int channel = 0; channel < 4; ++channel)
                mean[channel] = stats.sum[channel] / static_cast<double>(finite);
            entry["mean"] = Vector(mean);
            entry["min"] = Vector(stats.minimum);
            entry["max"] = Vector(stats.maximum);
        }
        static constexpr const char* kIssueNames[4] = {"", "nonFinite", "negative", "overMax"};
        for (size_t issue = 1u; issue < 4u; ++issue) {
            totals[issue] += stats.issues[issue];
            if (stats.issues[issue] > 0u)
                entry[kIssueNames[issue]] = stats.issues[issue];
        }
        tiles.push_back(std::move(entry));
    }
    result["tiles"] = std::move(tiles);
    return result;
}

// 二つの生の値の差（画素の rgb の差の最大）。被写体の画素だけを数え、diffImage に偽色で描く。
Json Difference(const Spec& spec, const SheetRender& sheet, const std::vector<float>& coverage,
                const std::vector<float>& a, const std::vector<float>& b, std::vector<uint8_t>& diffImage) {
    const size_t pixels = static_cast<size_t>(sheet.width) * sheet.height;
    std::vector<float> difference(pixels, 0.0f);
    double maximum = 0.0, sum = 0.0;
    uint64_t counted = 0u, changed = 0u;
    for (size_t pixel = 0u; pixel < pixels; ++pixel) {
        if (coverage[pixel * 4u + 3u] <= 0.5f)
            continue;
        float d = 0.0f;
        for (int channel = 0; channel < 3; ++channel) {
            const float delta = std::abs(a[pixel * 4u + channel] - b[pixel * 4u + channel]);
            d = std::isfinite(delta) ? (std::max)(d, delta) : std::numeric_limits<float>::infinity();
        }
        difference[pixel] = d;
        ++counted;
        changed += d > 1.0e-4f ? 1u : 0u;
        if (std::isfinite(d)) {
            maximum = (std::max)(maximum, static_cast<double>(d));
            sum += d;
        }
    }
    diffImage.assign(pixels * 4u, 0u);
    for (size_t pixel = 0u; pixel < pixels; ++pixel) {
        diffImage[pixel * 4u + 3u] = 255u;
        if (coverage[pixel * 4u + 3u] > 0.5f && difference[pixel] > 1.0e-4f)
            Heat(maximum > 0.0 ? static_cast<float>(difference[pixel] / maximum) : 1.0f, &diffImage[pixel * 4u]);
    }
    Json tiles = Json::array();
    for (uint32_t tile = 0u; tile < spec.tiles.size(); ++tile) {
        const Rect rect = TileRect(spec, tile);
        float tileMaximum = 0.0f;
        for (uint32_t y = rect.y; y < rect.y + rect.size; ++y)
            for (uint32_t x = rect.x; x < rect.x + rect.size; ++x)
                tileMaximum = (std::max)(tileMaximum, difference[static_cast<size_t>(y) * sheet.width + x]);
        tiles.push_back(std::isfinite(tileMaximum) ? Json(Short(tileMaximum)) : Json("nonFinite"));
    }
    return {{"maxDifference", Short(maximum)},
            {"meanDifference", Short(counted > 0u ? sum / static_cast<double>(counted) : 0.0)},
            {"changedPixels", changed},
            {"tileMaxDifference", std::move(tiles)}};
}

bool WriteBaseline(const std::filesystem::path& path, const SheetRender& sheet, const std::vector<float>& raw) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const uint32_t size[2] = {sheet.width, sheet.height};
    return file.write(kBaselineMagic, 4) && file.write(reinterpret_cast<const char*>(size), sizeof(size)) &&
           file.write(reinterpret_cast<const char*>(raw.data()),
                      static_cast<std::streamsize>(raw.size() * sizeof(float)));
}

} // namespace

bool WriteReport(const Spec& spec, const SheetRender& sheet, const std::filesystem::path& outputDirectory,
                 nlohmann::json& outSummary, std::string& outError) {
    std::error_code fileError;
    std::filesystem::create_directories(outputDirectory, fileError);
    if (fileError) {
        outError = "could not create " + TEXT::PathToUtf8(outputDirectory);
        return false;
    }
    for (const char* name : kOutputFiles)
        std::filesystem::remove(outputDirectory / name, fileError);

    Json summary = {{"name", spec.name},
                    {"output", TEXT::PathToUtf8(outputDirectory)},
                    {"adapter", sheet.adapter},
                    {"software", sheet.software},
                    {"scene", TEXT::EnumToName(kSceneNames, spec.scene)},
                    {"sheet",
                     {{"width", sheet.width},
                      {"height", sheet.height},
                      {"columns", spec.columns},
                      {"rows", sheet.rows},
                      {"tileSize", spec.tileSize},
                      {"gap", spec.gap}}}};
    if (spec.maxValue > 0.0f)
        summary["maxValue"] = spec.maxValue;
    Json files = Json::array();
    Json programs = Json::array();
    uint64_t totals[4]{};
    for (const ProgramRender& program : sheet.programs) {
        const bool primary = program.id == "a";
        std::vector<uint8_t> sheetPixels;
        std::vector<uint8_t> issueImage;
        if (program.compiled) {
            sheetPixels = SheetPixels(sheet, program);
            issueImage = sheetPixels;
            for (size_t index = 0u; index < issueImage.size(); index += 4u)
                for (size_t channel = 0u; channel < 3u; ++channel)
                    issueImage[index + channel] = static_cast<uint8_t>(issueImage[index + channel] / 3u);
        }
        uint64_t programTotals[4]{};
        programs.push_back(DescribeProgram(spec, sheet, program, primary ? &issueImage : nullptr, programTotals));
        if (!program.compiled)
            continue;
        const char* sheetName = primary ? "sheet.png" : "sheet-b.png";
        if (!WritePng(outputDirectory / sheetName, sheet.width, sheet.height, sheetPixels, outError))
            return false;
        files.push_back(sheetName);
        if (primary) {
            std::copy(std::begin(programTotals), std::end(programTotals), std::begin(totals));
            if (totals[1] + totals[2] + totals[3] > 0u) {
                if (!WritePng(outputDirectory / "issues.png", sheet.width, sheet.height, issueImage, outError))
                    return false;
                files.push_back("issues.png");
            }
        }
    }
    summary["programs"] = std::move(programs);
    summary["issues"] = {{"nonFinite", totals[1]}, {"negative", totals[2]}, {"overMax", totals[3]}};

    const ProgramRender& primary = sheet.programs.front();
    if (primary.compiled) {
        std::vector<uint8_t> diffImage;
        if (sheet.programs.size() > 1u && sheet.programs[1].compiled) {
            summary["compare"] =
                Difference(spec, sheet, primary.display, primary.raw, sheet.programs[1].raw, diffImage);
            if (!WritePng(outputDirectory / "diff.png", sheet.width, sheet.height, diffImage, outError))
                return false;
            files.push_back("diff.png");
        }
        const std::filesystem::path baselinePath = outputDirectory / kBaselineFile;
        std::vector<float> baseline;
        uint32_t baselineWidth = 0u, baselineHeight = 0u;
        if (ReadRawResult(outputDirectory, baselineWidth, baselineHeight, baseline) && baselineWidth == sheet.width &&
            baselineHeight == sheet.height) {
            summary["sinceLast"] = Difference(spec, sheet, primary.display, baseline, primary.raw, diffImage);
            if (!WritePng(outputDirectory / "diff-last.png", sheet.width, sheet.height, diffImage, outError))
                return false;
            files.push_back("diff-last.png");
        }
        if (!WriteBaseline(baselinePath, sheet, primary.raw)) {
            outError = "could not write " + TEXT::PathToUtf8(baselinePath);
            return false;
        }
    }
    files.push_back("result.json");
    summary["files"] = std::move(files);
    std::string message;
    if (!SERIALIZATION::JSON::WriteJsonFile(outputDirectory / "result.json", summary, &message)) {
        outError = "could not write result.json: " + message;
        return false;
    }
    outSummary = std::move(summary);
    outError.clear();
    return true;
}

bool ReadRawResult(const std::filesystem::path& outputDirectory, uint32_t& outWidth, uint32_t& outHeight,
                   std::vector<float>& outRaw) {
    std::ifstream file(outputDirectory / kBaselineFile, std::ios::binary);
    char magic[4]{};
    uint32_t size[2]{};
    if (!file.read(magic, 4) || std::memcmp(magic, kBaselineMagic, 4u) != 0 ||
        !file.read(reinterpret_cast<char*>(size), sizeof(size)) || size[0] == 0u || size[1] == 0u || size[0] > 8192u ||
        size[1] > 8192u)
        return false;
    outRaw.resize(static_cast<size_t>(size[0]) * size[1] * 4u);
    if (!file.read(reinterpret_cast<char*>(outRaw.data()), static_cast<std::streamsize>(outRaw.size() * sizeof(float))))
        return false;
    outWidth = size[0];
    outHeight = size[1];
    return true;
}

bool RunShaderPlayground(const std::filesystem::path& specPath, const std::filesystem::path& outputRoot,
                         nlohmann::json& outSummary, std::string& outError) {
    Spec spec;
    if (!ReadSpec(specPath, spec, outError))
        return false;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
        outError = "could not initialize COM for image files";
        return false;
    }
    SheetRender sheet;
    const bool written = RenderSheet(spec, GFX::ResolveShaderSourcePath(L"HIKARI/Shaders"), sheet, outError) &&
                         WriteReport(spec, sheet, outputRoot / TEXT::PathFromUtf8(spec.name), outSummary, outError);
    if (SUCCEEDED(com))
        CoUninitialize();
    if (!written)
        return false;
    for (const ProgramRender& program : sheet.programs) {
        if (!program.compiled) {
            outError = program.source + ": " + program.diagnostics;
            return false;
        }
    }
    return true;
}

} // namespace HIKARI::EDITOR::PLAYGROUND
