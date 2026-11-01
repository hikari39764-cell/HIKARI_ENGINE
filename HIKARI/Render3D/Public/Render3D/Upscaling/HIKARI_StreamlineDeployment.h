#pragma once

#include <filesystem>
#include <string>

namespace HIKARI::RENDER3D::UPSCALING {

// フレーム生成用の Streamline plugin 一式と DLSS-G の NGX snippet は実行ファイル横のこのフォルダーに置く。Streamline は
// featuresToLoad に無い plugin も探索フォルダーにあれば読み込んで NGX 要件を問い合わせ、NGX も見つけた snippet を検証する
// ため、同じフォルダーだとフレーム生成を使わない起動で数秒かかる。Streamline は plugin を見つけた最初のフォルダーだけを
// 使うので、このフォルダーには common/dlss も複製する。名前は cmake/HikariDependencies.cmake と一致させる。
inline constexpr const char* kStreamlineFrameGenerationDirectory = "StreamlineFrameGeneration";

struct StreamlineDeploymentOptions {
    bool requireDlss = false;
    bool requireFrameGeneration = false;
};

bool DeployStreamlineRuntime(const std::filesystem::path& sourceDirectory, const std::filesystem::path& outputDirectory,
                             const StreamlineDeploymentOptions& options, std::string& errorMessage);

} // namespace HIKARI::RENDER3D::UPSCALING
