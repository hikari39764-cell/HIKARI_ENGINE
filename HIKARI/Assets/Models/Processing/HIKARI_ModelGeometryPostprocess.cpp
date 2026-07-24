#include "Assets/Models/Processing/HIKARI_ModelGeometryPostprocess.h"

#include <cmath>
#include <vector>

#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI::ASSETS::MODELS {

    namespace {

        MATH::Vec3 BuildFallbackTangent(const MATH::Vec3& normal) {
            const MATH::Vec3 reference =
                std::abs(normal.y) < 0.999f
                    ? MATH::Vec3{ 0.0f, 1.0f, 0.0f }
                    : MATH::Vec3{ 1.0f, 0.0f, 0.0f };
            return MATH::Normalize(MATH::Cross(reference, normal));
        }

    } // namespace

    bool ModelMaterialHasNormalTexture(
        const ModelAsset& asset,
        uint32_t materialIndex) noexcept {

        return
            materialIndex < asset.materials.size() &&
            asset.materials[materialIndex].normalTexture.textureIndex >= 0;
    }

    void GenerateModelPrimitiveNormals(MeshPrimitive& primitive) {
        for (Vertex3D& vertex : primitive.staticVertices) {
            vertex.normal = {};
        }

        for (size_t i = 0; i + 2u < primitive.indices.size(); i += 3u) {
            const uint32_t i0 = primitive.indices[i + 0u];
            const uint32_t i1 = primitive.indices[i + 1u];
            const uint32_t i2 = primitive.indices[i + 2u];
            if (i0 >= primitive.staticVertices.size() ||
                i1 >= primitive.staticVertices.size() ||
                i2 >= primitive.staticVertices.size()) {
                continue;
            }

            const MATH::Vec3 p0 = primitive.staticVertices[i0].position;
            const MATH::Vec3 p1 = primitive.staticVertices[i1].position;
            const MATH::Vec3 p2 = primitive.staticVertices[i2].position;
            const MATH::Vec3 normal =
                MATH::Normalize(MATH::Cross(p1 - p0, p2 - p0));
            if (MATH::Length(normal) <= 1.0e-6f) {
                continue;
            }

            primitive.staticVertices[i0].normal =
                primitive.staticVertices[i0].normal + normal;
            primitive.staticVertices[i1].normal =
                primitive.staticVertices[i1].normal + normal;
            primitive.staticVertices[i2].normal =
                primitive.staticVertices[i2].normal + normal;
        }

        for (Vertex3D& vertex : primitive.staticVertices) {
            vertex.normal = MATH::Normalize(vertex.normal);
            if (MATH::Length(vertex.normal) <= 1.0e-6f) {
                vertex.normal = { 0.0f, 1.0f, 0.0f };
            }
        }
    }

    void GenerateModelPrimitiveTangents(MeshPrimitive& primitive) {
        std::vector<MATH::Vec3> tangentAccum(primitive.staticVertices.size());
        std::vector<MATH::Vec3> bitangentAccum(primitive.staticVertices.size());

        for (size_t i = 0; i + 2u < primitive.indices.size(); i += 3u) {
            const uint32_t i0 = primitive.indices[i + 0u];
            const uint32_t i1 = primitive.indices[i + 1u];
            const uint32_t i2 = primitive.indices[i + 2u];
            if (i0 >= primitive.staticVertices.size() ||
                i1 >= primitive.staticVertices.size() ||
                i2 >= primitive.staticVertices.size()) {
                continue;
            }

            const Vertex3D& v0 = primitive.staticVertices[i0];
            const Vertex3D& v1 = primitive.staticVertices[i1];
            const Vertex3D& v2 = primitive.staticVertices[i2];
            const MATH::Vec3 edge1 = v1.position - v0.position;
            const MATH::Vec3 edge2 = v2.position - v0.position;
            const MATH::Vec2 duv1 = v1.uv0 - v0.uv0;
            const MATH::Vec2 duv2 = v2.uv0 - v0.uv0;

            const float determinant = duv1.x * duv2.y - duv1.y * duv2.x;
            if (std::abs(determinant) <= 1.0e-8f) {
                continue;
            }

            const float inverse = 1.0f / determinant;
            const MATH::Vec3 tangent =
                (edge1 * duv2.y - edge2 * duv1.y) * inverse;
            const MATH::Vec3 bitangent =
                (edge2 * duv1.x - edge1 * duv2.x) * inverse;

            tangentAccum[i0] = tangentAccum[i0] + tangent;
            tangentAccum[i1] = tangentAccum[i1] + tangent;
            tangentAccum[i2] = tangentAccum[i2] + tangent;
            bitangentAccum[i0] = bitangentAccum[i0] + bitangent;
            bitangentAccum[i1] = bitangentAccum[i1] + bitangent;
            bitangentAccum[i2] = bitangentAccum[i2] + bitangent;
        }

        for (size_t i = 0; i < primitive.staticVertices.size(); ++i) {
            MATH::Vec3 normal =
                MATH::Normalize(primitive.staticVertices[i].normal);
            if (MATH::Length(normal) <= 1.0e-6f) {
                normal = { 0.0f, 1.0f, 0.0f };
            }

            MATH::Vec3 tangent =
                tangentAccum[i] - normal * MATH::Dot(normal, tangentAccum[i]);
            tangent = MATH::Normalize(tangent);
            if (MATH::Length(tangent) <= 1.0e-6f) {
                tangent = BuildFallbackTangent(normal);
            }
            if (MATH::Length(tangent) <= 1.0e-6f) {
                tangent = { 1.0f, 0.0f, 0.0f };
            }

            const float handedness =
                MATH::Dot(
                    MATH::Cross(normal, tangent),
                    bitangentAccum[i]) < 0.0f
                    ? -1.0f
                    : 1.0f;
            primitive.staticVertices[i].tangent = {
                tangent.x,
                tangent.y,
                tangent.z,
                handedness
            };
        }
    }

} // namespace HIKARI::ASSETS::MODELS
