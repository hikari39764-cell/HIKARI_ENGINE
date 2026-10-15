#include "Editor/Style/HIKARI_EditorFontManager.h"

#include <algorithm>
#include <array>
#include <filesystem>

#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {
ImGuiContext* gFontContext = nullptr;
ImFont* gRegularFont = nullptr;
ImFont* gStrongFont = nullptr;
ImFont* gMonospaceFont = nullptr;

// Japanese-first and Chinese-first font chains so the editor stays
// readable on Windows locales that only install one of them.
constexpr std::array<const char*, 4> kJapaneseFontCandidates{
    "C:/Windows/Fonts/YuGothM.ttc",
    "C:/Windows/Fonts/msgothic.ttc",
    "C:/Windows/Fonts/meiryo.ttc",
    "C:/Windows/Fonts/msyh.ttc",
};
constexpr std::array<const char*, 4> kChineseFontCandidates{
    "C:/Windows/Fonts/msyh.ttc",
    "C:/Windows/Fonts/msjh.ttc",
    "C:/Windows/Fonts/simhei.ttf",
    "C:/Windows/Fonts/simsun.ttc",
};

ImFont* AddFontIfPresent(ImFontAtlas& atlas, const char* path, float pixelSize, ImFontConfig* config = nullptr,
                         const ImWchar* ranges = nullptr) {
    if (!std::filesystem::exists(path)) {
        return nullptr;
    }
    return atlas.AddFontFromFileTTF(path, pixelSize, config, ranges);
}

void AddCombinedGlyphRanges(ImFontGlyphRangesBuilder& builder, ImFontAtlas& atlas) {
    builder.AddRanges(atlas.GetGlyphRangesDefault());
    builder.AddRanges(atlas.GetGlyphRangesJapanese());
    builder.AddRanges(atlas.GetGlyphRangesChineseFull());
    // 中日の範囲に無い記号。翻訳文の矢印（→ ←）と図形（● など）が欠字にならないようにする。
    static constexpr ImWchar kSymbolRanges[] = {0x2190, 0x21FF, 0x25A0, 0x25FF, 0};
    builder.AddRanges(kSymbolRanges);
}

void MergeCjkFallback(ImFontAtlas& atlas, float pixelSize, const ImWchar* ranges) {
    // Merge at most two complementary CJK fonts so Japanese kana and
    // Simplified Chinese ideographs are both covered without loading
    // every installed font into the atlas.
    const char* mergedPaths[2]{nullptr, nullptr};
    for (const char* candidate : kJapaneseFontCandidates) {
        if (std::filesystem::exists(candidate)) {
            mergedPaths[0] = candidate;
            break;
        }
    }
    for (const char* candidate : kChineseFontCandidates) {
        if (std::filesystem::exists(candidate) && candidate != mergedPaths[0]) {
            mergedPaths[1] = candidate;
            break;
        }
    }
    for (const char* path : mergedPaths) {
        if (path == nullptr) {
            continue;
        }
        ImFontConfig config{};
        config.MergeMode = true;
        config.PixelSnapH = true;
        config.OversampleH = 1;
        config.OversampleV = 1;
        (void)atlas.AddFontFromFileTTF(path, pixelSize, &config, ranges);
    }
}

bool HasCjkGlyph(const ImFont* font) { return font != nullptr && font->FindGlyphNoFallback(0x4E2D) != nullptr; }
} // namespace

bool EnsureEditorFonts(float uiScale) {
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (context == nullptr) {
        return false;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (gFontContext == context && gRegularFont != nullptr && io.Fonts != nullptr &&
        std::find(io.Fonts->Fonts.begin(), io.Fonts->Fonts.end(), gRegularFont) != io.Fonts->Fonts.end()) {
        return true;
    }

    gFontContext = context;
    gRegularFont = nullptr;
    gStrongFont = nullptr;
    gMonospaceFont = nullptr;

    if (io.Fonts == nullptr) {
        return false;
    }
    if (!io.Fonts->Fonts.empty()) {
        gRegularFont = io.FontDefault != nullptr ? io.FontDefault : io.Fonts->Fonts.front();
        if (HasCjkGlyph(gRegularFont)) {
            gStrongFont = gRegularFont;
            gMonospaceFont = gRegularFont;
            return true;
        }
        // A font was registered before us without CJK coverage (for
        // example the ImGui default font). Rebuild the atlas from scratch
        // so localized UI never falls back to question marks.
        io.Fonts->Clear();
    }

    const float scale = (std::clamp)(uiScale, 0.90f, 1.50f);
    const float regularSize = 16.0f * scale;
    const float monoSize = 15.0f * scale;
    ImFontGlyphRangesBuilder rangeBuilder{};
    AddCombinedGlyphRanges(rangeBuilder, *io.Fonts);
    ImVector<ImWchar> combinedRanges{};
    rangeBuilder.BuildRanges(&combinedRanges);
    const ImWchar* combinedRangePtr = combinedRanges.Data;

    ImFontConfig regularConfig{};
    regularConfig.OversampleH = 2;
    regularConfig.OversampleV = 1;
    regularConfig.PixelSnapH = false;
    gRegularFont =
        AddFontIfPresent(*io.Fonts, "C:/Windows/Fonts/segoeui.ttf", regularSize, &regularConfig, combinedRangePtr);
    if (gRegularFont == nullptr) {
        gRegularFont = io.Fonts->AddFontDefault();
    } else {
        MergeCjkFallback(*io.Fonts, regularSize, combinedRangePtr);
    }

    ImFontConfig strongConfig{};
    strongConfig.OversampleH = 2;
    strongConfig.OversampleV = 1;
    gStrongFont =
        AddFontIfPresent(*io.Fonts, "C:/Windows/Fonts/segoeuib.ttf", regularSize, &strongConfig, combinedRangePtr);
    if (gStrongFont != nullptr) {
        MergeCjkFallback(*io.Fonts, regularSize, combinedRangePtr);
    }

    ImFontConfig monoConfig{};
    monoConfig.OversampleH = 2;
    monoConfig.OversampleV = 1;
    gMonospaceFont =
        AddFontIfPresent(*io.Fonts, "C:/Windows/Fonts/consola.ttf", monoSize, &monoConfig, combinedRangePtr);
    if (gMonospaceFont != nullptr) {
        MergeCjkFallback(*io.Fonts, monoSize, combinedRangePtr);
    }

    if (gStrongFont == nullptr) {
        gStrongFont = gRegularFont;
    }
    if (gMonospaceFont == nullptr) {
        gMonospaceFont = gRegularFont;
    }
    io.FontDefault = gRegularFont;
    const bool built = io.Fonts->Build();
    if (built && !HasCjkGlyph(gRegularFont)) {
        // Last-resort fallback: rebuild with a CJK font as the primary
        // face so a missing Latin font never breaks localized output.
        io.Fonts->Clear();
        ImFontConfig cjkConfig{};
        cjkConfig.OversampleH = 2;
        cjkConfig.OversampleV = 1;
        for (const char* candidate : kChineseFontCandidates) {
            if (!std::filesystem::exists(candidate)) {
                continue;
            }
            gRegularFont = AddFontIfPresent(*io.Fonts, candidate, regularSize, &cjkConfig, combinedRangePtr);
            if (gRegularFont != nullptr) {
                break;
            }
        }
        if (gRegularFont == nullptr) {
            gRegularFont = io.Fonts->AddFontDefault();
        }
        gStrongFont = gRegularFont;
        gMonospaceFont = gRegularFont;
        io.FontDefault = gRegularFont;
        return io.Fonts->Build();
    }
    return built;
}

void ResetEditorFonts() noexcept {
    gFontContext = nullptr;
    gRegularFont = nullptr;
    gStrongFont = nullptr;
    gMonospaceFont = nullptr;
}

ImFont* GetEditorFont(EditorFontRole role) noexcept {
    switch (role) {
    case EditorFontRole::Strong: return gStrongFont;
    case EditorFontRole::Monospace: return gMonospaceFont;
    case EditorFontRole::Regular:
    default: return gRegularFont;
    }
}

bool PushEditorFont(EditorFontRole role) {
    ImFont* font = GetEditorFont(role);
    if (font == nullptr) {
        return false;
    }
    ImGui::PushFont(font);
    return true;
}

void PopEditorFont(bool pushed) {
    if (pushed) {
        ImGui::PopFont();
    }
}

} // namespace HIKARI::EDITOR
