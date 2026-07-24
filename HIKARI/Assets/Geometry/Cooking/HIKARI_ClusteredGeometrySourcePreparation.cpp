#include "Assets/Geometry/Cooking/Internal/HIKARI_ClusteredGeometryCookInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Math/HIKARI_NormalMatrix.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::GEOMETRY::COOKING {

    MATH::Vec3 TransformVector(const MATH::Mat4& matrix, const MATH::Vec3& value) {
        const MATH::Vec4 transformed = matrix.TransformPoint({ value.x, value.y, value.z, 0.0f });
        return { transformed.x, transformed.y, transformed.z };
    }

    ClusterVertex ToClusterVertex(
        const Vertex3D& source,
        const MATH::Mat4& matrix,
        const MATH::Mat4& normalMatrix) {

        ClusterVertex out{};
        const MATH::Vec4 p = matrix.TransformPoint({ source.position.x, source.position.y, source.position.z, 1.0f });
        out.position = { p.x, p.y, p.z };
        out.normal = MATH::Normalize(TransformVector(normalMatrix, source.normal));
        if (MATH::Length(out.normal) <= 1e-5f) {
            out.normal = { 0.0f, 1.0f, 0.0f };
        }
        const MATH::Vec3 tangent = MATH::Normalize(TransformVector(
            normalMatrix,
            { source.tangent.x, source.tangent.y, source.tangent.z }));
        out.tangent = {
            tangent.x,
            tangent.y,
            tangent.z,
            source.tangent.w == 0.0f ? 1.0f : source.tangent.w
        };
        out.uv0 = source.uv0;
        out.uv1 = source.uv1;
        out.color = source.color0;
        return out;
    }

    ClusterVertex ToClusterVertex(const SkinnedVertex3D& source) {
        ClusterVertex out{};
        out.position = source.position;
        out.normal = MATH::Normalize(source.normal);
        if (MATH::Length(out.normal) <= 1e-5f) {
            out.normal = { 0.0f, 1.0f, 0.0f };
        }
        MATH::Vec3 tangent = MATH::Normalize({
            source.tangent.x,
            source.tangent.y,
            source.tangent.z
        });
        if (MATH::Length(tangent) <= 1e-5f) {
            tangent = { 1.0f, 0.0f, 0.0f };
        }
        out.tangent = {
            tangent.x,
            tangent.y,
            tangent.z,
            source.tangent.w == 0.0f ? 1.0f : source.tangent.w
        };
        out.uv0 = source.uv0;
        out.uv1 = source.uv1;
        out.color = source.color0;

        float weightSum = 0.0f;
        for (size_t i = 0; i < 4u; ++i) {
            out.joints[i] = source.joints[i];
            out.weights[i] = (std::max)(0.0f, source.weights[i]);
            weightSum += out.weights[i];
        }
        if (weightSum <= 1e-8f) {
            out.joints[0] = 0u;
            out.weights[0] = 1.0f;
        } else {
            const float inverseWeightSum = 1.0f / weightSum;
            for (float& weight : out.weights) {
                weight *= inverseWeightSum;
            }
        }
        return out;
    }

    Bounds ComputeVertexBounds(const std::vector<ClusterVertex>& vertices) {
        Bounds bounds = BOUNDS::EmptyBounds();
        bool hasBounds = false;
        for (const ClusterVertex& vertex : vertices) {
            if (!MATH::IsFinite(vertex.position)) {
                continue;
            }
            BOUNDS::Encapsulate(bounds, vertex.position);
            hasBounds = true;
        }
        return hasBounds ? bounds : Bounds{};
    }

    Bounds MergeBounds(const Bounds& a, const Bounds& b) {
        if (!BOUNDS::IsUsable(a)) {
            return b;
        }
        if (!BOUNDS::IsUsable(b)) {
            return a;
        }
        Bounds out = a;
        BOUNDS::Encapsulate(out, b);
        return out;
    }

    SourceTriangle BuildTriangle(
        uint32_t i0,
        uint32_t i1,
        uint32_t i2,
        const std::vector<ClusterVertex>& vertices) {

        SourceTriangle tri{};
        tri.i0 = i0;
        tri.i1 = i1;
        tri.i2 = i2;

        Bounds bounds = BOUNDS::EmptyBounds();
        bool hasBounds = false;
        const uint32_t triangleVertices[3] = { i0, i1, i2 };
        for (uint32_t vertexIndex : triangleVertices) {
            const MATH::Vec3& position = vertices[vertexIndex].position;
            if (!MATH::IsFinite(position)) {
                continue;
            }
            BOUNDS::Encapsulate(bounds, position);
            hasBounds = true;
        }
        tri.bounds = hasBounds ? bounds : Bounds{};

        const MATH::Vec3 e1 = vertices[i1].position - vertices[i0].position;
        const MATH::Vec3 e2 = vertices[i2].position - vertices[i0].position;
        const MATH::Vec3 cross = MATH::Cross(e1, e2);
        tri.area = MATH::Length(cross) * 0.5f;
        tri.normal = MATH::Normalize(cross);
        if (MATH::Length(tri.normal) <= 1e-5f) {
            tri.normal = { 0.0f, 1.0f, 0.0f };
        }
        return tri;
    }

    bool HasMissingNormals(const std::vector<ClusterVertex>& vertices) {
        for (const ClusterVertex& vertex : vertices) {
            if (MATH::Length(vertex.normal) <= 1e-5f) {
                return true;
            }
        }
        return false;
    }

    bool HasMissingTangents(const std::vector<ClusterVertex>& vertices) {
        for (const ClusterVertex& vertex : vertices) {
            const MATH::Vec3 t{ vertex.tangent.x, vertex.tangent.y, vertex.tangent.z };
            if (MATH::Length(t) <= 1e-5f) {
                return true;
            }
        }
        return false;
    }

    void GenerateNormals(std::vector<ClusterVertex>& vertices, const std::vector<uint32_t>& indices) {
        std::vector<MATH::Vec3> sums(vertices.size(), {});
        for (size_t i = 0; i + 2u < indices.size(); i += 3u) {
            const uint32_t i0 = indices[i + 0u];
            const uint32_t i1 = indices[i + 1u];
            const uint32_t i2 = indices[i + 2u];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                continue;
            }
            const MATH::Vec3 e1 = vertices[i1].position - vertices[i0].position;
            const MATH::Vec3 e2 = vertices[i2].position - vertices[i0].position;
            MATH::Vec3 n = MATH::Cross(e1, e2);
            if (MATH::Length(n) <= 1e-5f) {
                continue;
            }
            sums[i0] = sums[i0] + n;
            sums[i1] = sums[i1] + n;
            sums[i2] = sums[i2] + n;
        }
        for (size_t i = 0; i < vertices.size(); ++i) {
            MATH::Vec3 n = MATH::Normalize(sums[i]);
            if (MATH::Length(n) <= 1e-5f) {
                n = { 0.0f, 1.0f, 0.0f };
            }
            vertices[i].normal = n;
        }
    }

    void GenerateTangents(std::vector<ClusterVertex>& vertices, const std::vector<uint32_t>& indices) {
        std::vector<MATH::Vec3> sums(vertices.size(), {});
        for (size_t i = 0; i + 2u < indices.size(); i += 3u) {
            const uint32_t i0 = indices[i + 0u];
            const uint32_t i1 = indices[i + 1u];
            const uint32_t i2 = indices[i + 2u];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                continue;
            }

            const MATH::Vec3 p0 = vertices[i0].position;
            const MATH::Vec3 p1 = vertices[i1].position;
            const MATH::Vec3 p2 = vertices[i2].position;
            const MATH::Vec2 uv0 = vertices[i0].uv0;
            const MATH::Vec2 uv1 = vertices[i1].uv0;
            const MATH::Vec2 uv2 = vertices[i2].uv0;
            const MATH::Vec3 e1 = p1 - p0;
            const MATH::Vec3 e2 = p2 - p0;
            const MATH::Vec2 duv1 = uv1 - uv0;
            const MATH::Vec2 duv2 = uv2 - uv0;
            const float denom = duv1.x * duv2.y - duv2.x * duv1.y;
            if (std::abs(denom) <= 1e-8f) {
                continue;
            }
            const float r = 1.0f / denom;
            const MATH::Vec3 tangent = (e1 * duv2.y - e2 * duv1.y) * r;
            sums[i0] = sums[i0] + tangent;
            sums[i1] = sums[i1] + tangent;
            sums[i2] = sums[i2] + tangent;
        }

        for (size_t i = 0; i < vertices.size(); ++i) {
            const MATH::Vec3 n = MATH::Normalize(vertices[i].normal);
            MATH::Vec3 t = sums[i];
            t = t - n * MATH::Dot(n, t);
            t = MATH::Normalize(t);
            if (MATH::Length(t) <= 1e-5f) {
                t = { 1.0f, 0.0f, 0.0f };
            }
            vertices[i].tangent = { t.x, t.y, t.z, 1.0f };
        }
    }

    uint32_t BuildSurfaceFlags(const ModelAsset& model, const MeshPrimitive& primitive, int nodeSkinIndex) {
        uint32_t flags = 0;
        const MaterialAsset* material = primitive.materialIndex < model.materials.size()
            ? &model.materials[primitive.materialIndex]
            : nullptr;

        if (material == nullptr || material->alphaMode == AlphaMode::Opaque) {
            RENDER3D::CLUSTER::AddFlag(flags, ClusterSurfaceFlags::Opaque);
        } else if (material->alphaMode == AlphaMode::Mask) {
            RENDER3D::CLUSTER::AddFlag(flags, ClusterSurfaceFlags::AlphaMask);
        } else {
            RENDER3D::CLUSTER::AddFlag(flags, ClusterSurfaceFlags::Transparent);
        }
        // material の doubleSided は描画結果の契約なので、cluster cook でも保持する。
        if (material != nullptr && SURFACE_POLICY::ShouldRenderDoubleSided(*material, primitive)) {
            RENDER3D::CLUSTER::AddFlag(flags, ClusterSurfaceFlags::DoubleSided);
        }
        if (primitive.layout == VertexLayoutKind::SkinnedPNTTJW ||
            !primitive.skinnedVertices.empty() ||
            nodeSkinIndex >= 0) {
            RENDER3D::CLUSTER::AddFlag(flags, ClusterSurfaceFlags::Skinned);
        }
        const bool skinned =
            primitive.layout == VertexLayoutKind::SkinnedPNTTJW ||
            !primitive.skinnedVertices.empty() ||
            nodeSkinIndex >= 0;
        if (primitive.indices.size() < 3u ||
            (skinned
                ? primitive.skinnedVertices.empty()
                : primitive.staticVertices.empty())) {
            RENDER3D::CLUSTER::AddFlag(flags, ClusterSurfaceFlags::Unsupported);
        }
        return flags;
    }

    bool IsClusterablePrimitive(
        const ModelAsset& model,
        const MeshPrimitive& primitive,
        int nodeSkinIndex) {

        const uint32_t flags = BuildSurfaceFlags(model, primitive, nodeSkinIndex);
        return !primitive.hasMorphTargets &&
            !RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::Unsupported);
    }

    void AccumulateSourcePrimitiveCounts(
        const ModelAsset& model,
        const MeshPrimitive& primitive,
        int nodeSkinIndex,
        SourceGeometryCounts& counts) {

        if (!IsClusterablePrimitive(model, primitive, nodeSkinIndex)) {
            return;
        }

        counts.staticTriangleCount += static_cast<uint32_t>(primitive.indices.size() / 3u);
        const bool skinned =
            primitive.layout == VertexLayoutKind::SkinnedPNTTJW ||
            !primitive.skinnedVertices.empty() ||
            nodeSkinIndex >= 0;
        counts.staticVertexCount += static_cast<uint32_t>(
            skinned
                ? primitive.skinnedVertices.size()
                : primitive.staticVertices.size());
    }

    SourceGeometryCounts CountSourceGeometry(const ModelAsset& model) {
        SourceGeometryCounts counts{};
        if (!model.nodes.empty()) {
            for (const ModelNode& node : model.nodes) {
                if (node.meshIndex < 0 || node.meshIndex >= static_cast<int>(model.meshes.size())) {
                    continue;
                }
                const MeshAsset& mesh = model.meshes[static_cast<size_t>(node.meshIndex)];
                for (const MeshPrimitive& primitive : mesh.primitives) {
                    AccumulateSourcePrimitiveCounts(model, primitive, node.skinIndex, counts);
                }
            }
            return counts;
        }

        for (const MeshAsset& mesh : model.meshes) {
            for (const MeshPrimitive& primitive : mesh.primitives) {
                AccumulateSourcePrimitiveCounts(model, primitive, -1, counts);
            }
        }
        return counts;
    }

    std::vector<SourceTriangle> BuildTriangles(
        const std::vector<ClusterVertex>& vertices,
        const std::vector<uint32_t>& indices) {

        std::vector<SourceTriangle> triangles;
        triangles.reserve(indices.size() / 3u);
        for (size_t i = 0; i + 2u < indices.size(); i += 3u) {
            const uint32_t i0 = indices[i + 0u];
            const uint32_t i1 = indices[i + 1u];
            const uint32_t i2 = indices[i + 2u];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
                continue;
            }
            if (i0 == i1 || i1 == i2 || i0 == i2) {
                continue;
            }
            triangles.push_back(BuildTriangle(i0, i1, i2, vertices));
        }
        return triangles;
    }

    uint32_t CountSurfaceTriangles(const SurfaceCookInput& work) {
        return static_cast<uint32_t>(work.indices.size() / 3u);
    }

    float Distance(const MATH::Vec3& a, const MATH::Vec3& b) {
        const MATH::Vec3 d = a - b;
        return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    }

    float TriangleMaxEdgeLength(
        const std::vector<ClusterVertex>& vertices,
        uint32_t i0,
        uint32_t i1,
        uint32_t i2) {

        if (i0 >= vertices.size() ||
            i1 >= vertices.size() ||
            i2 >= vertices.size()) {
            return 0.0f;
        }

        return (std::max)(
            Distance(vertices[i0].position, vertices[i1].position),
            (std::max)(
                Distance(vertices[i1].position, vertices[i2].position),
                Distance(vertices[i2].position, vertices[i0].position)));
    }

    MATH::Vec2 LerpVec2(const MATH::Vec2& a, const MATH::Vec2& b, float t) {
        return {
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t
        };
    }

    MATH::Vec3 LerpVec3(const MATH::Vec3& a, const MATH::Vec3& b, float t) {
        return {
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t
        };
    }

    MATH::Vec4 LerpVec4(const MATH::Vec4& a, const MATH::Vec4& b, float t) {
        return {
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        };
    }

    ClusterVertex LerpClusterVertex(
        const ClusterVertex& a,
        const ClusterVertex& b,
        float t) {

        ClusterVertex out{};
        out.position = LerpVec3(a.position, b.position, t);
        out.normal = MATH::Normalize(LerpVec3(a.normal, b.normal, t));
        if (MATH::Length(out.normal) <= 1.0e-5f) {
            out.normal = MATH::Length(a.normal) > 1.0e-5f
                ? a.normal
                : MATH::Vec3{ 0.0f, 1.0f, 0.0f };
        }

        const MATH::Vec3 tangentA{ a.tangent.x, a.tangent.y, a.tangent.z };
        const MATH::Vec3 tangentB{ b.tangent.x, b.tangent.y, b.tangent.z };
        MATH::Vec3 tangent = MATH::Normalize(LerpVec3(tangentA, tangentB, t));
        if (MATH::Length(tangent) <= 1.0e-5f) {
            tangent = MATH::Length(tangentA) > 1.0e-5f
                ? tangentA
                : MATH::Vec3{ 1.0f, 0.0f, 0.0f };
        }
        const float handedness =
            std::abs(a.tangent.w) >= std::abs(b.tangent.w)
                ? a.tangent.w
                : b.tangent.w;
        out.tangent = {
            tangent.x,
            tangent.y,
            tangent.z,
            std::abs(handedness) <= 1.0e-5f ? 1.0f : handedness
        };
        out.uv0 = LerpVec2(a.uv0, b.uv0, t);
        out.uv1 = LerpVec2(a.uv1, b.uv1, t);
        out.color = LerpVec4(a.color, b.color, t);

        struct Influence {
            uint16_t joint = 0;
            float weight = 0.0f;
        };
        std::array<Influence, 8> influences{};
        size_t influenceCount = 0;
        const auto appendInfluence = [&](uint16_t joint, float weight) {
            if (weight <= 1e-8f) {
                return;
            }
            for (size_t i = 0; i < influenceCount; ++i) {
                if (influences[i].joint == joint) {
                    influences[i].weight += weight;
                    return;
                }
            }
            if (influenceCount < influences.size()) {
                influences[influenceCount++] = { joint, weight };
            }
        };
        for (size_t i = 0; i < 4u; ++i) {
            appendInfluence(a.joints[i], a.weights[i] * (1.0f - t));
            appendInfluence(b.joints[i], b.weights[i] * t);
        }
        std::sort(
            influences.begin(),
            influences.begin() + influenceCount,
            [](const Influence& lhs, const Influence& rhs) {
                return lhs.weight > rhs.weight;
            });
        const size_t retainedInfluenceCount =
            (std::min)(influenceCount, size_t{ 4u });
        float retainedWeightSum = 0.0f;
        for (size_t i = 0; i < retainedInfluenceCount; ++i) {
            out.joints[i] = influences[i].joint;
            out.weights[i] = influences[i].weight;
            retainedWeightSum += influences[i].weight;
        }
        if (retainedWeightSum > 1e-8f) {
            const float inverseWeightSum = 1.0f / retainedWeightSum;
            for (size_t i = 0; i < retainedInfluenceCount; ++i) {
                out.weights[i] *= inverseWeightSum;
            }
        }
        return out;
    }

    struct TriangleSubdivisionStats {
        uint32_t sourceTriangleCount = 0;
        uint32_t outputTriangleCount = 0;
        bool budgetReached = false;
    };

    uint32_t AppendMidpointVertex(
        std::vector<ClusterVertex>& vertices,
        uint32_t a,
        uint32_t b) {

        if (a >= vertices.size() || b >= vertices.size()) {
            return a;
        }
        if (vertices.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
            return a;
        }

        const uint32_t index = static_cast<uint32_t>(vertices.size());
        vertices.push_back(LerpClusterVertex(vertices[a], vertices[b], 0.5f));
        return index;
    }

    void AppendTriangleIndices(
        std::vector<uint32_t>& indices,
        uint32_t i0,
        uint32_t i1,
        uint32_t i2,
        TriangleSubdivisionStats& stats) {

        indices.push_back(i0);
        indices.push_back(i1);
        indices.push_back(i2);
        ++stats.outputTriangleCount;
    }

    void AppendSubdividedTriangleRecursive(
        std::vector<ClusterVertex>& vertices,
        std::vector<uint32_t>& indices,
        uint32_t i0,
        uint32_t i1,
        uint32_t i2,
        float maxEdgeLength,
        uint32_t maxDepth,
        uint32_t depth,
        uint32_t maxGeneratedTriangles,
        TriangleSubdivisionStats& stats) {

        if (stats.outputTriangleCount >= maxGeneratedTriangles) {
            stats.budgetReached = true;
            AppendTriangleIndices(indices, i0, i1, i2, stats);
            return;
        }

        const float e01 = i0 < vertices.size() && i1 < vertices.size()
            ? Distance(vertices[i0].position, vertices[i1].position)
            : 0.0f;
        const float e12 = i1 < vertices.size() && i2 < vertices.size()
            ? Distance(vertices[i1].position, vertices[i2].position)
            : 0.0f;
        const float e20 = i2 < vertices.size() && i0 < vertices.size()
            ? Distance(vertices[i2].position, vertices[i0].position)
            : 0.0f;
        const float longest = (std::max)(e01, (std::max)(e12, e20));
        if (longest <= maxEdgeLength ||
            depth >= maxDepth ||
            stats.outputTriangleCount + 2u > maxGeneratedTriangles) {
            AppendTriangleIndices(indices, i0, i1, i2, stats);
            if (longest > maxEdgeLength &&
                (depth >= maxDepth || stats.outputTriangleCount >= maxGeneratedTriangles)) {
                stats.budgetReached = true;
            }
            return;
        }

        if (e01 >= e12 && e01 >= e20) {
            const uint32_t mid = AppendMidpointVertex(vertices, i0, i1);
            AppendSubdividedTriangleRecursive(
                vertices, indices, i0, mid, i2, maxEdgeLength,
                maxDepth, depth + 1u, maxGeneratedTriangles, stats);
            AppendSubdividedTriangleRecursive(
                vertices, indices, mid, i1, i2, maxEdgeLength,
                maxDepth, depth + 1u, maxGeneratedTriangles, stats);
        } else if (e12 >= e20) {
            const uint32_t mid = AppendMidpointVertex(vertices, i1, i2);
            AppendSubdividedTriangleRecursive(
                vertices, indices, i1, mid, i0, maxEdgeLength,
                maxDepth, depth + 1u, maxGeneratedTriangles, stats);
            AppendSubdividedTriangleRecursive(
                vertices, indices, mid, i2, i0, maxEdgeLength,
                maxDepth, depth + 1u, maxGeneratedTriangles, stats);
        } else {
            const uint32_t mid = AppendMidpointVertex(vertices, i2, i0);
            AppendSubdividedTriangleRecursive(
                vertices, indices, i2, mid, i1, maxEdgeLength,
                maxDepth, depth + 1u, maxGeneratedTriangles, stats);
            AppendSubdividedTriangleRecursive(
                vertices, indices, mid, i0, i1, maxEdgeLength,
                maxDepth, depth + 1u, maxGeneratedTriangles, stats);
        }
    }

    bool ShouldSubdivideLargeStaticTriangles(
        const SurfaceCookInput& work,
        const ClusteredGeometryCookSettings& settings) {

        return
            settings.subdivideLargeStaticTriangles &&
            settings.surfacePartitionPolicy == SurfacePartitionPolicy::SceneStatic &&
            ShouldUsePermissiveOpaqueLods(work.flags) &&
            work.vertices.size() >= 3u &&
            work.indices.size() >= 3u &&
            settings.largeStaticTriangleMaxEdgeLength > 0.0f &&
            settings.largeStaticTriangleMaxSubdivisions > 0u &&
            settings.largeStaticTriangleMaxGeneratedTriangles > 0u;
    }

    SurfaceCookInput SubdivideLargeStaticTriangles(
        const SurfaceCookInput& work,
        const ClusteredGeometryCookSettings& settings,
        ClusteredGeometryBuildReport& report) {

        if (!ShouldSubdivideLargeStaticTriangles(work, settings)) {
            return work;
        }

        const std::vector<SourceTriangle> sourceTriangles =
            BuildTriangles(work.vertices, work.indices);
        const SurfaceShapeAnalysis shapeAnalysis =
            AnalyzeSurfaceShape(sourceTriangles, ComputeVertexBounds(work.vertices));
        const bool balancePlanarSubdivision =
            settings.balancePlanarStaticSurfaces &&
            settings.surfacePartitionPolicy == SurfacePartitionPolicy::SceneStatic &&
            shapeAnalysis.largePlanar;
        if (balancePlanarSubdivision &&
            sourceTriangles.size() >= (std::max)(
                settings.largeSurfacePartitionMinTriangles / 2u,
                256u)) {
            ++report.planarSubdivisionSkippedSurfaceCount;
            return work;
        }

        SurfaceCookInput refined = work;
        refined.indices.clear();
        refined.indices.reserve(work.indices.size());
        float maxEdgeLength =
            (std::max)(0.1f, settings.largeStaticTriangleMaxEdgeLength);
        uint32_t maxDepth =
            (std::max)(1u, settings.largeStaticTriangleMaxSubdivisions);
        uint32_t maxGeneratedTriangles =
            (std::max)(1u, settings.largeStaticTriangleMaxGeneratedTriangles);
        if (balancePlanarSubdivision) {
            maxEdgeLength = (std::max)(
                maxEdgeLength,
                (std::max)(2.0f, settings.planarStaticSurfaceMinPartitionExtent * 0.50f));
            maxDepth = (std::min)(maxDepth, 4u);
            maxGeneratedTriangles = (std::min)(maxGeneratedTriangles, 16384u);
            ++report.planarSubdivisionCoarsenedSurfaceCount;
        }

        TriangleSubdivisionStats stats{};
        for (size_t i = 0; i + 2u < work.indices.size(); i += 3u) {
            const uint32_t i0 = work.indices[i + 0u];
            const uint32_t i1 = work.indices[i + 1u];
            const uint32_t i2 = work.indices[i + 2u];
            if (i0 >= refined.vertices.size() ||
                i1 >= refined.vertices.size() ||
                i2 >= refined.vertices.size()) {
                continue;
            }

            const float maxEdge = TriangleMaxEdgeLength(refined.vertices, i0, i1, i2);
            if (maxEdge > maxEdgeLength) {
                ++stats.sourceTriangleCount;
            }
            if (stats.budgetReached) {
                AppendTriangleIndices(refined.indices, i0, i1, i2, stats);
                continue;
            }
            AppendSubdividedTriangleRecursive(
                refined.vertices,
                refined.indices,
                i0,
                i1,
                i2,
                maxEdgeLength,
                maxDepth,
                0u,
                maxGeneratedTriangles,
                stats);
        }

        if (stats.sourceTriangleCount == 0u ||
            refined.indices.size() <= work.indices.size()) {
            return work;
        }

        ++report.subdividedSurfaceCount;
        report.subdividedSourceTriangleCount += stats.sourceTriangleCount;
        report.subdividedOutputTriangleCount += stats.outputTriangleCount;
        if (stats.budgetReached) {
            report.messages.push_back(
                "large static triangle subdivision reached per-surface budget");
        }
        return refined;
    }


    bool HasNormalMap(const ModelAsset& model, uint32_t materialIndex) {
        if (materialIndex >= model.materials.size()) {
            return false;
        }
        return model.materials[materialIndex].normalTexture.textureIndex >= 0;
    }

    bool BuildSurfaceCookInput(
        const ModelAsset& model,
        const MeshPrimitive& primitive,
        uint32_t nodeIndex,
        int nodeSkinIndex,
        uint32_t meshIndex,
        uint32_t primitiveIndex,
        const MATH::Mat4& matrix,
        const ClusteredGeometryCookSettings& settings,
        SurfaceCookInput& outWork) {

        outWork = {};
        outWork.nodeIndex = nodeIndex;
        outWork.meshIndex = meshIndex;
        outWork.primitiveIndex = primitiveIndex;
        outWork.materialIndex = primitive.materialIndex;
        outWork.flags = BuildSurfaceFlags(model, primitive, nodeSkinIndex);

        if (primitive.hasMorphTargets ||
            RENDER3D::CLUSTER::HasFlag(outWork.flags, ClusterSurfaceFlags::Unsupported)) {
            return false;
        }

        const bool skinned =
            RENDER3D::CLUSTER::HasFlag(outWork.flags, ClusterSurfaceFlags::Skinned);
        outWork.vertices.reserve(
            skinned
                ? primitive.skinnedVertices.size()
                : primitive.staticVertices.size());
        // 位置は node global を焼き込むが、法線と接線は逆転置で焼き込む。
        if (skinned) {
            for (const SkinnedVertex3D& vertex : primitive.skinnedVertices) {
                outWork.vertices.push_back(ToClusterVertex(vertex));
            }
        } else {
            const MATH::Mat4 normalMatrix = MATH::BuildNormalMatrixFromWorld(matrix);
            for (const Vertex3D& vertex : primitive.staticVertices) {
                outWork.vertices.push_back(ToClusterVertex(vertex, matrix, normalMatrix));
            }
        }
        outWork.indices = primitive.indices;

        if (settings.generateMissingNormals && HasMissingNormals(outWork.vertices)) {
            GenerateNormals(outWork.vertices, outWork.indices);
        }
        if (settings.generateMissingTangents &&
            HasNormalMap(model, primitive.materialIndex) &&
            HasMissingTangents(outWork.vertices)) {
            GenerateTangents(outWork.vertices, outWork.indices);
        }
        return !outWork.vertices.empty() && outWork.indices.size() >= 3u;
    }

    void AppendMaterialSlots(const ModelAsset& model, ClusteredGeometryAsset& asset) {
        asset.materialSlotMapping.clear();
        asset.materialSlotMapping.reserve(model.materials.size());
        for (uint32_t i = 0; i < model.materials.size(); ++i) {
            asset.materialSlotMapping.push_back(i);
        }
        if (asset.materialSlotMapping.empty()) {
            asset.materialSlotMapping.push_back(0u);
        }
    }

} // namespace HIKARI::ASSETS::GEOMETRY::COOKING
