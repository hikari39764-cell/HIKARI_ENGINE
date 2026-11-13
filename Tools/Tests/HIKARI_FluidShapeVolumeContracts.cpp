#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include "Render3D/Vfx/HIKARI_FluidShapeVolume.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Tools/Tests/Internal/HIKARI_ContractAssert.h"

// 流体源の形の距離の体積（模型の三角形から CPU で焼く）：箱の模型の内外と表面近くの距離、重なった殻の内側、
// 節点の変換、空の模型。最後に約 600 / 20,000 三角形の球（丸いものと、剣のように細長く潰したもの）を焼く時間を
// 表示する（時間では落とさない）。
namespace {
using namespace HIKARI;
using namespace RENDER3D::FLUID;
using MATH::Vec3;

struct CellIndex {
    uint32_t x = 0u, y = 0u, z = 0u;
};

float CellSize(const FluidShapeVolume& volume) {
    return (volume.boundsMax.x - volume.boundsMin.x) / static_cast<float>(volume.cells[0]);
}

Vec3 CellCentre(const FluidShapeVolume& volume, CellIndex cell) {
    const float size = CellSize(volume);
    return volume.boundsMin + Vec3{static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y) + 0.5f,
                                   static_cast<float>(cell.z) + 0.5f} *
                                  size;
}

// 点を含む格。
CellIndex CellAt(const FluidShapeVolume& volume, const Vec3& point) {
    const float size = CellSize(volume);
    const Vec3 local = (point - volume.boundsMin) * (1.0f / size);
    assert(local.x >= 0.0f && local.y >= 0.0f && local.z >= 0.0f);
    return {static_cast<uint32_t>(local.x), static_cast<uint32_t>(local.y), static_cast<uint32_t>(local.z)};
}

int Value(const FluidShapeVolume& volume, CellIndex cell) {
    assert(cell.x < volume.cells[0] && cell.y < volume.cells[1] && cell.z < volume.cells[2]);
    return volume.distance[cell.x + volume.cells[0] * (cell.y + volume.cells[1] * cell.z)];
}

// 符号付き距離 [m] から期待する量子化の値。
int Expected(const FluidShapeVolume& volume, float signedDistance) {
    const float normalized = (std::max)(-1.0f, (std::min)(1.0f, signedDistance / volume.band));
    return static_cast<int>(std::lround(normalized * 127.0f));
}

// 一辺 1 m、原点が中心の箱（外向きの 12 三角形）。
MeshAsset MakeCube() {
    MeshPrimitive primitive{};
    for (uint32_t corner = 0u; corner < 8u; ++corner) {
        Vertex3D vertex{};
        vertex.position = {(corner & 1u) ? 0.5f : -0.5f, (corner & 2u) ? 0.5f : -0.5f, (corner & 4u) ? 0.5f : -0.5f};
        primitive.staticVertices.push_back(vertex);
    }
    primitive.indices = {0, 4, 6, 0, 6, 2, 1, 3, 7, 1, 7, 5, 0, 1, 5, 0, 5, 4,
                         2, 6, 7, 2, 7, 3, 0, 2, 3, 0, 3, 1, 4, 5, 7, 4, 7, 6};
    MeshAsset mesh{};
    mesh.primitives.push_back(std::move(primitive));
    return mesh;
}

// 経緯の格子の球（極の列の潰れた三角形も含む。焼く側が捨てる）。三角形の数は 2 × slices × stacks。
MeshAsset MakeSphere(float radius, uint32_t slices, uint32_t stacks) {
    MeshPrimitive primitive{};
    for (uint32_t stack = 0u; stack <= stacks; ++stack) {
        const float polar = 3.14159265f * static_cast<float>(stack) / static_cast<float>(stacks);
        for (uint32_t slice = 0u; slice <= slices; ++slice) {
            const float azimuth = 6.28318531f * static_cast<float>(slice) / static_cast<float>(slices);
            Vertex3D vertex{};
            vertex.position =
                Vec3{std::sin(polar) * std::cos(azimuth), std::cos(polar), std::sin(polar) * std::sin(azimuth)} *
                radius;
            primitive.staticVertices.push_back(vertex);
        }
    }
    for (uint32_t stack = 0u; stack < stacks; ++stack) {
        for (uint32_t slice = 0u; slice < slices; ++slice) {
            const uint32_t a = stack * (slices + 1u) + slice;
            const uint32_t b = a + slices + 1u;
            primitive.indices.insert(primitive.indices.end(), {a, a + 1u, b, a + 1u, b + 1u, b});
        }
    }
    MeshAsset mesh{};
    mesh.primitives.push_back(std::move(primitive));
    return mesh;
}

ModelAsset MakeModel(MeshAsset mesh, const std::vector<Transform3D>& nodes) {
    ModelAsset model{};
    model.meshes.push_back(std::move(mesh));
    for (const Transform3D& transform : nodes) {
        ModelNode node{};
        node.localTransform = transform;
        node.meshIndex = 0;
        model.nodes.push_back(node);
    }
    return model;
}

void CheckCube() {
    const auto volume = BakeFluidShapeVolume(MakeModel(MakeCube(), {Transform3D{}}));
    assert(volume);
    assert(volume->distance.size() == static_cast<size_t>(volume->cells[0]) * volume->cells[1] * volume->cells[2]);
    const float size = CellSize(*volume);
    assert(std::abs(size - 1.0f / 128.0f) < 1.0e-6f);
    assert(std::abs(volume->band - kFluidShapeVolumeBandCells * size) < 1.0e-6f);
    assert(Value(*volume, CellAt(*volume, {})) == -127);

    // 中心を通る x の列：箱の中は負、外は正、表面から band の内側は距離どおり。
    const CellIndex middle = CellAt(*volume, {});
    for (uint32_t x = 0u; x < volume->cells[0]; ++x) {
        const CellIndex cell{x, middle.y, middle.z};
        const float centreX = CellCentre(*volume, cell).x;
        const float outside = std::abs(centreX) - 0.5f;
        const int value = Value(*volume, cell);
        if (std::abs(outside) > 1.0e-4f)
            assert((outside < 0.0f) == (value < 0));
        if (outside > 0.0f)
            assert(std::abs(value - Expected(*volume, outside)) <= 1);
    }
    // 面のすぐ外の格（表面から 1 格）。
    const CellIndex justOutside = CellAt(*volume, {0.5f + 1.5f * size, 0.0f, 0.0f});
    const float gap = CellCentre(*volume, justOutside).x - 0.5f;
    assert(gap > 0.0f && gap < volume->band);
    assert(std::abs(Value(*volume, justOutside) - Expected(*volume, gap)) <= 1);

    // 箱の六つの面の格はどれも +1。
    for (uint32_t z = 0u; z < volume->cells[2]; ++z)
        for (uint32_t y = 0u; y < volume->cells[1]; ++y)
            for (uint32_t x = 0u; x < volume->cells[0]; ++x) {
                const bool face = x == 0u || y == 0u || z == 0u || x + 1u == volume->cells[0] ||
                                  y + 1u == volume->cells[1] || z + 1u == volume->cells[2];
                if (face)
                    assert(Value(*volume, {x, y, z}) == 127);
            }
}

void CheckOverlappingShells() {
    Transform3D shifted{};
    shifted.position = {0.5f, 0.0f, 0.0f};
    const auto volume = BakeFluidShapeVolume(MakeModel(MakeCube(), {Transform3D{}, shifted}));
    assert(volume);
    // 二つの箱が重なった所（巻き数 2）も内側。中の面（他方の箱の中にある面）の近くも内側のまま。
    assert(Value(*volume, CellAt(*volume, {0.25f, 0.0f, 0.0f})) == -127);
    assert(Value(*volume, CellAt(*volume, {-0.3f, 0.2f, 0.0f})) == -127);
    assert(Value(*volume, CellAt(*volume, {0.8f, 0.0f, 0.2f})) == -127);
    const CellIndex middle = CellAt(*volume, {0.25f, 0.0f, 0.0f});
    for (uint32_t x = 0u; x < volume->cells[0]; ++x) {
        const CellIndex cell{x, middle.y, middle.z};
        const float centreX = CellCentre(*volume, cell).x;
        if (std::abs(centreX + 0.5f) > 1.0e-4f && std::abs(centreX - 1.0f) > 1.0e-4f)
            assert((centreX > -0.5f && centreX < 1.0f) == (Value(*volume, cell) < 0));
    }
    assert(Value(*volume, CellAt(*volume, {0.25f, 0.5f + 2.0f * CellSize(*volume), 0.0f})) > 0);
}

void CheckNodeTransform() {
    // 平行移動と軸ごとに違う拡大：箱は x [1, 3]、y [−1.25, −0.75]、z [2.5, 3.5]。
    Transform3D transform{};
    transform.position = {2.0f, -1.0f, 3.0f};
    transform.scale = {2.0f, 0.5f, 1.0f};
    const auto volume = BakeFluidShapeVolume(MakeModel(MakeCube(), {transform}));
    assert(volume);
    const float size = CellSize(*volume);
    assert(std::abs(size - 2.0f / 128.0f) < 1.0e-6f);
    assert(volume->boundsMin.x < 1.0f - volume->band && volume->boundsMax.x > 3.0f + volume->band);
    assert(volume->boundsMin.y < -1.25f - volume->band && volume->boundsMax.y > -0.75f + volume->band);
    assert(Value(*volume, CellAt(*volume, {2.0f, -1.0f, 3.0f})) < 0);

    // +x の面（x = 3）を横切る列と、薄い y の面（y = −0.75）を横切る列。
    const CellIndex xRow = CellAt(*volume, {2.0f, -1.0f, 3.0f});
    for (uint32_t x = 0u; x < volume->cells[0]; ++x) {
        const CellIndex cell{x, xRow.y, xRow.z};
        const float offset = CellCentre(*volume, cell).x - 3.0f;
        if (offset > 0.0f)
            assert(std::abs(Value(*volume, cell) - Expected(*volume, offset)) <= 1);
        else if (offset < -1.0e-4f && CellCentre(*volume, cell).x > 2.0f)
            assert(Value(*volume, cell) < 0);
    }
    for (uint32_t y = 0u; y < volume->cells[1]; ++y) {
        const CellIndex cell{xRow.x, y, xRow.z};
        const float offset = CellCentre(*volume, cell).y + 0.75f;
        if (offset > 0.0f)
            assert(std::abs(Value(*volume, cell) - Expected(*volume, offset)) <= 1);
        else if (offset < -1.0e-4f && CellCentre(*volume, cell).y > -1.0f)
            assert(Value(*volume, cell) < 0);
    }
}

void CheckEmptyModel() {
    assert(!BakeFluidShapeVolume(ModelAsset{}));
    // 節点の無い網は模型の形に入らない。
    ModelAsset meshOnly{};
    meshOnly.meshes.push_back(MakeCube());
    assert(!BakeFluidShapeVolume(meshOnly));
}

// scale は球を置く節点の拡大（細長くすると剣のような体積になる）。
void ReportSphereBakeTime(uint32_t slices, uint32_t stacks, const Vec3& scale) {
    Transform3D transform{};
    transform.scale = scale;
    const ModelAsset model = MakeModel(MakeSphere(1.0f, slices, stacks), {transform});
    const auto start = std::chrono::steady_clock::now();
    const auto volume = BakeFluidShapeVolume(model);
    const auto stop = std::chrono::steady_clock::now();
    assert(volume);
    assert(Value(*volume, CellAt(*volume, {})) == -127);
    assert(Value(*volume, CellAt(*volume, {0.7f * scale.x, 0.0f, 0.0f})) < 0);
    assert(Value(*volume, CellAt(*volume, {scale.x + 2.0f * CellSize(*volume), 0.0f, 0.0f})) > 0);
    std::printf("Fluid shape volume: %u triangles, %u x %u x %u cells, %.2f ms\n", 2u * slices * stacks,
                volume->cells[0], volume->cells[1], volume->cells[2],
                std::chrono::duration<double, std::milli>(stop - start).count());
}

} // namespace

int main() {
    CheckCube();
    CheckOverlappingShells();
    CheckNodeTransform();
    CheckEmptyModel();
    ReportSphereBakeTime(20u, 15u, {3.5f, 1.0f, 0.3f});
    ReportSphereBakeTime(20u, 15u, {1.0f, 1.0f, 1.0f});
    ReportSphereBakeTime(100u, 100u, {3.5f, 1.0f, 0.3f});
    ReportSphereBakeTime(100u, 100u, {1.0f, 1.0f, 1.0f});
    std::printf("Fluid shape volume contracts passed.\n");
    return EXIT_SUCCESS;
}
