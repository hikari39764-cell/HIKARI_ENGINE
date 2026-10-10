#pragma once
#include <filesystem>
#include <json.hpp>

namespace HIKARI::EDITOR::PROJECT_CODE {
// 現在の項目源码（Source・Tests・Tools・cmake と根の清单）を job/workspace に写し、構築の依頼（request.json）を書く。
// 原本と読み込み済みの模块には触れない。場面・World・编译器・模型はここに持たない。
nlohmann::json PrepareBuild(const std::filesystem::path& project, const std::filesystem::path& sdk,
                            const std::filesystem::path& job);
} // namespace HIKARI::EDITOR::PROJECT_CODE
