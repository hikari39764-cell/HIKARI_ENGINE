#pragma once

#include <memory>
#include <vector>
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::VFX::AFTERIMAGE {

// Geometry is copied once per source generation, shared by the bounded history.
// Runtime reloads cannot invalidate a captured primitive or bone hierarchy.
struct Geometry {
    const ModelAsset* source = nullptr;
    uint64_t revision = 0;
    ModelAsset model{};
    RENDER3D::RUNTIME::RenderModelAsset renderModel{};
};

struct Materials {
    const ModelAsset* source = nullptr;
    uint64_t revision = 0;
    std::vector<RENDER3D::RUNTIME::SceneRenderMaterialOverride> keys{};
    std::vector<Material> values{};
    std::vector<RENDER3D::TextureResourceHandle> textures{};
    ~Materials();
};

struct Part {
    std::shared_ptr<const Geometry> geometry{};
    std::shared_ptr<const Materials> materials{};
    RENDER3D::RUNTIME::SceneRenderObjectDesc desc{};
    std::shared_ptr<ANIMATION::AnimationPoseSnapshot> pose{};
    std::vector<Material> styled{};
};

class SnapshotCache {
public:
    bool Capture(const RENDER3D::RUNTIME::SceneRenderObjectDesc& source, Part& out);
    void Clear();

private:
    std::vector<std::shared_ptr<Geometry>> geometry_{};
    std::vector<std::shared_ptr<Materials>> materials_{};
};

void ApplyStyle(Part& part, const MATH::Vec3& color, float opacity);

} // namespace HIKARI::VFX::AFTERIMAGE
