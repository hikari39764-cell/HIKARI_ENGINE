#include "Scene/Document/Baking/HIKARI_LightingTransportSceneBuilder.h"

#include "Core/HIKARI_MathConfig.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "Assets/Lighting/HIKARI_LightmapSurfaceAnalysis.h"
#include "RenderContracts/Lighting/HIKARI_LightmapAtlasLayout.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Core/Math/HIKARI_NormalMatrix.h"
#include "Core/Math/HIKARI_TangentFrame.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_GpuMaterialProjection.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawRoute.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::DOCUMENT_SCENE_BAKING {

namespace TRANSPORT = RENDER3D::LIGHTING::TRANSPORT;

namespace {

using MaterialSnapshotIndices = std::unordered_map<const MaterialAsset*, std::unordered_map<const Material*, uint32_t>>;

struct ObjectBakePolicy {
    std::string name{};
    bool enabled = false;
    bool include = false;
    bool renderStatic = false;
};

struct PendingLightmapSurface {
    ASSETS::LIGHTING::LightmapSurfaceIdentity identity{};
    std::vector<uint32_t> triangleIndices{};
    float worldArea = 0.0f;
    float uvArea = 0.0f;
    MATH::Vec2 uvMin{1.0f, 1.0f};
    MATH::Vec2 uvMax{0.0f, 0.0f};
    uint32_t minimumInteriorWidth = ASSETS::LIGHTING::kSurfaceLightmapMinimumAuthoredUvExtent;
    uint32_t minimumInteriorHeight = ASSETS::LIGHTING::kSurfaceLightmapMinimumAuthoredUvExtent;
};

const SceneComponentData* FindModelComponent(const SceneObjectData& object) {
    const auto found =
        std::find_if(object.components.begin(), object.components.end(),
                     [](const SceneComponentData& component) { return component.type == "ModelComponent"; });
    return found == object.components.end() ? nullptr : &*found;
}

std::unordered_map<uint64_t, ObjectBakePolicy> BuildObjectPolicies(const SceneDocument& document) {
    std::unordered_map<uint64_t, ObjectBakePolicy> policies{};
    for (const SceneObjectData& object : document.objects) {
        const SceneComponentData* model = FindModelComponent(object);
        if (model == nullptr) {
            continue;
        }
        ObjectBakePolicy policy{};
        policy.name = object.name;
        policy.enabled = object.enabled && model->properties.value("visible", true);
        policy.include = model->properties.value("includeInLightingBake", true);
        policy.renderStatic = model->properties.value("renderStatic", false);
        policies[object.id.value] = std::move(policy);
    }
    return policies;
}

MATH::Vec3 TransformNormal(const MATH::Mat4& matrix, const MATH::Vec3& normal) {
    const MATH::Vec4 transformed = matrix.TransformPoint({normal.x, normal.y, normal.z, 0.0f});
    return MATH::Normalize({transformed.x, transformed.y, transformed.z});
}

ASSETS::LIGHTING::LightmapSurfaceAnalysis AppendSurfaceTriangles(const RENDER3D::RUNTIME::SceneSurfaceInstance& surface,
                                                                 TRANSPORT::LightingTransportScene& scene,
                                                                 MaterialSnapshotIndices& materialIndices,
                                                                 PendingLightmapSurface& pendingSurface) {
    if (surface.model == nullptr || surface.meshIndex >= surface.model->meshes.size()) {
        ASSETS::LIGHTING::LightmapSurfaceAnalysis analysis{};
        analysis.issueMask = static_cast<uint32_t>(ASSETS::LIGHTING::LightmapSurfaceIssue::MissingGeometry);
        return analysis;
    }
    const MeshAsset& mesh = surface.model->meshes[surface.meshIndex];
    if (surface.primitiveIndex >= mesh.primitives.size()) {
        ASSETS::LIGHTING::LightmapSurfaceAnalysis analysis{};
        analysis.issueMask = static_cast<uint32_t>(ASSETS::LIGHTING::LightmapSurfaceIssue::MissingGeometry);
        return analysis;
    }
    const MeshPrimitive& primitive = mesh.primitives[surface.primitiveIndex];
    if (primitive.lightmapUvLayout.HasGeneratedLayout()) {
        const auto preservePadding = [](uint32_t referenceExtent, uint32_t sourcePadding) {
            const uint64_t numerator =
                static_cast<uint64_t>(ASSETS::LIGHTING::kSurfaceLightmapMinimumChartPaddingTexels) * referenceExtent;
            return static_cast<uint32_t>((numerator + sourcePadding - 1u) / sourcePadding);
        };
        pendingSurface.minimumInteriorWidth =
            preservePadding(primitive.lightmapUvLayout.referenceWidth, primitive.lightmapUvLayout.chartPadding);
        pendingSurface.minimumInteriorHeight =
            preservePadding(primitive.lightmapUvLayout.referenceHeight, primitive.lightmapUvLayout.chartPadding);
    }

    const MaterialAsset* material = surface.materialIndex < surface.model->materials.size()
                                        ? &surface.model->materials[surface.materialIndex]
                                        : nullptr;
    const AlphaMode alphaMode = RENDER3D::MATERIAL::ResolveMaterialAlphaMode(material, surface.materialOverride);
    const ASSETS::LIGHTING::LightmapSurfaceAnalysis analysis =
        ASSETS::LIGHTING::AnalyzeLightmapSurface(primitive, alphaMode);
    // 切り抜き（葉、柵）は alpha test 付きで光線を遮り反射する。半透明（ガラス）は光を通すので入れない。
    if (analysis.HasIssue(ASSETS::LIGHTING::LightmapSurfaceIssue::UnsupportedVertexLayout) ||
        analysis.HasIssue(ASSETS::LIGHTING::LightmapSurfaceIssue::MorphTargetsUnsupported) ||
        analysis.HasIssue(ASSETS::LIGHTING::LightmapSurfaceIssue::MissingGeometry) ||
        analysis.HasIssue(ASSETS::LIGHTING::LightmapSurfaceIssue::BlendedSurface)) {
        return analysis;
    }
    const bool doubleSided = RENDER3D::RUNTIME::ResolveSurfaceDrawShaderRoute(surface.materialOverride, material,
                                                                              &primitive, surface.materialFxProfileId)
                                 .doubleSided;

    const MATH::Mat4 normalMatrix = MATH::BuildNormalMatrixFromWorld(surface.drawWorldMatrix);
    auto& overrides = materialIndices[material];
    const auto [binding, inserted] =
        overrides.try_emplace(surface.materialOverride, static_cast<uint32_t>(scene.materials.size()));
    if (inserted)
        scene.materials.push_back(
            RENDER3D::MATERIAL::CaptureMaterialSnapshot(surface.model, material, surface.materialOverride));
    for (size_t index = 0; index + 2u < primitive.indices.size(); index += 3u) {
        const uint32_t indices[3]{primitive.indices[index], primitive.indices[index + 1u],
                                  primitive.indices[index + 2u]};
        if (indices[0] >= primitive.staticVertices.size() || indices[1] >= primitive.staticVertices.size() ||
            indices[2] >= primitive.staticVertices.size()) {
            continue;
        }

        TRANSPORT::LightingTransportTriangle triangle{};
        triangle.objectId = surface.objectId.value;
        triangle.materialIndex = binding->second;
        triangle.doubleSided = doubleSided;
        for (uint32_t corner = 0u; corner < 3u; ++corner) {
            const Vertex3D& vertex = primitive.staticVertices[indices[corner]];
            triangle.position[corner] = MATH::TransformPoint(surface.drawWorldMatrix, vertex.position);
            triangle.normal[corner] = TransformNormal(normalMatrix, vertex.normal);
            triangle.lightmapUv[corner] = vertex.uv1;
            triangle.materialUv0[corner] = vertex.uv0;
            triangle.materialUv1[corner] = vertex.uv1;
            triangle.tangent[corner] = MATH::TransformSurfaceTangent(surface.drawWorldMatrix, vertex.tangent);
        }
        const uint32_t triangleIndex = static_cast<uint32_t>(scene.triangles.size());
        scene.triangles.push_back(std::move(triangle));
        if (analysis.IsEligible()) {
            pendingSurface.triangleIndices.push_back(triangleIndex);
            const TRANSPORT::LightingTransportTriangle& appended = scene.triangles.back();
            const MATH::Vec3 worldCross =
                MATH::Cross(appended.position[1] - appended.position[0], appended.position[2] - appended.position[0]);
            pendingSurface.worldArea += 0.5f * std::sqrt((std::max)(0.0f, MATH::Dot(worldCross, worldCross)));
            for (const auto& uv : appended.lightmapUv) {
                pendingSurface.uvMin.x = (std::min)(pendingSurface.uvMin.x, uv.x);
                pendingSurface.uvMin.y = (std::min)(pendingSurface.uvMin.y, uv.y);
                pendingSurface.uvMax.x = (std::max)(pendingSurface.uvMax.x, uv.x);
                pendingSurface.uvMax.y = (std::max)(pendingSurface.uvMax.y, uv.y);
            }
            const MATH::Vec2 uvEdge0 = appended.lightmapUv[1] - appended.lightmapUv[0];
            const MATH::Vec2 uvEdge1 = appended.lightmapUv[2] - appended.lightmapUv[0];
            pendingSurface.uvArea += 0.5f * std::abs(uvEdge0.x * uvEdge1.y - uvEdge0.y * uvEdge1.x);
        }
    }
    return analysis;
}

void AccumulateSurfaceAnalysis(TRANSPORT::LightingTransportSurfaceTarget& target,
                               const ASSETS::LIGHTING::LightmapSurfaceAnalysis& analysis) {
    ++target.analyzedSurfaceCount;
    if (analysis.IsEligible()) {
        ++target.eligibleSurfaceCount;
        return;
    }
    // 一つの面は一つの理由で数える。剪裁・半透明・未対応の面は自動展開もしないので UV1 が無いのは当然で、UV の問題には
    // 数えない。
    using enum ASSETS::LIGHTING::LightmapSurfaceIssue;
    if (analysis.HasIssue(AlphaMaskedSurface)) {
        ++target.alphaMaskedSurfaceCount;
    } else if (analysis.HasIssue(BlendedSurface)) {
        ++target.blendedSurfaceCount;
    } else if (analysis.HasIssue(UnsupportedVertexLayout) || analysis.HasIssue(MorphTargetsUnsupported) ||
               analysis.HasIssue(MissingGeometry)) {
        ++target.unsupportedSurfaceCount;
    } else {
        ++target.invalidUvSurfaceCount;
    }
}

void FinalizeSurfaceAtlas(TRANSPORT::LightingTransportScene& scene, TRANSPORT::LightingTransportSurfaceTarget& target,
                          std::vector<PendingLightmapSurface>& pendingSurfaces) {
    std::sort(pendingSurfaces.begin(), pendingSurfaces.end(),
              [](const PendingLightmapSurface& lhs, const PendingLightmapSurface& rhs) {
                  if (lhs.identity.nodeIndex != rhs.identity.nodeIndex) {
                      return lhs.identity.nodeIndex < rhs.identity.nodeIndex;
                  }
                  if (lhs.identity.meshIndex != rhs.identity.meshIndex) {
                      return lhs.identity.meshIndex < rhs.identity.meshIndex;
                  }
                  return lhs.identity.primitiveIndex < rhs.identity.primitiveIndex;
              });

    if (pendingSurfaces.empty()) {
        target.eligible = false;
    } else {
        std::vector<ASSETS::LIGHTING::LightmapAtlasSurfaceMetrics> metrics{};
        metrics.reserve(pendingSurfaces.size());
        for (const PendingLightmapSurface& pending : pendingSurfaces) {
            ASSETS::LIGHTING::LightmapAtlasSurfaceMetrics item{};
            item.surface = pending.identity;
            item.worldArea = pending.worldArea;
            item.uvArea = pending.uvArea;
            item.uvMin = pending.uvMin;
            item.uvMax = pending.uvMax;
            // 未使用の UV 領域を atlas へ持ち込まない。生成 chart の内部余白は
            // 元の UV 単位で維持し、外周の余白は atlas allocator が所有する。
            const auto croppedExtent = [](uint32_t reference, float span) {
                return (std::max)(ASSETS::LIGHTING::kSurfaceLightmapMinimumAuthoredUvExtent,
                                  static_cast<uint32_t>(std::ceil(reference * span)));
            };
            item.minimumInteriorWidth = croppedExtent(pending.minimumInteriorWidth, item.uvMax.x - item.uvMin.x);
            item.minimumInteriorHeight = croppedExtent(pending.minimumInteriorHeight, item.uvMax.y - item.uvMin.y);
            metrics.push_back(item);
        }

        std::vector<ASSETS::LIGHTING::LightmapAtlasLayout> layouts{};
        std::string layoutMessage;
        if (!ASSETS::LIGHTING::BuildLightmapAtlasPages(metrics, {}, layouts, layoutMessage)) {
            target.eligible = false;
            target.eligibilityMessage = layoutMessage;
        } else {
            target.eligible = true;
            target.eligibilityMessage = layoutMessage;
            for (const auto& layout : layouts) {
                auto& page = target.pages.emplace_back();
                page.outputResolution = layout.resolution;
                page.requestedTexelsPerWorldUnit = layout.requestedTexelsPerWorldUnit;
                page.resolvedTexelsPerWorldUnit = layout.resolvedTexelsPerWorldUnit;
                page.atlasDensityReduced = layout.densityReduced;
                page.atlasAllocations = layout.allocations;

                for (uint32_t surfaceIndex = 0u; surfaceIndex < layout.allocations.size(); ++surfaceIndex) {
                    const ASSETS::LIGHTING::LightmapAtlasAllocation& allocation = layout.allocations[surfaceIndex];
                    const auto pendingFound =
                        std::find_if(pendingSurfaces.begin(), pendingSurfaces.end(),
                                     [&allocation](const PendingLightmapSurface& pending) {
                                         return pending.identity.nodeIndex == allocation.surface.nodeIndex &&
                                                pending.identity.meshIndex == allocation.surface.meshIndex &&
                                                pending.identity.primitiveIndex == allocation.surface.primitiveIndex;
                                     });
                    if (pendingFound == pendingSurfaces.end()) {
                        continue;
                    }
                    for (const uint32_t triangleIndex : pendingFound->triangleIndices) {
                        TRANSPORT::LightingTransportTriangle& triangle = scene.triangles[triangleIndex];
                        triangle.lightmapAllocationIndex = surfaceIndex;
                        for (MATH::Vec2& uv : triangle.lightmapUv) {
                            uv = {uv.x * allocation.uvScaleBias.x + allocation.uvScaleBias.z,
                                  uv.y * allocation.uvScaleBias.y + allocation.uvScaleBias.w};
                        }
                        page.triangleIndices.push_back(triangleIndex);
                    }

                    ASSETS::LIGHTING::LightmapSurfaceBinding binding{};
                    binding.surface = allocation.surface;
                    binding.uvScaleBias = allocation.uvScaleBias;
                    page.surfaceBindings.push_back(binding);
                }
                target.eligible = target.eligible && page.triangleIndices.size() > 0u &&
                                  page.surfaceBindings.size() == layout.allocations.size();
                if (!target.eligible)
                    target.eligibilityMessage = "Lightmap atlas surface identity resolution failed.";
            }
        }
    }

    std::ostringstream message{};
    if (target.eligible) {
        message << "Ready: " << target.eligibilityMessage;
    } else {
        message << (target.eligibilityMessage.empty() ? "No eligible opaque static surface."
                                                      : target.eligibilityMessage);
    }
    if (target.alphaMaskedSurfaceCount > 0u) {
        message << " Mask occluder only=" << target.alphaMaskedSurfaceCount << " (no lightmap).";
    }
    if (target.blendedSurfaceCount > 0u) {
        message << " Blend skipped=" << target.blendedSurfaceCount << ".";
    }
    if (target.invalidUvSurfaceCount > 0u) {
        message << " Invalid or missing UV1 skipped=" << target.invalidUvSurfaceCount << ".";
    }
    if (target.unsupportedSurfaceCount > 0u) {
        message << " Unsupported skipped=" << target.unsupportedSurfaceCount << ".";
    }
    target.eligibilityMessage = message.str();
}

TRANSPORT::LightingTransportLight ConvertLight(const RENDER3D::LIGHTING::SceneLightProvider& source) {
    TRANSPORT::LightingTransportLight light{};
    light.type = source.type == RENDER3D::LIGHTING::SceneLightType::Spot ? TRANSPORT::kTransportLightSpot
                                                                         : TRANSPORT::kTransportLightPoint;
    light.position = source.position;
    light.range = source.range;
    light.sourceRadius = RENDER3D::LIGHTING::SanitizeLocalLightSourceRadius(source.sourceRadius);
    // 焼き込みの輸送は点とスポットだけを扱う。管と長方形は広がりを半径にした点光源で近似する。
    if (source.type == RENDER3D::LIGHTING::SceneLightType::Tube ||
        source.type == RENDER3D::LIGHTING::SceneLightType::Rect) {
        light.sourceRadius = RENDER3D::LIGHTING::SanitizeLocalLightSourceRadius((std::max)(
            light.sourceRadius,
            0.5f * (std::max)(source.length,
                              source.type == RENDER3D::LIGHTING::SceneLightType::Rect ? source.height : 0.0f)));
    }
    light.direction = MATH::Normalize(source.direction);
    light.radiance = source.color * (std::max)(0.0f, source.intensity);
    if (source.type == RENDER3D::LIGHTING::SceneLightType::Spot) {
        const auto cone = RENDER3D::LIGHTING::ResolveSpotLightCone(source);
        light.direction = RENDER3D::LIGHTING::ResolveSpotLightDirection(source);
        light.innerConeCos = cone.innerCos;
        light.outerConeCos = cone.outerCos;
        light.angularFalloff = cone.angularFalloff;
    }
    return light;
}

} // namespace

TRANSPORT::LightingTransportScene BuildLightingTransportScene(const SceneDocument& document,
                                                              const SceneEnvironment& environment,
                                                              const RENDER3D::LIGHTING::SceneLightingSet& lighting,
                                                              const RENDER3D::RUNTIME::SceneRenderCache& renderCache,
                                                              const SkyManager* skyManager) {
    TRANSPORT::LightingTransportScene scene{};
    const SkySettings& sky = environment.sky;
    const auto solar = ATMOSPHERE::BuildAtmosphereSolarState(environment);
    scene.environment.skyTintExposure = {sky.tint.x, sky.tint.y, sky.tint.z, ResolveSkyTextureRadianceScale(sky)};
    scene.environment.skyMode = sky.lightIntensity > 0.0f ? ResolveSkyRenderMode(sky.source) : SkyMode::None;
    scene.environment.skyYaw = sky.yaw;
    if (scene.environment.skyMode == SkyMode::Cubemap || scene.environment.skyMode == SkyMode::Texture2D) {
        const auto* asset = skyManager ? skyManager->FindAsset(sky.asset) : nullptr;
        if (!asset || asset->texturePath.empty()) {
            scene.environment.error = "Sky asset is unavailable for DXR baking: " + sky.asset;
        } else {
            auto& texture = scene.environment.skyTexture;
            texture.enabled = true;
            texture.dimension = scene.environment.skyMode == SkyMode::Cubemap
                                    ? RENDER3D::TextureResourceDimension::TextureCube
                                    : RENDER3D::TextureResourceDimension::Texture2D;
            texture.load = {"lighting_transport/sky", asset->texturePath,
                            asset->textureIsLinear ? RENDER3D::TextureResourceColorSpace::Linear
                                                   : RENDER3D::TextureResourceColorSpace::Srgb};
        }
    } else if (scene.environment.skyMode == SkyMode::Atmosphere) {
        scene.environment.atmosphere = ATMOSPHERE::BuildAtmosphereLutConstants(environment.atmosphere, solar);
    }
    scene.environment.skyStrength = scene.environment.skyMode != SkyMode::None ? sky.lightIntensity : 0.0f;

    // Runtime と同じ一つの主光源だけを輸送する。非選択 sun を bake へ混ぜない。
    if (solar.valid) {
        TRANSPORT::LightingTransportLight sun{};
        sun.type = TRANSPORT::kTransportLightSun;
        sun.direction = solar.sunDirection * -1.0f;
        sun.radiance = solar.sunColor * solar.sunRadiance;
        scene.lights.push_back(sun);
    }
    for (const auto& light : lighting.lights) {
        if (light.enabled && RENDER3D::LIGHTING::IsLocalLightUploadable(light)) {
            scene.lights.push_back(ConvertLight(light));
        }
    }

    const auto policies = BuildObjectPolicies(document);
    MaterialSnapshotIndices materialIndices{};
    std::unordered_map<uint64_t, size_t> targetByObject{};
    std::vector<std::vector<PendingLightmapSurface>> pendingSurfacesByTarget{};
    for (const auto& surface : renderCache.GetSurfaceInstances()) {
        if (!surface.valid || !surface.visible || surface.skinned || !surface.hasDrawWorldMatrix) {
            continue;
        }
        const auto policy = policies.find(surface.objectId.value);
        if (policy == policies.end() || !policy->second.enabled || !policy->second.include ||
            !policy->second.renderStatic || !surface.isStatic) {
            continue;
        }

        size_t targetIndex = 0u;
        const auto foundTarget = targetByObject.find(surface.objectId.value);
        if (foundTarget == targetByObject.end()) {
            TRANSPORT::LightingTransportSurfaceTarget target{};
            target.objectId = surface.objectId.value;
            target.name = policy->second.name;
            scene.targets.push_back(std::move(target));
            pendingSurfacesByTarget.emplace_back();
            targetIndex = scene.targets.size() - 1u;
            targetByObject[surface.objectId.value] = targetIndex;
        } else {
            targetIndex = foundTarget->second;
        }

        TRANSPORT::LightingTransportSurfaceTarget& target = scene.targets[targetIndex];
        PendingLightmapSurface pendingSurface{};
        pendingSurface.identity.nodeIndex = surface.nodeIndex;
        pendingSurface.identity.meshIndex = surface.meshIndex;
        pendingSurface.identity.primitiveIndex = surface.primitiveIndex;
        const ASSETS::LIGHTING::LightmapSurfaceAnalysis analysis =
            AppendSurfaceTriangles(surface, scene, materialIndices, pendingSurface);
        AccumulateSurfaceAnalysis(target, analysis);
        if (analysis.IsEligible() && !pendingSurface.triangleIndices.empty()) {
            pendingSurfacesByTarget[targetIndex].push_back(std::move(pendingSurface));
        }
    }

    for (size_t targetIndex = 0u; targetIndex < scene.targets.size(); ++targetIndex) {
        FinalizeSurfaceAtlas(scene, scene.targets[targetIndex], pendingSurfacesByTarget[targetIndex]);
    }
    return scene;
}

} // namespace HIKARI::DOCUMENT_SCENE_BAKING
