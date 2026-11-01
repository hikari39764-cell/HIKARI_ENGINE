#include "Editor/ShaderPlayground/HIKARI_ShaderPlaygroundPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/ShaderPlayground/HIKARI_ShaderPlaygroundOperations.h"
#include "Editor/ShaderPlayground/HIKARI_ShaderPlaygroundRunner.h"
#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Project/HIKARI_ProjectContext.h"
#include "RenderCore/Texture/HIKARI_DxTexture.h"

namespace HIKARI::EDITOR {
namespace {
using Json = nlohmann::json;

struct ViewFile {
    const char* file;
    const char* labelKey;
};
constexpr ViewFile kViews[] = {{"sheet.png", "ui.shaderPlayground.viewA"},
                               {"sheet-b.png", "ui.shaderPlayground.viewB"},
                               {"diff.png", "ui.shaderPlayground.viewDiff"},
                               {"issues.png", "ui.shaderPlayground.viewIssues"},
                               {"diff-last.png", "ui.shaderPlayground.viewSinceLast"}};
constexpr double kWatchInterval = 0.5;

std::string FormatVector(const Json& values) {
    if (!values.is_array() || values.size() < 3u)
        return "-";
    char text[96];
    std::snprintf(text, sizeof(text), "%.3g %.3g %.3g %.3g", values[0].get<double>(), values[1].get<double>(),
                  values[2].get<double>(), values.size() > 3u ? values[3].get<double>() : 0.0);
    return text;
}

class ShaderPlaygroundTool final : public IEditorTool {
public:
    ShaderPlaygroundTool() { textures_.fill(-1); }
    ~ShaderPlaygroundTool() override { ReleaseTextures(); }

    void Draw(EditorToolContext& context, bool& open) override {
        if (GetLastShaderPlay().revision != shownRevision_)
            Show(GetLastShaderPlay());
        const std::string windowName = TEXT::LocalizeWindow("ui.window.shaderPlayground", "Shader Playground");
        if (context.openRequest)
            RevealEditorWindow(windowName.c_str());
        ImGui::SetNextWindowSize({820.0f, 680.0f}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(windowName.c_str(), &open)) {
            ImGui::End();
            return;
        }
        ImGui::SetNextItemWidth((std::max)(ImGui::GetContentRegionAvail().x - 220.0f, 120.0f));
        ImGui::InputTextWithHint("##spec", TEXT::Localize("ui.shaderPlayground.specHint").c_str(), spec_,
                                 sizeof(spec_));
        ImGui::SameLine();
        if (ImGui::Button(TEXT::Localize("ui.shaderPlayground.run").c_str()))
            context.operations.Execute("shader.play", Json{{"spec", std::string(spec_)}});
        ImGui::SameLine();
        ImGui::Checkbox(TEXT::Localize("ui.shaderPlayground.watch").c_str(), &watch_);
        if (watch_ && ImGui::GetTime() - lastPoll_ > kWatchInterval) {
            lastPoll_ = ImGui::GetTime();
            if (WatchedFilesChanged())
                context.operations.Execute("shader.play", Json{{"spec", std::string(spec_)}});
        }
        DrawStatus();
        DrawViews();
        DrawTiles();
        ImGui::End();
    }

private:
    // 実行の結果は操作が記録し、窓は記録が新しくなった時に読み直す（窓以外から実行した結果も同じに出る）。
    void Show(const ShaderPlayRecord& record) {
        shownRevision_ = record.revision;
        std::snprintf(spec_, sizeof(spec_), "%s", record.spec.c_str());
        failed_ = !record.ok;
        message_ = record.message;
        result_ = Json::object();
        RememberWatchedFiles();
        ReleaseTextures();
        raw_.clear();
        if (record.output.empty())
            return;
        const std::filesystem::path& output = record.output;
        if (!SERIALIZATION::JSON::ReadJsonFile(output / "result.json", result_) || !result_.is_object())
            result_ = Json::object();
        ++generation_;
        for (size_t index = 0u; index < std::size(kViews); ++index) {
            const std::filesystem::path file = output / kViews[index].file;
            std::error_code error;
            if (!std::filesystem::is_regular_file(file, error))
                continue;
            const std::string path = TEXT::PathToUtf8(file);
            DXTEX::DxTextureManager::InvalidateTextureCacheByPath(path);
            textures_[index] = DXTEX::DxTextureManager::LoadTextureLinear(
                "shaderPlayground/" + std::to_string(generation_) + "/" + kViews[index].file, path);
        }
        if (!PLAYGROUND::ReadRawResult(output, rawWidth_, rawHeight_, raw_))
            raw_.clear();
        if (textures_[static_cast<size_t>(view_)] < 0)
            view_ = 0;
    }

    // 設定・着色器・入力画像の更新時刻。設定が読めなければ設定のファイルだけを見張る。
    void RememberWatchedFiles() {
        watched_.clear();
        std::filesystem::path spec = TEXT::PathFromUtf8(spec_);
        if (spec.is_relative())
            spec = GetProjectContext().GetRoot() / spec;
        std::vector<std::filesystem::path> files{spec};
        PLAYGROUND::Spec parsed;
        std::string error;
        if (PLAYGROUND::ReadSpec(spec, parsed, error)) {
            for (const std::filesystem::path* file : {&parsed.source, &parsed.compare, &parsed.image})
                if (!file->empty())
                    files.push_back(parsed.directory / *file);
        }
        for (const std::filesystem::path& file : files) {
            std::error_code timeError;
            watched_.emplace_back(file, std::filesystem::last_write_time(file, timeError));
        }
    }

    bool WatchedFilesChanged() const {
        for (const auto& [file, time] : watched_) {
            std::error_code error;
            if (std::filesystem::last_write_time(file, error) != time)
                return true;
        }
        return false;
    }

    void ReleaseTextures() {
        for (int& texture : textures_) {
            if (texture >= 0)
                DXTEX::DxTextureManager::ReleaseTextureDeferred(texture);
            texture = -1;
        }
    }

    void DrawStatus() {
        if (message_.empty()) {
            ImGui::TextDisabled("%s", TEXT::Localize("ui.shaderPlayground.noResult").c_str());
            return;
        }
        const ImVec4 color = failed_ ? GetEditorThemePalette().error : GetEditorThemePalette().textMuted;
        ImGui::TextColored(color, "%s", message_.c_str());
        if (result_.contains("adapter"))
            ImGui::TextDisabled("%s", result_.value("adapter", std::string{}).c_str());
        for (const Json& program : result_.value("programs", Json::array())) {
            const std::string line = program.value("id", std::string{}) + "  " + program.value("source", std::string{});
            if (!program.value("compiled", false)) {
                ImGui::TextColored(GetEditorThemePalette().error, "%s", line.c_str());
                ImGui::TextWrapped("%s", program.value("diagnostics", std::string{}).c_str());
                continue;
            }
            const double gpu = program.value("gpuMilliseconds", -1.0);
            char timing[96];
            std::snprintf(timing, sizeof(timing), gpu >= 0.0 ? "  compile %.0f ms · GPU %.3f ms" : "  compile %.0f ms",
                          program.value("compileMilliseconds", 0.0), gpu);
            ImGui::TextDisabled("%s%s", line.c_str(), timing);
        }
        const Json issues = result_.value("issues", Json::object());
        char text[160];
        std::snprintf(text, sizeof(text), TEXT::Localize("ui.shaderPlayground.issuesFormat").c_str(),
                      issues.value("nonFinite", 0ull), issues.value("negative", 0ull), issues.value("overMax", 0ull));
        ImGui::TextColored(
            issues.value("nonFinite", 0ull) + issues.value("negative", 0ull) + issues.value("overMax", 0ull) > 0u
                ? GetEditorThemePalette().warning
                : GetEditorThemePalette().textMuted,
            "%s", text);
        // 差の画は変化が無いと真っ黒なので、数でも出す。
        for (const auto& [key, format] : {std::pair{"compare", "ui.shaderPlayground.compareFormat"},
                                          std::pair{"sinceLast", "ui.shaderPlayground.sinceLastFormat"}}) {
            const Json difference = result_.value(key, Json());
            if (!difference.is_object())
                continue;
            std::snprintf(text, sizeof(text), TEXT::Localize(format).c_str(), difference.value("changedPixels", 0ull),
                          difference.value("maxDifference", 0.0));
            ImGui::TextDisabled("%s", text);
        }
    }

    void DrawViews() {
        if (textures_[0] < 0)
            return;
        for (size_t index = 0u; index < std::size(kViews); ++index) {
            if (textures_[index] < 0)
                continue;
            ImGui::RadioButton(TEXT::Localize(kViews[index].labelKey).c_str(), &view_, static_cast<int>(index));
            ImGui::SameLine();
        }
        ImGui::Checkbox(TEXT::Localize("ui.shaderPlayground.sideBySide").c_str(), &sideBySide_);
        // 並べるときは左が A、右が選んだ画（A を選んでいれば B、無ければ問題の画）。
        int right = view_;
        if (sideBySide_ && view_ == 0)
            right = textures_[1] >= 0 ? 1 : textures_[3] >= 0 ? 3 : -1;
        const bool pair = sideBySide_ && right > 0;
        const float width = pair ? (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f
                                 : ImGui::GetContentRegionAvail().x;
        DrawImage(pair ? 0 : view_, width);
        if (pair) {
            ImGui::SameLine();
            DrawImage(right, width);
        }
    }

    void DrawImage(int view, float width) {
        const int texture = textures_[static_cast<size_t>(view)];
        UINT textureWidth = 0u, textureHeight = 0u;
        DXTEX::DxTextureManager::GetTextureSize(texture, textureWidth, textureHeight);
        const D3D12_GPU_DESCRIPTOR_HANDLE srv = DXTEX::DxTextureManager::GetSrvGpuHandle(texture);
        if (srv.ptr == 0u || textureWidth == 0u)
            return;
        const float scale = (std::min)(width / static_cast<float>(textureWidth), 2.0f);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr)),
                     {textureWidth * scale, textureHeight * scale});
        if (ImGui::IsItemHovered() && !raw_.empty())
            DrawReadout(static_cast<uint32_t>((ImGui::GetMousePos().x - origin.x) / scale),
                        static_cast<uint32_t>((ImGui::GetMousePos().y - origin.y) / scale));
    }

    // 指した画素の生の値（A）と、そのタイルの名前。
    void DrawReadout(uint32_t x, uint32_t y) const {
        if (x >= rawWidth_ || y >= rawHeight_ || result_.value("programs", Json::array()).empty())
            return;
        const float* value = raw_.data() + (static_cast<size_t>(y) * rawWidth_ + x) * 4u;
        std::string label;
        for (const Json& tile : result_["programs"][0].value("tiles", Json::array())) {
            const Json& rect = tile["rect"];
            const uint32_t left = rect[0].get<uint32_t>(), top = rect[1].get<uint32_t>(),
                           size = rect[2].get<uint32_t>();
            if (x >= left && y >= top && x < left + size && y < top + size) {
                label = tile.value("label", std::string{});
                x -= left;
                y -= top;
                break;
            }
        }
        ImGui::BeginTooltip();
        if (!label.empty())
            ImGui::TextUnformatted(label.c_str());
        ImGui::Text("(%u, %u)  %.4g  %.4g  %.4g  %.4g", x, y, value[0], value[1], value[2], value[3]);
        ImGui::EndTooltip();
    }

    void DrawTiles() const {
        const Json programs = result_.value("programs", Json::array());
        if (programs.empty() || !programs[0].contains("tiles") ||
            !ImGui::CollapsingHeader(TEXT::Localize("ui.shaderPlayground.tiles").c_str()))
            return;
        if (!ImGui::BeginTable("tiles", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable))
            return;
        for (const char* header : {"tile", "mean", "min", "max", "NaN/-/max"})
            ImGui::TableSetupColumn(header);
        ImGui::TableHeadersRow();
        for (const Json& tile : programs[0]["tiles"]) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(tile.value("label", std::string{}).c_str());
            for (const char* key : {"mean", "min", "max"}) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(FormatVector(tile.value(key, Json())).c_str());
            }
            ImGui::TableNextColumn();
            ImGui::Text("%llu / %llu / %llu", tile.value("nonFinite", 0ull), tile.value("negative", 0ull),
                        tile.value("overMax", 0ull));
        }
        ImGui::EndTable();
    }

    char spec_[512]{};
    bool watch_ = false;
    double lastPoll_ = 0.0;
    std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> watched_;
    Json result_ = Json::object();
    std::string message_;
    bool failed_ = false;
    std::vector<float> raw_;
    uint32_t rawWidth_ = 0u;
    uint32_t rawHeight_ = 0u;
    std::array<int, std::size(kViews)> textures_{};
    int view_ = 0;
    bool sideBySide_ = false;
    uint32_t generation_ = 0u;
    uint64_t shownRevision_ = 0u;
};

} // namespace

std::unique_ptr<IEditorTool> CreateShaderPlaygroundTool() { return std::make_unique<ShaderPlaygroundTool>(); }

} // namespace HIKARI::EDITOR
