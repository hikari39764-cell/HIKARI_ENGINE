#include "Scene/Vfx/HIKARI_AfterimageSnapshot.h"

#include <algorithm>
#include "Render3D/Material/HIKARI_MaterialSnapshot.h"

namespace HIKARI::VFX::AFTERIMAGE {
namespace {
using namespace RENDER3D;
using namespace RENDER3D::MATERIAL;

template <class T>
bool Admit(std::vector<std::shared_ptr<T>>& cache, size_t limit) {
    if (cache.size() < limit)
        return true;
    std::erase_if(cache, [](const auto& item) { return item.use_count() == 1; });
    return cache.size() < limit;
}

bool SameMaterials(const Materials& cached, const RUNTIME::SceneRenderObjectDesc& source) {
    if (cached.source != source.model || cached.revision != source.model->sourceRevision ||
        cached.keys.size() != source.materialOverrides.size())
        return false;
    for (size_t i = 0; i < cached.keys.size(); ++i) {
        const auto& a = cached.keys[i];
        const auto& b = source.materialOverrides[i];
        if (a.materialIndex != b.materialIndex || a.material != b.material || a.materialRevision != b.materialRevision)
            return false;
    }
    return true;
}

bool PinTexture(const TextureSnapshotSource& source, Materials& owner, RuntimeTextureSlot& slot) {
    if (!source.enabled) {
        slot = {};
        return true;
    }
    auto handle = source.resource;
    if (!handle && !source.load.path.empty())
        handle = LoadTextureResourceWithColorSpace(source.load.name, source.load.path, source.load.colorSpace);
    auto* native = GetTextureResourceNative(handle);
    if (!native || GetTextureResourceDimension(handle) != TextureResourceDimension::Texture2D)
        return false;
    // A separate central-heap descriptor owns a reference to the immutable
    // resource. Hot reload may retire the source descriptor independently.
    RenderResourceDesc desc{};
    desc.debugName = "Afterimage.Coverage";
    auto pinned = RegisterTextureResourceFromNative(native, GetTextureResourceFormat(handle), std::move(desc));
    if (!pinned)
        return false;
    owner.textures.push_back(pinned);
    slot.resource = pinned;
    slot.handle = -1;
    slot.sourcePath.clear();
    slot.resolvedPath.clear();
    slot.enabled = true;
    return true;
}

bool CaptureMaterials(const RUNTIME::SceneRenderObjectDesc& source, Materials& out) {
    out.source = source.model;
    out.revision = source.model->sourceRevision;
    out.keys = source.materialOverrides;
    out.values.resize(source.model->materials.size());
    for (size_t i = 0; i < out.values.size(); ++i) {
        const auto found = std::find_if(source.materialOverrides.begin(), source.materialOverrides.end(),
                                        [i](const auto& value) { return value.materialIndex == i; });
        const Material* runtime = found != source.materialOverrides.end() ? found->material : nullptr;
        const auto& authored = source.model->materials[i];
        const auto snapshot = CaptureMaterialSnapshot(source.model, &authored, runtime);
        if (!snapshot.error.empty())
            return false;
        auto& result = out.values[i];
        if (runtime)
            result = *runtime;
        else {
            result.SetBaseColor(authored.baseColorFactor);
            result.SetAlphaCutoff(authored.alphaCutoff);
            result.SetDoubleSided(MATERIAL_POLICY::ShouldRenderDoubleSided(authored));
            result.SetFeatureBits(snapshot.projection.materialFlags);
        }
        result.SetMaterialParameterProjection({});
        result.SetShaderProfileId("PBR");
        result.SetInheritModelTextures(false);
        result.SetShellSettings({});
        auto flags = result.GetMaterialProgramInstanceFlags() | GPU_MATERIAL_PROGRAM_FLAGS::Afterimage;
        flags &= ~(GPU_MATERIAL_PROGRAM_FLAGS::RenderFeatureGeometryOutline |
                   GPU_MATERIAL_PROGRAM_FLAGS::RenderFeatureScreenOutline |
                   GPU_MATERIAL_PROGRAM_FLAGS::RenderFeatureVisibilityOverlay |
                   GPU_MATERIAL_PROGRAM_FLAGS::RenderFeatureFaceHairShadow |
                   GPU_MATERIAL_PROGRAM_FLAGS::EditorSelectionOutline);
        // AlphaMask is retained separately from transparent draw routing.
        if ((snapshot.projection.materialFlags & MATERIAL_FEATURES::AlphaMask) != 0u)
            flags |= GPU_MATERIAL_PROGRAM_FLAGS::AlphaMask;
        if ((snapshot.projection.materialFlags & MATERIAL_FEATURES::ThinTransparentSurface) != 0u)
            flags |= GPU_MATERIAL_PROGRAM_FLAGS::AlphaBlend;
        result.SetMaterialProgramInstanceFlags(flags);
        result.SetMaterialAlphaMode(RuntimeMaterialAlphaMode::Blend);
        result.SetUnlit(true);
        for (size_t channel = 0; channel < kGpuMaterialTextureUsages.size(); ++channel) {
            RuntimeTextureSlot slot{};
            const auto& uv = snapshot.projection.textures[channel];
            slot.texCoord = static_cast<int>(uv.uvSet);
            slot.uvScale = {uv.uvTransform.x, uv.uvTransform.y};
            slot.uvOffset = {uv.uvTransform.z, uv.uvTransform.w};
            slot.uvRotation = uv.uvRotation;
            if (!PinTexture(snapshot.textures[channel], out, slot))
                return false;
            result.SetTextureSlot(kGpuMaterialTextureUsages[channel], std::move(slot));
        }
        if (runtime) {
            const auto original = runtime->GetMaterialProgramResourceBlock();
            std::vector<RuntimeTextureSlot> slots(original.begin(), original.end());
            for (size_t index = 0; index < slots.size(); ++index)
                if (!PinTexture(snapshot.programTextures[index], out, slots[index]))
                    return false;
            result.SetMaterialProgramResourceBlock(std::move(slots));
        }
    }
    return true;
}
} // namespace

Materials::~Materials() {
    for (const auto texture : textures)
        RENDER3D::ReleaseTextureResource(texture); // deferred to submission fence
}

bool SnapshotCache::Capture(const RENDER3D::RUNTIME::SceneRenderObjectDesc& source, Part& out) {
    if (!source.visible || !source.model || !source.renderModel || !source.renderModel->valid)
        return false;
    if (source.model->HasSkinnedMesh() && (!source.animationPose || !source.animationPose->valid ||
                                           !source.animationPose->localPose.IsValidFor(source.model->nodes.size())))
        return false;
    auto geometry = std::find_if(geometry_.begin(), geometry_.end(), [&](const auto& entry) {
        return entry->source == source.model && entry->revision == source.model->sourceRevision;
    });
    std::shared_ptr<Geometry> frozen;
    if (geometry != geometry_.end())
        frozen = *geometry;
    else {
        if (!Admit(geometry_, 32u))
            return false;
        frozen = std::make_shared<Geometry>();
        frozen->source = source.model;
        frozen->revision = source.model->sourceRevision;
        frozen->model = *source.model;
        frozen->model.animations.clear();
        if (!RENDER3D::RUNTIME::BuildRenderModelAsset(frozen->model, frozen->renderModel))
            return false;
        geometry_.push_back(frozen);
    }
    auto material = std::find_if(materials_.begin(), materials_.end(),
                                 [&](const auto& item) { return SameMaterials(*item, source); });
    std::shared_ptr<Materials> frozenMaterials;
    if (material != materials_.end())
        frozenMaterials = *material;
    else {
        if (!Admit(materials_, 64u))
            return false;
        frozenMaterials = std::make_shared<Materials>();
        if (!CaptureMaterials(source, *frozenMaterials))
            return false;
        materials_.push_back(frozenMaterials);
    }
    out.geometry = frozen;
    out.materials = frozenMaterials;
    out.desc = source;
    const auto worldMatrix = source.worldTransform.GetWorldMatrix();
    out.desc.worldTransform = {};
    out.desc.worldTransform.useExplicitMatrix = true;
    out.desc.worldTransform.explicitMatrix = worldMatrix;
    out.desc.model = &frozen->model;
    out.desc.renderModel = &frozen->renderModel;
    if (source.animationPose) {
        if (!out.pose)
            out.pose = std::make_shared<ANIMATION::AnimationPoseSnapshot>();
        *out.pose = *source.animationPose;
        out.desc.animationPose = out.pose;
    }
    out.desc.castShadow = false;
    out.desc.receiveShadow = false;
    out.desc.isStatic = false;
    out.desc.allowStaticCachedForward = false;
    out.desc.materialFxProfileId.clear();
    out.desc.materialFxValuesInitialized = false;
    out.desc.editorSelectionMaterialIndex = RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex;
    out.desc.materialOverrides.clear();
    out.styled.assign(frozenMaterials->values.begin(), frozenMaterials->values.end());
    out.desc.materialOverrides.reserve(out.styled.size());
    return true;
}

void ApplyStyle(Part& part, const MATH::Vec3& color, float opacity) {
    part.desc.materialOverrides.clear();
    for (size_t index = 0; index < part.styled.size(); ++index) {
        auto& material = part.styled[index];
        material.SetEmissiveFactor(color);
        material.SetEmissiveStrength(opacity);
        part.desc.materialOverrides.push_back({static_cast<uint32_t>(index), &material, material.GetRevision()});
    }
}

void SnapshotCache::Clear() {
    materials_.clear();
    geometry_.clear();
}
} // namespace HIKARI::VFX::AFTERIMAGE
