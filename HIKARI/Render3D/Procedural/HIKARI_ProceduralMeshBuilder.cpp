#include "Render3D/Procedural/HIKARI_ProceduralMeshBuilder.h"

#include <cmath>

#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI::PROCEDURAL {
    namespace {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kTwoPi = kPi * 2.0f;

        Vertex3D MakeVertex(
            const MATH::Vec3& position,
            const MATH::Vec3& normal,
            const MATH::Vec4& tangent,
            float u,
            float v) {

            Vertex3D vertex{};
            vertex.position = position;
            vertex.normal = normal;
            vertex.tangent = tangent;
            vertex.uv0 = { u, v };
            vertex.color0 = { 1.0f, 1.0f, 1.0f, 1.0f };
            return vertex;
        }

        MATH::Vec4 MakeRadialTangent(float theta, bool enabled) {
            return enabled
                ? MATH::Vec4{ -std::sin(theta), 0.0f, std::cos(theta), 1.0f }
                : MATH::Vec4{};
        }

        void AppendRoundedRing(
            MeshPrimitive& primitive,
            uint32_t slices,
            float radius,
            float centerY,
            float radial,
            float normalY,
            float v,
            bool generateTangents) {

            for (uint32_t slice = 0; slice <= slices; ++slice) {
                const float u =
                    static_cast<float>(slice) /
                    static_cast<float>(slices);
                const float theta = u * kTwoPi;
                const MATH::Vec3 normal{
                    radial * std::cos(theta),
                    normalY,
                    radial * std::sin(theta)
                };
                primitive.staticVertices.push_back(MakeVertex(
                    {
                        normal.x * radius,
                        centerY + normal.y * radius,
                        normal.z * radius
                    },
                    normal,
                    MakeRadialTangent(theta, generateTangents),
                    u,
                    v));
            }
        }

        void BuildGridPlane(
            const ProceduralMeshSettings& settings,
            MeshPrimitive& primitive) {

            const uint32_t sx = settings.segmentsX;
            const uint32_t sy = settings.segmentsY;
            primitive.staticVertices.reserve(
                static_cast<size_t>(sx + 1u) * static_cast<size_t>(sy + 1u));
            for (uint32_t y = 0; y <= sy; ++y) {
                const float fy = static_cast<float>(y) / static_cast<float>(sy);
                for (uint32_t x = 0; x <= sx; ++x) {
                    const float fx = static_cast<float>(x) / static_cast<float>(sx);
                    primitive.staticVertices.push_back(MakeVertex(
                        {
                            -settings.width * 0.5f + settings.width * fx,
                            0.0f,
                            -settings.depth * 0.5f + settings.depth * fy
                        },
                        { 0.0f, 1.0f, 0.0f },
                        settings.generateTangents
                            ? MATH::Vec4{ 1.0f, 0.0f, 0.0f, 1.0f }
                            : MATH::Vec4{},
                        fx,
                        fy));
                }
            }

            primitive.indices.reserve(
                static_cast<size_t>(sx) * static_cast<size_t>(sy) * 6u);
            for (uint32_t y = 0; y < sy; ++y) {
                for (uint32_t x = 0; x < sx; ++x) {
                    const uint32_t row0 = y * (sx + 1u);
                    const uint32_t row1 = (y + 1u) * (sx + 1u);
                    const uint32_t i0 = row0 + x;
                    const uint32_t i1 = row0 + x + 1u;
                    const uint32_t i2 = row1 + x;
                    const uint32_t i3 = row1 + x + 1u;
                    primitive.indices.insert(
                        primitive.indices.end(),
                        { i0, i2, i1, i1, i2, i3 });
                }
            }
        }

        void AddBoxFace(
            MeshPrimitive& primitive,
            MATH::Vec3 a,
            MATH::Vec3 b,
            MATH::Vec3 c,
            MATH::Vec3 d,
            MATH::Vec3 normal,
            MATH::Vec4 tangent) {

            const uint32_t base =
                static_cast<uint32_t>(primitive.staticVertices.size());
            primitive.staticVertices.push_back(MakeVertex(a, normal, tangent, 0.0f, 1.0f));
            primitive.staticVertices.push_back(MakeVertex(b, normal, tangent, 1.0f, 1.0f));
            primitive.staticVertices.push_back(MakeVertex(c, normal, tangent, 0.0f, 0.0f));
            primitive.staticVertices.push_back(MakeVertex(d, normal, tangent, 1.0f, 0.0f));
            primitive.indices.insert(
                primitive.indices.end(),
                { base, base + 1u, base + 2u, base + 1u, base + 3u, base + 2u });
        }

        void BuildBox(
            const ProceduralMeshSettings& settings,
            MeshPrimitive& primitive) {

            const float x = settings.width * 0.5f;
            const float y = settings.height * 0.5f;
            const float z = settings.depth * 0.5f;
            const auto tangent = [enabled = settings.generateTangents](
                MATH::Vec4 value) {
                return enabled ? value : MATH::Vec4{};
            };
            AddBoxFace(primitive, { -x, -y, z }, { x, -y, z }, { -x, y, z }, { x, y, z }, { 0, 0, 1 }, tangent({ 1, 0, 0, 1 }));
            AddBoxFace(primitive, { x, -y, -z }, { -x, -y, -z }, { x, y, -z }, { -x, y, -z }, { 0, 0, -1 }, tangent({ -1, 0, 0, 1 }));
            AddBoxFace(primitive, { -x, -y, -z }, { -x, -y, z }, { -x, y, -z }, { -x, y, z }, { -1, 0, 0 }, tangent({ 0, 0, 1, 1 }));
            AddBoxFace(primitive, { x, -y, z }, { x, -y, -z }, { x, y, z }, { x, y, -z }, { 1, 0, 0 }, tangent({ 0, 0, -1, 1 }));
            AddBoxFace(primitive, { -x, y, z }, { x, y, z }, { -x, y, -z }, { x, y, -z }, { 0, 1, 0 }, tangent({ 1, 0, 0, 1 }));
            AddBoxFace(primitive, { -x, -y, -z }, { x, -y, -z }, { -x, -y, z }, { x, -y, z }, { 0, -1, 0 }, tangent({ 1, 0, 0, 1 }));
        }

        void BuildRoundedVerticalShape(
            const ProceduralMeshSettings& settings,
            MeshPrimitive& primitive,
            bool capsule) {

            const uint32_t slices = settings.sphereSlices;
            const float radius = settings.width * 0.5f;
            const float halfCylinder = capsule
                ? (settings.height - settings.width) * 0.5f
                : 0.0f;
            const bool hasCylinderSection =
                capsule && halfCylinder > 1e-6f;
            const uint32_t ringCount = capsule
                ? (hasCylinderSection
                    ? settings.sphereStacks * 2u
                    : settings.sphereStacks * 2u - 1u)
                : settings.sphereStacks - 1u;
            primitive.staticVertices.reserve(
                2u +
                static_cast<size_t>(slices + 1u) *
                static_cast<size_t>(ringCount));

            const MATH::Vec4 poleTangent = settings.generateTangents
                ? MATH::Vec4{ 1.0f, 0.0f, 0.0f, 1.0f }
                : MATH::Vec4{};
            primitive.staticVertices.push_back(MakeVertex(
                { 0.0f, halfCylinder + radius, 0.0f },
                { 0.0f, 1.0f, 0.0f },
                poleTangent,
                0.5f,
                0.0f));

            if (!capsule || !hasCylinderSection) {
                const uint32_t verticalSegments = capsule
                    ? settings.sphereStacks * 2u
                    : settings.sphereStacks;
                for (uint32_t ring = 1u;
                    ring < verticalSegments;
                    ++ring) {

                    const float v =
                        static_cast<float>(ring) /
                        static_cast<float>(verticalSegments);
                    const float phi = v * kPi;
                    AppendRoundedRing(
                        primitive,
                        slices,
                        radius,
                        0.0f,
                        std::sin(phi),
                        std::cos(phi),
                        v,
                        settings.generateTangents);
                }
            } else {
                const uint32_t hemisphereSegments =
                    settings.sphereStacks;
                const float totalHeight =
                    halfCylinder * 2.0f + radius * 2.0f;
                const float topY = halfCylinder + radius;

                for (uint32_t step = 1u;
                    step <= hemisphereSegments;
                    ++step) {

                    const float angle =
                        static_cast<float>(step) /
                        static_cast<float>(hemisphereSegments) *
                        (kPi * 0.5f);
                    const float normalY = std::cos(angle);
                    const float y =
                        halfCylinder + normalY * radius;
                    AppendRoundedRing(
                        primitive,
                        slices,
                        radius,
                        halfCylinder,
                        std::sin(angle),
                        normalY,
                        (topY - y) / totalHeight,
                        settings.generateTangents);
                }

                for (uint32_t step = 0u;
                    step < hemisphereSegments;
                    ++step) {

                    const float angle =
                        static_cast<float>(step) /
                        static_cast<float>(hemisphereSegments) *
                        (kPi * 0.5f);
                    const float normalY = -std::sin(angle);
                    const float y =
                        -halfCylinder + normalY * radius;
                    AppendRoundedRing(
                        primitive,
                        slices,
                        radius,
                        -halfCylinder,
                        std::cos(angle),
                        normalY,
                        (topY - y) / totalHeight,
                        settings.generateTangents);
                }
            }

            const uint32_t bottomPole =
                static_cast<uint32_t>(primitive.staticVertices.size());
            primitive.staticVertices.push_back(MakeVertex(
                { 0.0f, -halfCylinder - radius, 0.0f },
                { 0.0f, -1.0f, 0.0f },
                poleTangent,
                0.5f,
                1.0f));

            const uint32_t rowSize = slices + 1u;
            const uint32_t firstRing = 1u;
            for (uint32_t slice = 0; slice < slices; ++slice) {
                primitive.indices.insert(
                    primitive.indices.end(),
                    { 0u, firstRing + slice + 1u, firstRing + slice });
            }
            for (uint32_t ring = 0; ring + 1u < ringCount; ++ring) {
                for (uint32_t slice = 0; slice < slices; ++slice) {
                    const uint32_t i0 = firstRing + ring * rowSize + slice;
                    const uint32_t i1 = i0 + 1u;
                    const uint32_t i2 = i0 + rowSize;
                    const uint32_t i3 = i2 + 1u;
                    primitive.indices.insert(
                        primitive.indices.end(),
                        { i0, i1, i2, i1, i3, i2 });
                }
            }
            const uint32_t lastRing =
                firstRing + (ringCount - 1u) * rowSize;
            for (uint32_t slice = 0; slice < slices; ++slice) {
                primitive.indices.insert(
                    primitive.indices.end(),
                    { lastRing + slice, lastRing + slice + 1u, bottomPole });
            }
        }

        void BuildCylinderCap(
            const ProceduralMeshSettings& settings,
            MeshPrimitive& primitive,
            float y,
            float normalY) {

            const uint32_t slices = settings.sphereSlices;
            const float radius = settings.width * 0.5f;
            const MATH::Vec4 tangent = settings.generateTangents
                ? MATH::Vec4{ 1.0f, 0.0f, 0.0f, 1.0f }
                : MATH::Vec4{};
            const uint32_t center =
                static_cast<uint32_t>(primitive.staticVertices.size());
            primitive.staticVertices.push_back(MakeVertex(
                { 0.0f, y, 0.0f },
                { 0.0f, normalY, 0.0f },
                tangent,
                0.5f,
                0.5f));
            for (uint32_t slice = 0; slice <= slices; ++slice) {
                const float u = static_cast<float>(slice) / static_cast<float>(slices);
                const float theta = u * kTwoPi;
                const float x = radius * std::cos(theta);
                const float z = radius * std::sin(theta);
                primitive.staticVertices.push_back(MakeVertex(
                    { x, y, z },
                    { 0.0f, normalY, 0.0f },
                    tangent,
                    0.5f + x / (radius * 2.0f),
                    0.5f + z / (radius * 2.0f)));
            }
            for (uint32_t slice = 0; slice < slices; ++slice) {
                const uint32_t current = center + 1u + slice;
                const uint32_t next = current + 1u;
                if (normalY > 0.0f) {
                    primitive.indices.insert(
                        primitive.indices.end(), { center, next, current });
                } else {
                    primitive.indices.insert(
                        primitive.indices.end(), { center, current, next });
                }
            }
        }

        void BuildCylinder(
            const ProceduralMeshSettings& settings,
            MeshPrimitive& primitive) {

            const uint32_t slices = settings.sphereSlices;
            const uint32_t rows = settings.segmentsY;
            const float radius = settings.width * 0.5f;
            const float halfHeight = settings.height * 0.5f;
            for (uint32_t row = 0; row <= rows; ++row) {
                const float v = static_cast<float>(row) / static_cast<float>(rows);
                const float y = -halfHeight + settings.height * v;
                for (uint32_t slice = 0; slice <= slices; ++slice) {
                    const float u = static_cast<float>(slice) / static_cast<float>(slices);
                    const float theta = u * kTwoPi;
                    const MATH::Vec3 normal{
                        std::cos(theta), 0.0f, std::sin(theta)
                    };
                    primitive.staticVertices.push_back(MakeVertex(
                        { normal.x * radius, y, normal.z * radius },
                        normal,
                        MakeRadialTangent(theta, settings.generateTangents),
                        u,
                        1.0f - v));
                }
            }
            const uint32_t rowSize = slices + 1u;
            for (uint32_t row = 0; row < rows; ++row) {
                for (uint32_t slice = 0; slice < slices; ++slice) {
                    const uint32_t i0 = row * rowSize + slice;
                    const uint32_t i1 = i0 + 1u;
                    const uint32_t i2 = i0 + rowSize;
                    const uint32_t i3 = i2 + 1u;
                    primitive.indices.insert(
                        primitive.indices.end(),
                        { i0, i2, i1, i1, i2, i3 });
                }
            }
            BuildCylinderCap(settings, primitive, halfHeight, 1.0f);
            BuildCylinderCap(settings, primitive, -halfHeight, -1.0f);
        }
    }

    void BuildProceduralMesh(
        const ProceduralMeshSettings& rawSettings,
        MeshPrimitive& outPrimitive) {

        const ProceduralMeshSettings settings =
            SanitizeProceduralMeshSettings(rawSettings);
        switch (settings.kind) {
        case ProceduralMeshKind::Plane:
        case ProceduralMeshKind::GridPlane:
            BuildGridPlane(settings, outPrimitive);
            break;
        case ProceduralMeshKind::Sphere:
            BuildRoundedVerticalShape(settings, outPrimitive, false);
            break;
        case ProceduralMeshKind::Cylinder:
            BuildCylinder(settings, outPrimitive);
            break;
        case ProceduralMeshKind::Capsule:
            BuildRoundedVerticalShape(settings, outPrimitive, true);
            break;
        case ProceduralMeshKind::Box:
        default:
            BuildBox(settings, outPrimitive);
            break;
        }
    }

} // namespace HIKARI::PROCEDURAL
