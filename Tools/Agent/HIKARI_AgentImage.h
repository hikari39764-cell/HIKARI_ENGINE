#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace HIKARI::AGENT_CLI {

// 智能体へ画像を渡す時の長辺。模型の画像の令牌は画素数で決まるので、確認に足りる大きさへ縮める。
inline constexpr uint32_t kModelImageLongSide = 768u;

// PNG を読み、長辺が longSide を超えていれば縮めた PNG を返す。縮められなければ元のまま、読めなければ空。
std::string ReadImageForModel(const std::filesystem::path& path, uint32_t longSide);

} // namespace HIKARI::AGENT_CLI
