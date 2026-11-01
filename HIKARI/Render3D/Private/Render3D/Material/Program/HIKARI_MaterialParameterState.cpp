#include "Render3D/Core/HIKARI_Material.h"

#include <cstring>
#include <utility>

namespace HIKARI {
namespace {

bool EqualParameterBlock(const std::vector<MATH::Vec4>& left, const std::vector<MATH::Vec4>& right) noexcept {
    return left.size() == right.size() &&
           (left.empty() || std::memcmp(left.data(), right.data(), left.size() * sizeof(MATH::Vec4)) == 0);
}

} // namespace

void Material::SetMaterialProgramParameterBlock(std::vector<MATH::Vec4> values) {
    materialParameterProjectionApplied_ = false;
    if (EqualParameterBlock(materialProgramParameterBlock_, values)) {
        return;
    }
    materialProgramParameterBlock_ = std::move(values);
    TouchRevision();
}

std::span<const MATH::Vec4> Material::GetMaterialProgramParameterBlock() const {
    return materialProgramParameterBlock_;
}

void Material::SetMaterialParameterProjection(
    std::shared_ptr<const RENDER3D::MATERIAL::MaterialParameterProjectionRecipe> recipe) {
    materialParameterProjection_ = std::move(recipe);
    materialParameterProjectionApplied_ = false;
    materialParameterProjectionInstanceId_ = 0u;
    materialParameterProjectionRevision_ = 0u;
}

const std::shared_ptr<const RENDER3D::MATERIAL::MaterialParameterProjectionRecipe>&
Material::GetMaterialParameterProjection() const noexcept {
    return materialParameterProjection_;
}

bool Material::NeedsMaterialParameterProjection(uint64_t sourceRevision) const noexcept {
    return NeedsMaterialParameterProjection(0u, sourceRevision);
}

bool Material::NeedsMaterialParameterProjection(uint64_t sourceInstanceId, uint64_t sourceRevision) const noexcept {
    return materialParameterProjection_ != nullptr &&
           (!materialParameterProjectionApplied_ || materialParameterProjectionInstanceId_ != sourceInstanceId ||
            materialParameterProjectionRevision_ != sourceRevision);
}

void Material::ApplyMaterialParameterProjection(std::vector<MATH::Vec4> values, uint64_t sourceRevision) {
    ApplyMaterialParameterProjection(std::move(values), 0u, sourceRevision);
}

void Material::ApplyMaterialParameterProjection(std::vector<MATH::Vec4> values, uint64_t sourceInstanceId,
                                                uint64_t sourceRevision) {
    materialParameterProjectionApplied_ = true;
    materialParameterProjectionInstanceId_ = sourceInstanceId;
    materialParameterProjectionRevision_ = sourceRevision;
    if (EqualParameterBlock(materialProgramParameterBlock_, values)) {
        return;
    }
    materialProgramParameterBlock_ = std::move(values);
    TouchRevision();
}

} // namespace HIKARI
