#include "Scene/Document/Baking/HIKARI_LightingTransportSceneBuilder.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "Assets/Lighting/HIKARI_LightmapSurfaceAnalysis.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Core/Math/HIKARI_NormalMatrix.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::DOCUMENT_SCENE_BAKING {

    namespace TRANSPORT =
        RENDER3D::LIGHTING::TRANSPORT;

    namespace {

        struct ObjectBakePolicy {
            std::string name{};
            bool enabled = false;
            bool include = false;
            bool renderStatic = false;
        };

        struct PendingLightmapSurface {
            ASSETS::LIGHTING::LightmapSurfaceIdentity
                identity{};
            std::vector<uint32_t> triangleIndices{};
        };

        const SceneComponentData* FindModelComponent(
            const SceneObjectData& object) {
            const auto found = std::find_if(
                object.components.begin(),
                object.components.end(),
                [](const SceneComponentData& component) {
                    return component.type == "ModelComponent";
                });
            return found == object.components.end()
                ? nullptr
                : &*found;
        }

        std::unordered_map<uint64_t, ObjectBakePolicy>
            BuildObjectPolicies(const SceneDocument& document) {

            std::unordered_map<uint64_t, ObjectBakePolicy>
                policies{};
            for (const SceneObjectData& object : document.objects) {
                const SceneComponentData* model =
                    FindModelComponent(object);
                if (model == nullptr) {
                    continue;
                }
                ObjectBakePolicy policy{};
                policy.name = object.name;
                policy.enabled =
                    object.enabled &&
                    model->properties.value("visible", true);
                policy.include =
                    model->properties.value(
                        "includeInLightingBake",
                        true);
                policy.renderStatic =
                    model->properties.value(
                        "renderStatic",
                        false);
                policies[object.id.value] = std::move(policy);
            }
            return policies;
        }

        MATH::Vec3 TransformPosition(
            const MATH::Mat4& matrix,
            const MATH::Vec3& position) {
            const MATH::Vec4 transformed =
                matrix.TransformPoint({
                    position.x,
                    position.y,
                    position.z,
                    1.0f
                });
            return {
                transformed.x,
                transformed.y,
                transformed.z
            };
        }

        MATH::Vec3 TransformNormal(
            const MATH::Mat4& matrix,
            const MATH::Vec3& normal) {
            const MATH::Vec4 transformed =
                matrix.TransformPoint({
                    normal.x,
                    normal.y,
                    normal.z,
                    0.0f
                });
            return MATH::Normalize({
                transformed.x,
                transformed.y,
                transformed.z
            });
        }

        void ResolveMaterial(
            const RENDER3D::RUNTIME::SceneSurfaceInstance& surface,
            MATH::Vec3& outDiffuse,
            MATH::Vec3& outEmissive,
            AlphaMode& outAlphaMode) {

            outDiffuse = { 1.0f, 1.0f, 1.0f };
            outEmissive = {};
            outAlphaMode = AlphaMode::Opaque;
            if (surface.model != nullptr &&
                surface.materialIndex <
                    surface.model->materials.size()) {
                const MaterialAsset& material =
                    surface.model->materials[
                        surface.materialIndex];
                outDiffuse = {
                    material.baseColorFactor.x,
                    material.baseColorFactor.y,
                    material.baseColorFactor.z
                };
                outEmissive =
                    material.emissiveFactor *
                    material.emissiveStrength;
                outAlphaMode = material.alphaMode;
            }

            if (surface.materialOverride == nullptr) {
                return;
            }

            const MATH::Vec4 color =
                surface.materialOverride->GetBaseColor();
            outDiffuse = { color.x, color.y, color.z };
            const MATH::Vec3 emissive =
                surface.materialOverride->GetEmissiveFactor();
            const float strength =
                surface.materialOverride->GetEmissiveStrength();
            outEmissive = emissive * strength;
            const uint32_t features =
                surface.materialOverride->GetFeatureBits();
            if ((features &
                    MATERIAL_FEATURES::AlphaMask) != 0u) {
                outAlphaMode = AlphaMode::Mask;
            } else if (color.w < 0.999f ||
                (features &
                    MATERIAL_FEATURES::
                        ThinTransparentSurface) != 0u) {
                outAlphaMode = AlphaMode::Blend;
            } else {
                outAlphaMode = AlphaMode::Opaque;
            }
        }

        ASSETS::LIGHTING::LightmapSurfaceAnalysis
            AppendSurfaceTriangles(
            const RENDER3D::RUNTIME::SceneSurfaceInstance& surface,
            TRANSPORT::LightingTransportScene& scene,
            PendingLightmapSurface& pendingSurface) {

            if (surface.model == nullptr ||
                surface.meshIndex >= surface.model->meshes.size()) {
                ASSETS::LIGHTING::LightmapSurfaceAnalysis
                    analysis{};
                analysis.issueMask =
                    static_cast<uint32_t>(
                        ASSETS::LIGHTING::
                            LightmapSurfaceIssue::
                                MissingGeometry);
                return analysis;
            }
            const MeshAsset& mesh =
                surface.model->meshes[surface.meshIndex];
            if (surface.primitiveIndex >= mesh.primitives.size()) {
                ASSETS::LIGHTING::LightmapSurfaceAnalysis
                    analysis{};
                analysis.issueMask =
                    static_cast<uint32_t>(
                        ASSETS::LIGHTING::
                            LightmapSurfaceIssue::
                                MissingGeometry);
                return analysis;
            }
            const MeshPrimitive& primitive =
                mesh.primitives[surface.primitiveIndex];

            MATH::Vec3 diffuse{};
            MATH::Vec3 emissive{};
            AlphaMode alphaMode = AlphaMode::Opaque;
            ResolveMaterial(
                surface,
                diffuse,
                emissive,
                alphaMode);
            const ASSETS::LIGHTING::LightmapSurfaceAnalysis
                analysis =
                    ASSETS::LIGHTING::AnalyzeLightmapSurface(
                        primitive,
                        alphaMode);
            if (analysis.HasIssue(
                    ASSETS::LIGHTING::
                        LightmapSurfaceIssue::
                            UnsupportedVertexLayout) ||
                analysis.HasIssue(
                    ASSETS::LIGHTING::
                        LightmapSurfaceIssue::
                            MorphTargetsUnsupported) ||
                analysis.HasIssue(
                    ASSETS::LIGHTING::
                        LightmapSurfaceIssue::
                            MissingGeometry) ||
                analysis.HasIssue(
                    ASSETS::LIGHTING::
                        LightmapSurfaceIssue::
                            AlphaMaskedSurface) ||
                analysis.HasIssue(
                    ASSETS::LIGHTING::
                        LightmapSurfaceIssue::
                            BlendedSurface)) {
                return analysis;
            }

            const MATH::Mat4 normalMatrix =
                MATH::BuildNormalMatrixFromWorld(
                    surface.drawWorldMatrix);
            for (size_t index = 0;
                index + 2u < primitive.indices.size();
                index += 3u) {
                const uint32_t indices[3]{
                    primitive.indices[index],
                    primitive.indices[index + 1u],
                    primitive.indices[index + 2u]
                };
                if (indices[0] >= primitive.staticVertices.size() ||
                    indices[1] >= primitive.staticVertices.size() ||
                    indices[2] >= primitive.staticVertices.size()) {
                    continue;
                }

                TRANSPORT::LightingTransportTriangle triangle{};
                triangle.objectId = surface.objectId.value;
                triangle.diffuseReflectance = diffuse;
                triangle.emissiveRadiance = emissive;
                for (uint32_t corner = 0u;
                    corner < 3u;
                    ++corner) {
                    const Vertex3D& vertex =
                        primitive.staticVertices[
                            indices[corner]];
                    triangle.position[corner] =
                        TransformPosition(
                            surface.drawWorldMatrix,
                            vertex.position);
                    triangle.normal[corner] =
                        TransformNormal(
                            normalMatrix,
                            vertex.normal);
                    triangle.lightmapUv[corner] =
                        vertex.uv1;
                }
                const uint32_t triangleIndex =
                    static_cast<uint32_t>(
                        scene.triangles.size());
                scene.triangles.push_back(std::move(triangle));
                if (analysis.IsEligible()) {
                    pendingSurface.triangleIndices.
                        push_back(triangleIndex);
                }
            }
            return analysis;
        }

        uint32_t NextPowerOfTwo(uint32_t value) {
            uint32_t result = 1u;
            while (result < value && result < 1024u) {
                result <<= 1u;
            }
            return (std::min)(result, 1024u);
        }

        void AccumulateSurfaceAnalysis(
            TRANSPORT::LightingTransportSurfaceTarget& target,
            const ASSETS::LIGHTING::LightmapSurfaceAnalysis&
                analysis) {
            ++target.analyzedSurfaceCount;
            if (analysis.IsEligible()) {
                ++target.eligibleSurfaceCount;
                return;
            }
            if (analysis.HasIssue(
                    ASSETS::LIGHTING::
                        LightmapSurfaceIssue::
                            AlphaMaskedSurface)) {
                ++target.alphaMaskedSurfaceCount;
            }
            if (analysis.HasIssue(
                    ASSETS::LIGHTING::
                        LightmapSurfaceIssue::
                            BlendedSurface)) {
                ++target.blendedSurfaceCount;
            }
            if (analysis.HasIssue(
                    ASSETS::LIGHTING::
                        LightmapSurfaceIssue::UvNonFinite) ||
                analysis.HasIssue(
                    ASSETS::LIGHTING::
                        LightmapSurfaceIssue::
                            UvOutsideUnitRange) ||
                analysis.HasIssue(
                    ASSETS::LIGHTING::
                        LightmapSurfaceIssue::UvDegenerate) ||
                analysis.HasIssue(
                    ASSETS::LIGHTING::
                        LightmapSurfaceIssue::UvOverlap) ||
                analysis.HasIssue(
                    ASSETS::LIGHTING::
                        LightmapSurfaceIssue::
                            IndexOutOfRange)) {
                ++target.invalidUvSurfaceCount;
            }
            if (analysis.HasIssue(
                    ASSETS::LIGHTING::
                        LightmapSurfaceIssue::
                            UnsupportedVertexLayout) ||
                analysis.HasIssue(
                    ASSETS::LIGHTING::
                        LightmapSurfaceIssue::
                            MorphTargetsUnsupported) ||
                analysis.HasIssue(
                    ASSETS::LIGHTING::
                        LightmapSurfaceIssue::
                            MissingGeometry)) {
                ++target.unsupportedSurfaceCount;
            }
        }

        void FinalizeSurfaceAtlas(
            TRANSPORT::LightingTransportScene& scene,
            TRANSPORT::LightingTransportSurfaceTarget& target,
            std::vector<PendingLightmapSurface>&
                pendingSurfaces) {

            std::sort(
                pendingSurfaces.begin(),
                pendingSurfaces.end(),
                [](const PendingLightmapSurface& lhs,
                   const PendingLightmapSurface& rhs) {
                    if (lhs.identity.nodeIndex !=
                        rhs.identity.nodeIndex) {
                        return lhs.identity.nodeIndex <
                            rhs.identity.nodeIndex;
                    }
                    if (lhs.identity.meshIndex !=
                        rhs.identity.meshIndex) {
                        return lhs.identity.meshIndex <
                            rhs.identity.meshIndex;
                    }
                    return lhs.identity.primitiveIndex <
                        rhs.identity.primitiveIndex;
                });

            if (pendingSurfaces.empty()) {
                target.eligible = false;
            } else {
                const uint32_t surfaceCount =
                    static_cast<uint32_t>(
                        pendingSurfaces.size());
                const uint32_t columns =
                    static_cast<uint32_t>(
                        std::ceil(std::sqrt(
                            static_cast<float>(
                                surfaceCount))));
                const uint32_t rows =
                    (surfaceCount + columns - 1u) /
                    columns;
                target.outputResolution =
                    NextPowerOfTwo((std::max)(
                        128u,
                        (std::max)(columns, rows) *
                            32u));

                for (uint32_t surfaceIndex = 0u;
                    surfaceIndex < surfaceCount;
                    ++surfaceIndex) {
                    PendingLightmapSurface& pending =
                        pendingSurfaces[surfaceIndex];
                    const uint32_t column =
                        surfaceIndex % columns;
                    const uint32_t row =
                        surfaceIndex / columns;
                    const uint32_t x0 =
                        column *
                        target.outputResolution /
                        columns;
                    const uint32_t x1 =
                        (column + 1u) *
                        target.outputResolution /
                        columns;
                    const uint32_t y0 =
                        row *
                        target.outputResolution /
                        rows;
                    const uint32_t y1 =
                        (row + 1u) *
                        target.outputResolution /
                        rows;
                    const uint32_t tileWidth = x1 - x0;
                    const uint32_t tileHeight = y1 - y0;
                    const uint32_t padding =
                        (std::min)(
                            4u,
                            (std::min)(
                                tileWidth,
                                tileHeight) / 4u);
                    const float inverseResolution =
                        1.0f /
                        static_cast<float>(
                            target.outputResolution);
                    const MATH::Vec4 uvScaleBias{
                        static_cast<float>(
                            tileWidth - padding * 2u) *
                            inverseResolution,
                        static_cast<float>(
                            tileHeight - padding * 2u) *
                            inverseResolution,
                        static_cast<float>(x0 + padding) *
                            inverseResolution,
                        static_cast<float>(y0 + padding) *
                            inverseResolution
                    };

                    for (const uint32_t triangleIndex :
                         pending.triangleIndices) {
                        TRANSPORT::LightingTransportTriangle&
                            triangle =
                                scene.triangles[
                                    triangleIndex];
                        for (MATH::Vec2& uv :
                             triangle.lightmapUv) {
                            uv = {
                                uv.x * uvScaleBias.x +
                                    uvScaleBias.z,
                                uv.y * uvScaleBias.y +
                                    uvScaleBias.w
                            };
                        }
                        target.triangleIndices.push_back(
                            triangleIndex);
                    }

                    ASSETS::LIGHTING::
                        LightmapSurfaceBinding binding{};
                    binding.surface = pending.identity;
                    binding.uvScaleBias = uvScaleBias;
                    target.surfaceBindings.push_back(
                        binding);
                }
                target.eligible =
                    !target.triangleIndices.empty();
            }

            std::ostringstream message{};
            if (target.eligible) {
                message
                    << "Ready: "
                    << target.eligibleSurfaceCount
                    << " surface(s) packed into a "
                    << target.outputResolution
                    << " atlas.";
            } else {
                message
                    << "No eligible opaque static surface.";
            }
            if (target.alphaMaskedSurfaceCount > 0u) {
                message
                    << " Mask skipped="
                    << target.alphaMaskedSurfaceCount
                    << " (DXR any-hit not implemented).";
            }
            if (target.blendedSurfaceCount > 0u) {
                message
                    << " Blend skipped="
                    << target.blendedSurfaceCount
                    << ".";
            }
            if (target.invalidUvSurfaceCount > 0u) {
                message
                    << " Invalid UV1 skipped="
                    << target.invalidUvSurfaceCount
                    << ".";
            }
            if (target.unsupportedSurfaceCount > 0u) {
                message
                    << " Unsupported skipped="
                    << target.unsupportedSurfaceCount
                    << ".";
            }
            target.eligibilityMessage = message.str();
        }

        TRANSPORT::LightingTransportLight ConvertLight(
            const RENDER3D::LIGHTING::SceneLightProvider& source) {

            TRANSPORT::LightingTransportLight light{};
            light.type = static_cast<uint32_t>(source.type);
            light.position = source.position;
            light.range = source.range;
            light.direction =
                MATH::Normalize(source.direction);
            light.radiance =
                source.color *
                (std::max)(0.0f, source.intensity);
            if (source.type ==
                RENDER3D::LIGHTING::SceneLightType::Spot) {
                constexpr float kDegreesToRadians =
                    std::numbers::pi_v<float> / 180.0f;
                light.innerConeCos = std::cos(
                    source.innerConeAngleDegrees *
                    kDegreesToRadians);
                light.outerConeCos = std::cos(
                    source.outerConeAngleDegrees *
                    kDegreesToRadians);
            }
            return light;
        }

    } // namespace

    TRANSPORT::LightingTransportScene
        BuildLightingTransportScene(
            const SceneDocument& document,
            const SceneEnvironment& environment,
            const RENDER3D::LIGHTING::SceneLightingSet& lighting,
            const RENDER3D::RUNTIME::SceneRenderCache& renderCache) {

        TRANSPORT::LightingTransportScene scene{};
        scene.environment.ambientRadiance =
            environment.ambient.color *
            (std::max)(0.0f, environment.ambient.intensity);
        scene.environment.skyZenithRadiance =
            environment.sky.zenithColor *
            environment.sky.exposure;
        scene.environment.skyHorizonRadiance =
            environment.sky.horizonColor *
            environment.sky.exposure;
        scene.environment.skyGroundRadiance =
            environment.sky.groundColor *
            environment.sky.exposure;
        scene.environment.skyStrength =
            environment.sky.enabled
            ? (std::max)(
                0.0f,
                environment.sky.ambientFromSky)
            : 0.0f;

        for (const auto& light : lighting.lights) {
            if (light.enabled) {
                scene.lights.push_back(ConvertLight(light));
            }
        }

        const auto policies = BuildObjectPolicies(document);
        std::unordered_map<uint64_t, size_t> targetByObject{};
        std::vector<std::vector<PendingLightmapSurface>>
            pendingSurfacesByTarget{};
        for (const auto& surface :
             renderCache.GetSurfaceInstances()) {
            if (!surface.valid ||
                !surface.visible ||
                surface.skinned ||
                !surface.hasDrawWorldMatrix) {
                continue;
            }
            const auto policy =
                policies.find(surface.objectId.value);
            if (policy == policies.end() ||
                !policy->second.enabled ||
                !policy->second.include ||
                !policy->second.renderStatic ||
                !surface.isStatic) {
                continue;
            }

            size_t targetIndex = 0u;
            const auto foundTarget =
                targetByObject.find(surface.objectId.value);
            if (foundTarget == targetByObject.end()) {
                TRANSPORT::LightingTransportSurfaceTarget target{};
                target.objectId = surface.objectId.value;
                target.name = policy->second.name;
                scene.targets.push_back(std::move(target));
                pendingSurfacesByTarget.emplace_back();
                targetIndex = scene.targets.size() - 1u;
                targetByObject[surface.objectId.value] =
                    targetIndex;
            } else {
                targetIndex = foundTarget->second;
            }

            TRANSPORT::LightingTransportSurfaceTarget& target =
                scene.targets[targetIndex];
            PendingLightmapSurface pendingSurface{};
            pendingSurface.identity.nodeIndex =
                surface.nodeIndex;
            pendingSurface.identity.meshIndex =
                surface.meshIndex;
            pendingSurface.identity.primitiveIndex =
                surface.primitiveIndex;
            const ASSETS::LIGHTING::
                LightmapSurfaceAnalysis analysis =
                    AppendSurfaceTriangles(
                        surface,
                        scene,
                        pendingSurface);
            AccumulateSurfaceAnalysis(
                target,
                analysis);
            if (analysis.IsEligible() &&
                !pendingSurface.triangleIndices.empty()) {
                pendingSurfacesByTarget[targetIndex].
                    push_back(std::move(pendingSurface));
            }
        }

        for (size_t targetIndex = 0u;
            targetIndex < scene.targets.size();
            ++targetIndex) {
            FinalizeSurfaceAtlas(
                scene,
                scene.targets[targetIndex],
                pendingSurfacesByTarget[targetIndex]);
        }
        return scene;
    }

} // namespace HIKARI::DOCUMENT_SCENE_BAKING
