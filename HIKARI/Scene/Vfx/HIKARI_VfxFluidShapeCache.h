#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "Render3D/Vfx/HIKARI_FluidShapeVolume.h"

namespace HIKARI {
class AssetRegistry;
class ModelManager;
} // namespace HIKARI

namespace HIKARI::VFX {

// 流体源の「模型の形」（FluidSourceSettings::shapeModel）が読む距離の体積を、模型の GUID ごとに焼いて持つ。
// 模型は既存の runtime loader で読み、最初に使われた時に 1 回だけ焼く（同じ模型の源と域は同じ体積を共有する）。
// 資産表が替わるか版が進んだら（模型を入れ直した）捨てて焼き直す。
class VfxFluidShapeCache final {
public:
    void Bind(const AssetRegistry* assetRegistry, ModelManager* modelManager) noexcept;
    // 模型が無い・読めない・三角形が無い時は nullptr（資産表の版が進むまで、同じ模型をもう一度読みに行かない）。
    std::shared_ptr<const RENDER3D::FLUID::FluidShapeVolume> Resolve(const std::string& modelGuid);
    void Clear() noexcept;

private:
    const AssetRegistry* assetRegistry_ = nullptr;
    ModelManager* modelManager_ = nullptr;
    uint64_t registryId_ = 0u;
    uint64_t registryRevision_ = 0u;
    std::unordered_map<std::string, std::shared_ptr<const RENDER3D::FLUID::FluidShapeVolume>> volumes_{};
};

} // namespace HIKARI::VFX
