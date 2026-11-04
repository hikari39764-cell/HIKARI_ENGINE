#pragma once

#include <cstdint>
#include <filesystem>

#include <json.hpp>

namespace HIKARI {
class ModelAsset;
}

namespace HIKARI::ASSETS::MODELS::GLTF {

// 画像と材質を読む。material を指さない三角形 primitive があれば既定材質を末尾に足す。戻り値はそういう primitive
// に割り当てる材質番号。
uint32_t ReadMaterials(const std::filesystem::path& sourcePath, const nlohmann::json& root, ModelAsset& asset);

// primitive の material が読んだ材質を指せばその番号、指さなければ defaultMaterialIndex。
uint32_t PrimitiveMaterialIndex(const nlohmann::json& root, const nlohmann::json& primitive,
                                uint32_t defaultMaterialIndex);

} // namespace HIKARI::ASSETS::MODELS::GLTF
