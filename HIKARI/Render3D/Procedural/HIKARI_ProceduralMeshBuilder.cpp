#include "Render3D/Procedural/HIKARI_ProceduralMeshBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI::PROCEDURAL {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;

Vertex3D MakeVertex(const MATH::Vec3& position, const MATH::Vec3& normal, const MATH::Vec4& tangent, float u, float v) {
    Vertex3D vertex{};
    vertex.position = position;
    vertex.normal = normal;
    vertex.tangent = tangent;
    vertex.uv0 = {u, v};
    vertex.color0 = {1.0f, 1.0f, 1.0f, 1.0f};
    return vertex;
}

MATH::Vec4 MakeRadialTangent(float theta, bool enabled) {
    return enabled ? MATH::Vec4{-std::sin(theta), 0.0f, std::cos(theta), 1.0f} : MATH::Vec4{};
}

void AppendRoundedRing(MeshPrimitive& primitive, uint32_t slices, float radius, float centerY, float radial,
                       float normalY, float v, bool generateTangents) {
    for (uint32_t slice = 0; slice <= slices; ++slice) {
        const float u = static_cast<float>(slice) / static_cast<float>(slices);
        const float theta = u * kTwoPi;
        const MATH::Vec3 normal{radial * std::cos(theta), normalY, radial * std::sin(theta)};
        primitive.staticVertices.push_back(
            MakeVertex({normal.x * radius, centerY + normal.y * radius, normal.z * radius}, normal,
                       MakeRadialTangent(theta, generateTangents), u, v));
    }
}

void BuildGridPlane(const ProceduralMeshSettings& settings, MeshPrimitive& primitive) {
    const uint32_t sx = settings.segmentsX;
    const uint32_t sy = settings.segmentsY;
    // GridPlane は既定で 1 セグメントごとに 1 回タイルする。
    // terrain response mesh は明示 tile 数を使い、細分化の切替で
    // 地面全体のテクスチャ密度が突然変わらないようにする。
    const bool tilePerSegment = settings.kind == ProceduralMeshKind::GridPlane;
    const float tileCountX = settings.uvTilesX > 0.0f ? settings.uvTilesX : static_cast<float>(sx);
    const float tileCountY = settings.uvTilesY > 0.0f ? settings.uvTilesY : static_cast<float>(sy);
    primitive.staticVertices.reserve(static_cast<size_t>(sx + 1u) * static_cast<size_t>(sy + 1u));
    for (uint32_t y = 0; y <= sy; ++y) {
        const float fy = static_cast<float>(y) / static_cast<float>(sy);
        for (uint32_t x = 0; x <= sx; ++x) {
            const float fx = static_cast<float>(x) / static_cast<float>(sx);
            const float u = tilePerSegment ? fx * tileCountX : fx;
            const float v = tilePerSegment ? fy * tileCountY : fy;
            Vertex3D vertex = MakeVertex(
                {-settings.width * 0.5f + settings.width * fx, 0.0f, -settings.depth * 0.5f + settings.depth * fy},
                {0.0f, 1.0f, 0.0f}, settings.generateTangents ? MATH::Vec4{1.0f, 0.0f, 0.0f, 1.0f} : MATH::Vec4{}, u,
                v);
            // uv0 はマテリアル用のタイル座標、uv1 は地形高さ場・
            // coverage 用の正規化座標として役割を分離する。
            vertex.uv1 = {fx, fy};
            primitive.staticVertices.push_back(vertex);
        }
    }

    primitive.indices.reserve(static_cast<size_t>(sx) * static_cast<size_t>(sy) * 6u);
    for (uint32_t y = 0; y < sy; ++y) {
        for (uint32_t x = 0; x < sx; ++x) {
            const uint32_t row0 = y * (sx + 1u);
            const uint32_t row1 = (y + 1u) * (sx + 1u);
            const uint32_t i0 = row0 + x;
            const uint32_t i1 = row0 + x + 1u;
            const uint32_t i2 = row1 + x;
            const uint32_t i3 = row1 + x + 1u;
            primitive.indices.insert(primitive.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
}

// 四角の面。a・b が下の辺（uv の v が大きい側）、c・d が上の辺。uv は a から d の順に (u0,v1) (u1,v1) (u0,v0) (u1,v0)。
void AddBoxFace(MeshPrimitive& primitive, MATH::Vec3 a, MATH::Vec3 b, MATH::Vec3 c, MATH::Vec3 d, MATH::Vec3 normal,
                MATH::Vec4 tangent, MATH::Vec4 uv = {0.0f, 0.0f, 1.0f, 1.0f}) {
    const uint32_t base = static_cast<uint32_t>(primitive.staticVertices.size());
    primitive.staticVertices.push_back(MakeVertex(a, normal, tangent, uv.x, uv.w));
    primitive.staticVertices.push_back(MakeVertex(b, normal, tangent, uv.z, uv.w));
    primitive.staticVertices.push_back(MakeVertex(c, normal, tangent, uv.x, uv.y));
    primitive.staticVertices.push_back(MakeVertex(d, normal, tangent, uv.z, uv.y));
    primitive.indices.insert(primitive.indices.end(), {base, base + 1u, base + 2u, base + 1u, base + 3u, base + 2u});
}

// 箱の面の向き。u は接線の向き、v は面の上から下へ（BuildBox の uv の向き）。
struct BoxFace {
    int axis;
    int sign;
    MATH::Vec3 normal;
    int uAxis;
    float uSign;
    int vAxis;
    float vSign;
    MATH::Vec4 tangent;
};
constexpr BoxFace kBoxFaces[6] = {
    {2, 1, {0, 0, 1}, 0, 1.0f, 1, -1.0f, {1, 0, 0, 1}},   {2, -1, {0, 0, -1}, 0, -1.0f, 1, -1.0f, {-1, 0, 0, 1}},
    {0, -1, {-1, 0, 0}, 2, 1.0f, 1, -1.0f, {0, 0, 1, 1}}, {0, 1, {1, 0, 0}, 2, -1.0f, 1, -1.0f, {0, 0, -1, 1}},
    {1, 1, {0, 1, 0}, 0, 1.0f, 2, 1.0f, {1, 0, 0, 1}},    {1, -1, {0, -1, 0}, 0, 1.0f, 2, -1.0f, {1, 0, 0, 1}},
};

float AxisValue(const MATH::Vec3& value, int axis) { return axis == 0 ? value.x : (axis == 1 ? value.y : value.z); }

MATH::Vec3 UnitAxis(int axis, float sign) {
    return {axis == 0 ? sign : 0.0f, axis == 1 ? sign : 0.0f, axis == 2 ? sign : 0.0f};
}

// 箱の面全体に対する uv（欠けた面でも模様が続く）。
MATH::Vec2 BoxFaceUv(const BoxFace& face, const MATH::Vec3& point, const MATH::Vec3& half) {
    const float uHalf = AxisValue(half, face.uAxis);
    const float vHalf = AxisValue(half, face.vAxis);
    return {(AxisValue(point, face.uAxis) * face.uSign + uHalf) / (2.0f * uHalf),
            (AxisValue(point, face.vAxis) * face.vSign + vHalf) / (2.0f * vHalf)};
}

// normal の側から見て表になるように回りを揃えた三角形。
void AddOrientedTriangle(MeshPrimitive& primitive, std::array<MATH::Vec3, 3> points, std::array<MATH::Vec2, 3> uvs,
                         std::array<MATH::Vec3, 3> normals, const MATH::Vec3& facing, const MATH::Vec4& tangent) {
    if (MATH::Dot(MATH::Cross(points[1] - points[0], points[2] - points[0]), facing) < 0.0f) {
        std::swap(points[1], points[2]);
        std::swap(uvs[1], uvs[2]);
        std::swap(normals[1], normals[2]);
    }
    const uint32_t base = static_cast<uint32_t>(primitive.staticVertices.size());
    for (size_t index = 0; index < 3; ++index) {
        primitive.staticVertices.push_back(
            MakeVertex(points[index], normals[index], tangent, uvs[index].x, uvs[index].y));
    }
    primitive.indices.insert(primitive.indices.end(), {base, base + 1u, base + 2u});
}

// 丸い穴：外接する正方形の柱を格子から抜いた所に、両端の面の四隅（正方形の角から円弧への扇）と、内向きの円柱の壁を足す。
void AddRoundCutFill(const ProceduralMeshCut& cut, const MATH::Vec3& half, bool tangents, MeshPrimitive& primitive) {
    constexpr int kQuarterSegments = 8;
    constexpr float kHalfPi = 1.57079632679f;
    const int axis = cut.roundAxis;
    const int u = (axis + 1) % 3;
    const int v = (axis + 2) % 3;
    const float radius = AxisValue(cut.size, u) * 0.5f;
    const MATH::Vec3 uDir = UnitAxis(u, 1.0f);
    const MATH::Vec3 vDir = UnitAxis(v, 1.0f);
    const MATH::Vec3 center = UnitAxis(u, AxisValue(cut.center, u)) + UnitAxis(v, AxisValue(cut.center, v));
    const float extent = AxisValue(half, axis);
    const auto arc = [&](float angle) { return center + (uDir * std::cos(angle) + vDir * std::sin(angle)) * radius; };
    for (const BoxFace& face : kBoxFaces) {
        if (face.axis != axis) {
            continue;
        }
        const MATH::Vec3 plane = UnitAxis(axis, static_cast<float>(face.sign) * extent);
        for (int quarter = 0; quarter < 4; ++quarter) {
            // この四分の一の外接正方形の角（扇の要）。
            const float start = kHalfPi * static_cast<float>(quarter);
            const float middle = start + kHalfPi * 0.5f;
            const MATH::Vec3 square =
                plane + center +
                (uDir * (std::cos(middle) >= 0.0f ? 1.0f : -1.0f) + vDir * (std::sin(middle) >= 0.0f ? 1.0f : -1.0f)) *
                    radius;
            for (int segment = 0; segment < kQuarterSegments; ++segment) {
                const float a0 = start + kHalfPi * static_cast<float>(segment) / kQuarterSegments;
                const float a1 = start + kHalfPi * static_cast<float>(segment + 1) / kQuarterSegments;
                const MATH::Vec3 p0 = plane + arc(a0);
                const MATH::Vec3 p1 = plane + arc(a1);
                AddOrientedTriangle(
                    primitive, {square, p0, p1},
                    {BoxFaceUv(face, square, half), BoxFaceUv(face, p0, half), BoxFaceUv(face, p1, half)},
                    {face.normal, face.normal, face.normal}, face.normal, tangents ? face.tangent : MATH::Vec4{});
            }
        }
    }
    // 穴の壁（円の中心を向く）。
    constexpr int kSegments = kQuarterSegments * 4;
    const MATH::Vec3 bottom = UnitAxis(axis, -extent);
    const MATH::Vec3 top = UnitAxis(axis, extent);
    for (int segment = 0; segment < kSegments; ++segment) {
        const float a0 = 4.0f * kHalfPi * static_cast<float>(segment) / kSegments;
        const float a1 = 4.0f * kHalfPi * static_cast<float>(segment + 1) / kSegments;
        const float aMid = (a0 + a1) * 0.5f;
        const MATH::Vec3 inward = (uDir * std::cos(aMid) + vDir * std::sin(aMid)) * -1.0f;
        const MATH::Vec4 tangent = tangents ? MATH::Vec4{-std::sin(aMid) * uDir.x + std::cos(aMid) * vDir.x,
                                                         -std::sin(aMid) * uDir.y + std::cos(aMid) * vDir.y,
                                                         -std::sin(aMid) * uDir.z + std::cos(aMid) * vDir.z, 1.0f}
                                            : MATH::Vec4{};
        const float u0 = static_cast<float>(segment) / kSegments * 4.0f;
        const float u1 = static_cast<float>(segment + 1) / kSegments * 4.0f;
        const MATH::Vec3 b0 = bottom + arc(a0);
        const MATH::Vec3 b1 = bottom + arc(a1);
        const MATH::Vec3 t0 = top + arc(a0);
        const MATH::Vec3 t1 = top + arc(a1);
        // 法線は頂点ごとに円の中心へ向け、壁を滑らかに見せる。
        const MATH::Vec3 n0 = (uDir * std::cos(a0) + vDir * std::sin(a0)) * -1.0f;
        const MATH::Vec3 n1 = (uDir * std::cos(a1) + vDir * std::sin(a1)) * -1.0f;
        AddOrientedTriangle(primitive, {b0, b1, t0}, {MATH::Vec2{u0, 1.0f}, MATH::Vec2{u1, 1.0f}, MATH::Vec2{u0, 0.0f}},
                            {n0, n1, n0}, inward, tangent);
        AddOrientedTriangle(primitive, {b1, t1, t0}, {MATH::Vec2{u1, 1.0f}, MATH::Vec2{u1, 0.0f}, MATH::Vec2{u0, 0.0f}},
                            {n1, n1, n0}, inward, tangent);
    }
}

// 切り抜いた箱。残った升と空き（外を含む）の境の面だけを出し、面の向きと uv は BuildBox と同じ。uv は箱の面全体に
// 対する位置なので、欠けた所の周りでも模様が続く。丸い穴の正方形の柱の内側の面は出さず、四隅と壁を別に作る。
void BuildCutBox(const ProceduralMeshSettings& settings, MeshPrimitive& primitive) {
    const ProceduralCutGrid grid = BuildProceduralCutGrid(settings);
    const MATH::Vec3 half{settings.width * 0.5f, settings.height * 0.5f, settings.depth * 0.5f};
    for (int z = 0; z < grid.Count(2); ++z) {
        for (int y = 0; y < grid.Count(1); ++y) {
            for (int x = 0; x < grid.Count(0); ++x) {
                if (!grid.IsSolid(x, y, z)) {
                    continue;
                }
                const std::array<int, 3> cell{x, y, z};
                for (const BoxFace& face : kBoxFaces) {
                    std::array<int, 3> neighbor = cell;
                    neighbor[static_cast<size_t>(face.axis)] += face.sign;
                    const int carver = grid.CarvedBy(neighbor[0], neighbor[1], neighbor[2]);
                    if (carver == -1 || (carver >= 0 && settings.cuts[static_cast<size_t>(carver)].roundAxis >= 0)) {
                        continue;
                    }
                    const auto edge = [&](int axis, int offset) {
                        return grid.edges[static_cast<size_t>(axis)][static_cast<size_t>(cell[axis] + offset)];
                    };
                    const float plane = edge(face.axis, face.sign > 0 ? 1 : 0);
                    // 面の上での u・v の範囲（符号付きの軸で測る）。
                    const float u0 = (std::min)(edge(face.uAxis, 0) * face.uSign, edge(face.uAxis, 1) * face.uSign);
                    const float u1 = (std::max)(edge(face.uAxis, 0) * face.uSign, edge(face.uAxis, 1) * face.uSign);
                    const float v0 = (std::min)(edge(face.vAxis, 0) * face.vSign, edge(face.vAxis, 1) * face.vSign);
                    const float v1 = (std::max)(edge(face.vAxis, 0) * face.vSign, edge(face.vAxis, 1) * face.vSign);
                    const MATH::Vec3 u = UnitAxis(face.uAxis, face.uSign);
                    const MATH::Vec3 v = UnitAxis(face.vAxis, face.vSign);
                    const MATH::Vec3 origin = UnitAxis(face.axis, plane);
                    const float uHalf = AxisValue(half, face.uAxis);
                    const float vHalf = AxisValue(half, face.vAxis);
                    const MATH::Vec4 uv{(u0 + uHalf) / (2.0f * uHalf), (v0 + vHalf) / (2.0f * vHalf),
                                        (u1 + uHalf) / (2.0f * uHalf), (v1 + vHalf) / (2.0f * vHalf)};
                    AddBoxFace(primitive, origin + u * u0 + v * v1, origin + u * u1 + v * v1, origin + u * u0 + v * v0,
                               origin + u * u1 + v * v0, face.normal,
                               settings.generateTangents ? face.tangent : MATH::Vec4{}, uv);
                }
            }
        }
    }
    for (const ProceduralMeshCut& cut : settings.cuts) {
        if (cut.roundAxis >= 0) {
            AddRoundCutFill(cut, half, settings.generateTangents, primitive);
        }
    }
}

void BuildBox(const ProceduralMeshSettings& settings, MeshPrimitive& primitive) {
    const float x = settings.width * 0.5f;
    const float y = settings.height * 0.5f;
    const float z = settings.depth * 0.5f;
    const auto tangent = [enabled = settings.generateTangents](MATH::Vec4 value) {
        return enabled ? value : MATH::Vec4{};
    };
    AddBoxFace(primitive, {-x, -y, z}, {x, -y, z}, {-x, y, z}, {x, y, z}, {0, 0, 1}, tangent({1, 0, 0, 1}));
    AddBoxFace(primitive, {x, -y, -z}, {-x, -y, -z}, {x, y, -z}, {-x, y, -z}, {0, 0, -1}, tangent({-1, 0, 0, 1}));
    AddBoxFace(primitive, {-x, -y, -z}, {-x, -y, z}, {-x, y, -z}, {-x, y, z}, {-1, 0, 0}, tangent({0, 0, 1, 1}));
    AddBoxFace(primitive, {x, -y, z}, {x, -y, -z}, {x, y, z}, {x, y, -z}, {1, 0, 0}, tangent({0, 0, -1, 1}));
    AddBoxFace(primitive, {-x, y, z}, {x, y, z}, {-x, y, -z}, {x, y, -z}, {0, 1, 0}, tangent({1, 0, 0, 1}));
    AddBoxFace(primitive, {-x, -y, -z}, {x, -y, -z}, {-x, -y, z}, {x, -y, z}, {0, -1, 0}, tangent({1, 0, 0, 1}));
}

void AddTriangle(MeshPrimitive& primitive, MATH::Vec3 a, MATH::Vec3 b, MATH::Vec3 c, MATH::Vec3 normal,
                 MATH::Vec4 tangent) {
    const uint32_t base = static_cast<uint32_t>(primitive.staticVertices.size());
    primitive.staticVertices.push_back(MakeVertex(a, normal, tangent, 0.0f, 1.0f));
    primitive.staticVertices.push_back(MakeVertex(b, normal, tangent, 1.0f, 1.0f));
    primitive.staticVertices.push_back(MakeVertex(c, normal, tangent, 1.0f, 0.0f));
    primitive.indices.insert(primitive.indices.end(), {base, base + 1u, base + 2u});
}

// 手前（-Z）の下端から奥（+Z）の上端へ上がるくさび。
void BuildRamp(const ProceduralMeshSettings& settings, MeshPrimitive& primitive) {
    const float x = settings.width * 0.5f;
    const float y = settings.height * 0.5f;
    const float z = settings.depth * 0.5f;
    const MATH::Vec4 tangentX = settings.generateTangents ? MATH::Vec4{1, 0, 0, 1} : MATH::Vec4{};
    const MATH::Vec4 tangentZ = settings.generateTangents ? MATH::Vec4{0, 0, 1, 1} : MATH::Vec4{};
    const MATH::Vec4 tangentNegZ = settings.generateTangents ? MATH::Vec4{0, 0, -1, 1} : MATH::Vec4{};
    const MATH::Vec3 slopeNormal = MATH::Normalize({0.0f, settings.depth, -settings.height});
    AddBoxFace(primitive, {-x, y, z}, {x, y, z}, {-x, -y, -z}, {x, -y, -z}, slopeNormal, tangentX);
    AddBoxFace(primitive, {-x, -y, z}, {x, -y, z}, {-x, y, z}, {x, y, z}, {0, 0, 1}, tangentX);
    AddBoxFace(primitive, {-x, -y, -z}, {x, -y, -z}, {-x, -y, z}, {x, -y, z}, {0, -1, 0}, tangentX);
    AddTriangle(primitive, {-x, -y, -z}, {-x, -y, z}, {-x, y, z}, {-1, 0, 0}, tangentZ);
    AddTriangle(primitive, {x, -y, z}, {x, -y, -z}, {x, y, z}, {1, 0, 0}, tangentNegZ);
}

// 斜面と同じ向きに segmentsY 段で上がる階段。段ごとに離れた柱を並べる（境目の面は柱の内側に隠れる）。
void BuildStairs(const ProceduralMeshSettings& settings, MeshPrimitive& primitive) {
    const uint32_t steps = (std::max)(settings.segmentsY, 1u);
    const float x = settings.width * 0.5f;
    const float y0 = -settings.height * 0.5f;
    const float front = -settings.depth * 0.5f;
    const float rise = settings.height / static_cast<float>(steps);
    const float run = settings.depth / static_cast<float>(steps);
    const auto tangent = [enabled = settings.generateTangents](MATH::Vec4 value) {
        return enabled ? value : MATH::Vec4{};
    };
    for (uint32_t step = 0; step < steps; ++step) {
        const float y1 = y0 + rise * static_cast<float>(step + 1u);
        const float z0 = front + run * static_cast<float>(step);
        const float z1 = z0 + run;
        AddBoxFace(primitive, {-x, y0, z1}, {x, y0, z1}, {-x, y1, z1}, {x, y1, z1}, {0, 0, 1}, tangent({1, 0, 0, 1}));
        AddBoxFace(primitive, {x, y0, z0}, {-x, y0, z0}, {x, y1, z0}, {-x, y1, z0}, {0, 0, -1}, tangent({-1, 0, 0, 1}));
        AddBoxFace(primitive, {-x, y0, z0}, {-x, y0, z1}, {-x, y1, z0}, {-x, y1, z1}, {-1, 0, 0},
                   tangent({0, 0, 1, 1}));
        AddBoxFace(primitive, {x, y0, z1}, {x, y0, z0}, {x, y1, z1}, {x, y1, z0}, {1, 0, 0}, tangent({0, 0, -1, 1}));
        AddBoxFace(primitive, {-x, y1, z1}, {x, y1, z1}, {-x, y1, z0}, {x, y1, z0}, {0, 1, 0}, tangent({1, 0, 0, 1}));
        AddBoxFace(primitive, {-x, y0, z0}, {x, y0, z0}, {-x, y0, z1}, {x, y0, z1}, {0, -1, 0}, tangent({1, 0, 0, 1}));
    }
}

void BuildRoundedVerticalShape(const ProceduralMeshSettings& settings, MeshPrimitive& primitive, bool capsule) {
    const uint32_t slices = settings.sphereSlices;
    const float radius = settings.width * 0.5f;
    const float halfCylinder = capsule ? (settings.height - settings.width) * 0.5f : 0.0f;
    const bool hasCylinderSection = capsule && halfCylinder > 1e-6f;
    const uint32_t ringCount = capsule
                                   ? (hasCylinderSection ? settings.sphereStacks * 2u : settings.sphereStacks * 2u - 1u)
                                   : settings.sphereStacks - 1u;
    primitive.staticVertices.reserve(2u + static_cast<size_t>(slices + 1u) * static_cast<size_t>(ringCount));

    const MATH::Vec4 poleTangent = settings.generateTangents ? MATH::Vec4{1.0f, 0.0f, 0.0f, 1.0f} : MATH::Vec4{};
    primitive.staticVertices.push_back(
        MakeVertex({0.0f, halfCylinder + radius, 0.0f}, {0.0f, 1.0f, 0.0f}, poleTangent, 0.5f, 0.0f));

    if (!capsule || !hasCylinderSection) {
        const uint32_t verticalSegments = capsule ? settings.sphereStacks * 2u : settings.sphereStacks;
        for (uint32_t ring = 1u; ring < verticalSegments; ++ring) {
            const float v = static_cast<float>(ring) / static_cast<float>(verticalSegments);
            const float phi = v * kPi;
            AppendRoundedRing(primitive, slices, radius, 0.0f, std::sin(phi), std::cos(phi), v,
                              settings.generateTangents);
        }
    } else {
        const uint32_t hemisphereSegments = settings.sphereStacks;
        const float totalHeight = halfCylinder * 2.0f + radius * 2.0f;
        const float topY = halfCylinder + radius;

        for (uint32_t step = 1u; step <= hemisphereSegments; ++step) {
            const float angle = static_cast<float>(step) / static_cast<float>(hemisphereSegments) * (kPi * 0.5f);
            const float normalY = std::cos(angle);
            const float y = halfCylinder + normalY * radius;
            AppendRoundedRing(primitive, slices, radius, halfCylinder, std::sin(angle), normalY,
                              (topY - y) / totalHeight, settings.generateTangents);
        }

        for (uint32_t step = 0u; step < hemisphereSegments; ++step) {
            const float angle = static_cast<float>(step) / static_cast<float>(hemisphereSegments) * (kPi * 0.5f);
            const float normalY = -std::sin(angle);
            const float y = -halfCylinder + normalY * radius;
            AppendRoundedRing(primitive, slices, radius, -halfCylinder, std::cos(angle), normalY,
                              (topY - y) / totalHeight, settings.generateTangents);
        }
    }

    const uint32_t bottomPole = static_cast<uint32_t>(primitive.staticVertices.size());
    primitive.staticVertices.push_back(
        MakeVertex({0.0f, -halfCylinder - radius, 0.0f}, {0.0f, -1.0f, 0.0f}, poleTangent, 0.5f, 1.0f));

    const uint32_t rowSize = slices + 1u;
    const uint32_t firstRing = 1u;
    for (uint32_t slice = 0; slice < slices; ++slice) {
        primitive.indices.insert(primitive.indices.end(), {0u, firstRing + slice + 1u, firstRing + slice});
    }
    for (uint32_t ring = 0; ring + 1u < ringCount; ++ring) {
        for (uint32_t slice = 0; slice < slices; ++slice) {
            const uint32_t i0 = firstRing + ring * rowSize + slice;
            const uint32_t i1 = i0 + 1u;
            const uint32_t i2 = i0 + rowSize;
            const uint32_t i3 = i2 + 1u;
            primitive.indices.insert(primitive.indices.end(), {i0, i1, i2, i1, i3, i2});
        }
    }
    const uint32_t lastRing = firstRing + (ringCount - 1u) * rowSize;
    for (uint32_t slice = 0; slice < slices; ++slice) {
        primitive.indices.insert(primitive.indices.end(), {lastRing + slice, lastRing + slice + 1u, bottomPole});
    }
}

void BuildCylinderCap(const ProceduralMeshSettings& settings, MeshPrimitive& primitive, float y, float normalY) {
    const uint32_t slices = settings.sphereSlices;
    const float radius = settings.width * 0.5f;
    const MATH::Vec4 tangent = settings.generateTangents ? MATH::Vec4{1.0f, 0.0f, 0.0f, 1.0f} : MATH::Vec4{};
    const uint32_t center = static_cast<uint32_t>(primitive.staticVertices.size());
    primitive.staticVertices.push_back(MakeVertex({0.0f, y, 0.0f}, {0.0f, normalY, 0.0f}, tangent, 0.5f, 0.5f));
    for (uint32_t slice = 0; slice <= slices; ++slice) {
        const float u = static_cast<float>(slice) / static_cast<float>(slices);
        const float theta = u * kTwoPi;
        const float x = radius * std::cos(theta);
        const float z = radius * std::sin(theta);
        primitive.staticVertices.push_back(MakeVertex({x, y, z}, {0.0f, normalY, 0.0f}, tangent,
                                                      0.5f + x / (radius * 2.0f), 0.5f + z / (radius * 2.0f)));
    }
    for (uint32_t slice = 0; slice < slices; ++slice) {
        const uint32_t current = center + 1u + slice;
        const uint32_t next = current + 1u;
        if (normalY > 0.0f) {
            primitive.indices.insert(primitive.indices.end(), {center, next, current});
        } else {
            primitive.indices.insert(primitive.indices.end(), {center, current, next});
        }
    }
}

void BuildCylinder(const ProceduralMeshSettings& settings, MeshPrimitive& primitive) {
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
            const MATH::Vec3 normal{std::cos(theta), 0.0f, std::sin(theta)};
            primitive.staticVertices.push_back(MakeVertex({normal.x * radius, y, normal.z * radius}, normal,
                                                          MakeRadialTangent(theta, settings.generateTangents), u,
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
            primitive.indices.insert(primitive.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    BuildCylinderCap(settings, primitive, halfHeight, 1.0f);
    BuildCylinderCap(settings, primitive, -halfHeight, -1.0f);
}
} // namespace

void BuildProceduralMesh(const ProceduralMeshSettings& rawSettings, MeshPrimitive& outPrimitive) {
    const ProceduralMeshSettings settings = SanitizeProceduralMeshSettings(rawSettings);
    switch (settings.kind) {
    case ProceduralMeshKind::Plane:
    case ProceduralMeshKind::GridPlane: BuildGridPlane(settings, outPrimitive); break;
    case ProceduralMeshKind::Sphere: BuildRoundedVerticalShape(settings, outPrimitive, false); break;
    case ProceduralMeshKind::Cylinder: BuildCylinder(settings, outPrimitive); break;
    case ProceduralMeshKind::Capsule: BuildRoundedVerticalShape(settings, outPrimitive, true); break;
    case ProceduralMeshKind::Ramp: BuildRamp(settings, outPrimitive); break;
    case ProceduralMeshKind::Stairs: BuildStairs(settings, outPrimitive); break;
    case ProceduralMeshKind::Box:
    default:
        if (settings.cuts.empty()) {
            BuildBox(settings, outPrimitive);
        } else {
            BuildCutBox(settings, outPrimitive);
        }
        break;
    }
}

} // namespace HIKARI::PROCEDURAL
