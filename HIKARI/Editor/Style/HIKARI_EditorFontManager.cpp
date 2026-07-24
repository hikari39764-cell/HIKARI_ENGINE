#include "Editor/Style/HIKARI_EditorFontManager.h"

#if defined(HIKARI_WITH_EDITOR)
#include <algorithm>
#include <filesystem>

#include "imgui.h"

namespace HIKARI::EDITOR {
    namespace {
        ImGuiContext* gFontContext = nullptr;
        ImFont* gRegularFont = nullptr;
        ImFont* gStrongFont = nullptr;
        ImFont* gMonospaceFont = nullptr;

        ImFont* AddFontIfPresent(
            ImFontAtlas& atlas,
            const char* path,
            float pixelSize,
            ImFontConfig* config = nullptr,
            const ImWchar* ranges = nullptr) {
            if (!std::filesystem::exists(path)) {
                return nullptr;
            }
            return atlas.AddFontFromFileTTF(
                path,
                pixelSize,
                config,
                ranges);
        }

        void MergeCjkFallback(
            ImFontAtlas& atlas,
            float pixelSize,
            const char* path,
            const ImWchar* ranges) {
            if (!std::filesystem::exists(path)) {
                return;
            }
            ImFontConfig config{};
            config.MergeMode = true;
            config.PixelSnapH = true;
            config.OversampleH = 1;
            config.OversampleV = 1;
            (void)atlas.AddFontFromFileTTF(
                path,
                pixelSize,
                &config,
                ranges);
        }
    }

    bool EnsureEditorFonts(float uiScale) {
        ImGuiContext* context = ImGui::GetCurrentContext();
        if (context == nullptr) {
            return false;
        }
        ImGuiIO& io = ImGui::GetIO();
        if (gFontContext == context &&
            gRegularFont != nullptr &&
            io.Fonts != nullptr &&
            std::find(
                io.Fonts->Fonts.begin(),
                io.Fonts->Fonts.end(),
                gRegularFont) != io.Fonts->Fonts.end()) {
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
            gRegularFont = io.FontDefault != nullptr
                ? io.FontDefault
                : io.Fonts->Fonts.front();
            gStrongFont = gRegularFont;
            gMonospaceFont = gRegularFont;
            return true;
        }

        const float scale = (std::clamp)(uiScale, 0.90f, 1.50f);
        const float regularSize = 16.0f * scale;
        const float monoSize = 15.0f * scale;

        ImFontConfig regularConfig{};
        regularConfig.OversampleH = 2;
        regularConfig.OversampleV = 1;
        regularConfig.PixelSnapH = false;
        gRegularFont = AddFontIfPresent(
            *io.Fonts,
            "C:/Windows/Fonts/segoeui.ttf",
            regularSize,
            &regularConfig,
            io.Fonts->GetGlyphRangesDefault());
        if (gRegularFont == nullptr) {
            gRegularFont = io.Fonts->AddFontDefault();
        } else {
            MergeCjkFallback(
                *io.Fonts,
                regularSize,
                "C:/Windows/Fonts/YuGothM.ttc",
                io.Fonts->GetGlyphRangesJapanese());
            MergeCjkFallback(
                *io.Fonts,
                regularSize,
                "C:/Windows/Fonts/msyh.ttc",
                io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        }

        ImFontConfig strongConfig{};
        strongConfig.OversampleH = 2;
        strongConfig.OversampleV = 1;
        gStrongFont = AddFontIfPresent(
            *io.Fonts,
            "C:/Windows/Fonts/segoeuib.ttf",
            regularSize,
            &strongConfig,
            io.Fonts->GetGlyphRangesDefault());

        ImFontConfig monoConfig{};
        monoConfig.OversampleH = 2;
        monoConfig.OversampleV = 1;
        gMonospaceFont = AddFontIfPresent(
            *io.Fonts,
            "C:/Windows/Fonts/consola.ttf",
            monoSize,
            &monoConfig,
            io.Fonts->GetGlyphRangesDefault());

        if (gStrongFont == nullptr) {
            gStrongFont = gRegularFont;
        }
        if (gMonospaceFont == nullptr) {
            gMonospaceFont = gRegularFont;
        }
        io.FontDefault = gRegularFont;
        return io.Fonts->Build();
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
#endif
