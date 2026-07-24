#include "Render3D/Models/Runtime/HIKARI_ModelRuntimeResourceBuilder.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Models/Runtime/HIKARI_ModelTextureResolver.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "HIKARI_Services.h"

namespace HIKARI::RENDER3D::MODELS {

    namespace {
        RENDER3D::TextureResourceHandle ResolveReleaseResource(const RuntimeTextureSlot& slot) {
            if (slot.resource) {
                return slot.resource;
            }
            if (slot.handle >= 0) {
                return RENDER3D::RegisterTextureResourceFromBackendHandle(slot.handle);
            }
            return {};
        }

        void ReleaseTextureResourceOnce(
            RENDER3D::TextureResourceHandle resource,
            std::vector<RENDER3D::TextureResourceHandle>& releasedResources) {

            if (!resource) {
                return;
            }
            if (std::find(releasedResources.begin(), releasedResources.end(), resource) != releasedResources.end()) {
                return;
            }

            RENDER3D::ReleaseTextureResource(resource);
            releasedResources.push_back(resource);
        }

        void ReleaseRuntimeMaterialTextures(const Material* material) {
            if (!material) {
                return;
            }

            std::vector<RENDER3D::TextureResourceHandle> releasedResources{};
            // Model reload 譎ゅ↓蜿､縺・material slot 縺ｮ texture resource 繧・deferred release 縺ｸ貂｡縺吶・
            ReleaseTextureResourceOnce(ResolveReleaseResource(material->GetTextureSlot(MaterialTextureUsage::BaseColor)), releasedResources);
            ReleaseTextureResourceOnce(ResolveReleaseResource(material->GetTextureSlot(MaterialTextureUsage::Normal)), releasedResources);
            ReleaseTextureResourceOnce(ResolveReleaseResource(material->GetTextureSlot(MaterialTextureUsage::MetallicRoughness)), releasedResources);
            ReleaseTextureResourceOnce(ResolveReleaseResource(material->GetTextureSlot(MaterialTextureUsage::Occlusion)), releasedResources);
            ReleaseTextureResourceOnce(ResolveReleaseResource(material->GetTextureSlot(MaterialTextureUsage::Emissive)), releasedResources);
            ReleaseTextureResourceOnce(ResolveReleaseResource(material->GetTextureSlot(MaterialTextureUsage::Specular)), releasedResources);
            ReleaseTextureResourceOnce(ResolveReleaseResource(material->GetTextureSlot(MaterialTextureUsage::SpecularColor)), releasedResources);
        }
    } // namespace

    bool BuildModelRuntimeResources(
        ModelAsset& asset,
        const ModelTextureResolver& textureResolver) {
        std::vector<VertexStatic3D> legacyVertices;
        std::vector<uint32_t> legacyIndices;

        // HMODEL 縺ｯ CPU 繝・・繧ｿ繧剃ｿ晄戟縺励∝ｮ溯｡梧凾縺縺大ｾ捺擂縺ｮ Mesh/Material 縺ｸ讖区ｸ｡縺励☆繧九・
        for (const MeshAsset& meshAsset : asset.meshes) {
            for (const MeshPrimitive& primitive : meshAsset.primitives) {
                const uint32_t baseVertex = static_cast<uint32_t>(legacyVertices.size());

                if (!primitive.staticVertices.empty()) {
                    legacyVertices.reserve(legacyVertices.size() + primitive.staticVertices.size());
                    for (const Vertex3D& source : primitive.staticVertices) {
                        VertexStatic3D vertex{};
                        vertex.position = source.position;
                        vertex.normal = source.normal;
                        vertex.tangent = source.tangent;
                        vertex.u = source.uv0.x;
                        vertex.v = source.uv0.y;
                        vertex.uv1 = source.uv1;
                        legacyVertices.push_back(vertex);
                    }
                } else if (!primitive.skinnedVertices.empty()) {
                    legacyVertices.reserve(legacyVertices.size() + primitive.skinnedVertices.size());
                    for (const SkinnedVertex3D& source : primitive.skinnedVertices) {
                        VertexStatic3D vertex{};
                        vertex.position = source.position;
                        vertex.normal = source.normal;
                        vertex.tangent = source.tangent;
                        vertex.u = source.uv0.x;
                        vertex.v = source.uv0.y;
                        vertex.uv1 = source.uv1;
                        legacyVertices.push_back(vertex);
                    }
                }

                const uint32_t vertexCount = static_cast<uint32_t>(legacyVertices.size() - baseVertex);
                if (!primitive.indices.empty()) {
                    legacyIndices.reserve(legacyIndices.size() + primitive.indices.size());
                    for (uint32_t index : primitive.indices) {
                        if (index < vertexCount) {
                            legacyIndices.push_back(baseVertex + index);
                        }
                    }
                } else {
                    legacyIndices.reserve(legacyIndices.size() + vertexCount);
                    for (uint32_t index = 0; index < vertexCount; ++index) {
                        legacyIndices.push_back(baseVertex + index);
                    }
                }
            }
        }

        if (legacyVertices.empty() || legacyIndices.empty()) {
            return false;
        }

        auto mesh = std::make_unique<Mesh>();
        if (!mesh->CreateStatic(SERVICES::gCtx.device, legacyVertices, legacyIndices)) {
            return false;
        }

        auto material = std::make_unique<Material>();
        material->SetBaseColor({ 1.0f, 1.0f, 1.0f, 1.0f });

        if (!asset.materials.empty()) {
            const MaterialAsset& primaryMat = asset.materials.front();
            material->SetBaseColor(primaryMat.baseColorFactor);
            textureResolver.ApplyMaterial(asset, primaryMat, *material, asset.GetName() + "/runtime");
        }

        asset.SetLegacyRuntimeMesh(std::move(mesh));
        asset.SetLegacyRuntimeMaterial(std::move(material));
        return true;
    }


    void ReleaseModelRuntimeResources(ModelAsset& asset) {
        ReleaseRuntimeMaterialTextures(asset.GetLegacyRuntimeMaterial());
        asset.SetLegacyRuntimeMesh(nullptr);
        asset.SetLegacyRuntimeMaterial(nullptr);
    }

} // namespace HIKARI::RENDER3D::MODELS
