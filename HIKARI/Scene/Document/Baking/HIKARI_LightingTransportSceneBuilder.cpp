#include "Scene/Document/Baking/HIKARI_LightingTransportSceneBuilder.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <utility>

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

        bool IsFiniteUv(const MATH::Vec2& uv) {
            return std::isfinite(uv.x) &&
                std::isfinite(uv.y) &&
                uv.x >= -0.0001f &&
                uv.y >= -0.0001f &&
                uv.x <= 1.0001f &&
                uv.y <= 1.0001f;
        }

        bool IsValidLightmapTriangle(
            const TRANSPORT::LightingTransportTriangle& triangle) {
            for (const MATH::Vec2& uv : triangle.lightmapUv) {
                if (!IsFiniteUv(uv)) {
                    return false;
                }
            }
            const MATH::Vec2 edge0 =
                triangle.lightmapUv[1] -
                triangle.lightmapUv[0];
            const MATH::Vec2 edge1 =
                triangle.lightmapUv[2] -
                triangle.lightmapUv[0];
            const float area =
                edge0.x * edge1.y -
                edge0.y * edge1.x;
            return std::abs(area) > 1.0e-8f;
        }

        void ResolveMaterial(
            const RENDER3D::RUNTIME::SceneSurfaceInstance& surface,
            MATH::Vec3& outDiffuse,
            MATH::Vec3& outEmissive,
            AlphaMode& outAlphaMode) {

            outDiffuse = { 1.0f, 1.0f, 1.0f };
            outEmissive = {};
            outAlphaMode = AlphaMode::Opaque;
            if (surface.materialOverride != nullptr) {
                const MATH::Vec4 color =
                    surface.materialOverride->GetBaseColor();
                outDiffuse = { color.x, color.y, color.z };
                const MATH::Vec3 emissive =
                    surface.materialOverride->GetEmissiveFactor();
                const float strength =
                    surface.materialOverride->GetEmissiveStrength();
                outEmissive = emissive * strength;
                return;
            }
            if (surface.model == nullptr ||
                surface.materialIndex >=
                    surface.model->materials.size()) {
                return;
            }
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

        void AppendSurfaceTriangles(
            const RENDER3D::RUNTIME::SceneSurfaceInstance& surface,
            TRANSPORT::LightingTransportScene& scene,
            TRANSPORT::LightingTransportSurfaceTarget& target,
            bool& targetUvValid) {

            if (surface.model == nullptr ||
                surface.meshIndex >= surface.model->meshes.size()) {
                return;
            }
            const MeshAsset& mesh =
                surface.model->meshes[surface.meshIndex];
            if (surface.primitiveIndex >= mesh.primitives.size()) {
                return;
            }
            const MeshPrimitive& primitive =
                mesh.primitives[surface.primitiveIndex];
            if (primitive.layout != VertexLayoutKind::StaticPNTT ||
                primitive.hasMorphTargets ||
                primitive.staticVertices.empty() ||
                primitive.indices.size() < 3u) {
                targetUvValid = false;
                return;
            }

            MATH::Vec3 diffuse{};
            MATH::Vec3 emissive{};
            AlphaMode alphaMode = AlphaMode::Opaque;
            ResolveMaterial(
                surface,
                diffuse,
                emissive,
                alphaMode);
            if (alphaMode != AlphaMode::Opaque) {
                targetUvValid = false;
                return;
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
                    targetUvValid = false;
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
                targetUvValid =
                    targetUvValid &&
                    IsValidLightmapTriangle(triangle);
                target.triangleIndices.push_back(
                    static_cast<uint32_t>(
                        scene.triangles.size()));
                scene.triangles.push_back(std::move(triangle));
            }
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
                target.eligible = true;
                scene.targets.push_back(std::move(target));
                targetIndex = scene.targets.size() - 1u;
                targetByObject[surface.objectId.value] =
                    targetIndex;
            } else {
                targetIndex = foundTarget->second;
            }

            TRANSPORT::LightingTransportSurfaceTarget& target =
                scene.targets[targetIndex];
            bool uvValid = target.eligible;
            AppendSurfaceTriangles(
                surface,
                scene,
                target,
                uvValid);
            target.eligible = uvValid;
        }

        for (TRANSPORT::LightingTransportSurfaceTarget& target :
             scene.targets) {
            if (target.triangleIndices.empty()) {
                target.eligible = false;
                target.eligibilityMessage =
                    "No supported opaque static triangles.";
            } else if (!target.eligible) {
                target.eligibilityMessage =
                    "UV1 is missing, outside [0, 1], or degenerate. "
                    "Bake-time rasterization also rejects excessive overlap.";
            } else {
                target.eligibilityMessage =
                    "Ready for surface lightmap bake.";
            }
        }
        return scene;
    }

} // namespace HIKARI::DOCUMENT_SCENE_BAKING
