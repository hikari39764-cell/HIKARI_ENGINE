#include "Render3D/Vfx/HIKARI_FluidShapeVolume.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <emmintrin.h>

#include "RenderContracts/Model/HIKARI_ModelBounds.h"

// 模型の三角形から流体源の形の距離の体積を焼く（CPU、単線程）。距離は表面の近く（band の内側）の格だけで量り、
// 内外は三つの軸の光線の巻き数の多数決で決める。計算は格の座標（格 i の中心が整数 i、長さの単位が 1 格）で行う。
namespace HIKARI::RENDER3D::FLUID {
namespace {

using MATH::Vec3;

struct Triangle {
    Vec3 corner[3];
};

// 一本の光線（列）と三角形の交差。t は光線の向きの格の座標、winding は入る交差なら +1、出る交差なら −1。
struct Crossing {
    uint32_t column = 0u;
    float t = 0.0f;
    int32_t winding = 0;
};

// 軸ごとの交差の表（三軸で使い回す）。found は見つけた順、sorted は列ごとに並べ直したもの（列 c の交差は
// sorted[first[c]] から sorted[first[c + 1]] の手前まで）。
struct CrossingLists {
    std::vector<Crossing> found;
    std::vector<Crossing> sorted;
    std::vector<uint32_t> first;
};

struct Point2 {
    float u = 0.0f;
    float v = 0.0f;
};

float Component(const Vec3& value, int axis) { return axis == 0 ? value.x : (axis == 1 ? value.y : value.z); }

float LengthSquared(const Vec3& value) { return MATH::Dot(value, value); }

void AppendPrimitiveTriangles(const MeshPrimitive& primitive, const MATH::Mat4& transform, std::vector<Vec3>& positions,
                              std::vector<Triangle>& triangles) {
    // 蒙皮の網も静止の姿勢（頂点そのものの位置）で読む。輪郭は形の目安で、骨の変形は追わない。
    positions.clear();
    if (primitive.layout == VertexLayoutKind::SkinnedPNTTJW) {
        for (const SkinnedVertex3D& vertex : primitive.skinnedVertices)
            positions.push_back(MATH::TransformPoint(transform, vertex.position));
    } else {
        for (const Vertex3D& vertex : primitive.staticVertices)
            positions.push_back(MATH::TransformPoint(transform, vertex.position));
    }
    const size_t vertexCount = positions.size();
    const size_t cornerCount = primitive.indices.empty() ? vertexCount : primitive.indices.size();
    for (size_t first = 0u; first + 3u <= cornerCount; first += 3u) {
        Triangle triangle{};
        bool valid = true;
        for (size_t k = 0u; k < 3u && valid; ++k) {
            const size_t index = primitive.indices.empty() ? first + k : primitive.indices[first + k];
            valid = index < vertexCount;
            if (valid)
                triangle.corner[k] = positions[index];
        }
        if (!valid)
            continue;
        // 潰れた三角形（二辺の成す角が無いに等しい）は法線の向きが決まらず、巻き数を乱すだけなので捨てる。
        const Vec3 ab = triangle.corner[1] - triangle.corner[0];
        const Vec3 ac = triangle.corner[2] - triangle.corner[0];
        if (LengthSquared(MATH::Cross(ab, ac)) <= 1.0e-10f * LengthSquared(ab) * LengthSquared(ac))
            continue;
        triangles.push_back(triangle);
    }
}

std::vector<Triangle> CollectModelTriangles(const ModelAsset& model) {
    std::vector<Triangle> triangles{};
    std::vector<Vec3> positions{};
    const std::vector<MATH::Mat4> globals = BOUNDS::BuildModelNodeGlobals(model);
    for (size_t nodeIndex = 0u; nodeIndex < model.nodes.size(); ++nodeIndex) {
        const int meshIndex = model.nodes[nodeIndex].meshIndex;
        if (meshIndex < 0 || meshIndex >= static_cast<int>(model.meshes.size()))
            continue;
        for (const MeshPrimitive& primitive : model.meshes[static_cast<size_t>(meshIndex)].primitives)
            AppendPrimitiveTriangles(primitive, globals[nodeIndex], positions, triangles);
    }
    return triangles;
}

// 4 つの格（同じ行の x … x + 3）から一つの三角形までの距離の二乗。面への足が三角形の内側に落ちれば面からの距離、
// 外なら三辺（線分）までの距離の最小で、Ericson（Real-Time Collision Detection 5.1.5）の場合分けと同じ値を分岐なしで
// 求める。網が細かいと格ごとの分岐の予測が外れ続けて遅いので、SSE2 で 4 格ずつ同じ式を通す。点ごとの内積はどれも
// x の一次式なので、三角形ごと・行ごとの分を前もって足し込み、格ごとには x の項だけを足す。
class RowDistance {
public:
    explicit RowDistance(const Triangle& triangle) {
        const Vec3& a = triangle.corner[0];
        ab_ = triangle.corner[1] - a;
        ac_ = triangle.corner[2] - a;
        bc_ = triangle.corner[2] - triangle.corner[1];
        a_ = a;
        const Vec3 cross = MATH::Cross(ab_, ac_);
        normal_ = cross * (1.0f / std::sqrt(LengthSquared(cross)));
        const float d00 = MATH::Dot(ab_, ab_);
        const float d01 = MATH::Dot(ab_, ac_);
        const float d11 = MATH::Dot(ac_, ac_);
        const float dbc = LengthSquared(bc_);
        d00_ = _mm_set1_ps(d00);
        d01_ = _mm_set1_ps(d01);
        d11_ = _mm_set1_ps(d11);
        dbc_ = _mm_set1_ps(dbc);
        inverseAb_ = _mm_set1_ps(1.0f / d00);
        inverseAc_ = _mm_set1_ps(1.0f / d11);
        inverseBc_ = _mm_set1_ps(1.0f / dbc);
        inverseDenominator_ = _mm_set1_ps(1.0f / (d00 * d11 - d01 * d01));
        abx_ = _mm_set1_ps(ab_.x);
        acx_ = _mm_set1_ps(ac_.x);
        bcx_ = _mm_set1_ps(bc_.x);
        normalX_ = _mm_set1_ps(normal_.x);
        ax_ = _mm_set1_ps(a.x);
    }

    const Vec3& Normal() const { return normal_; }

    // 行（y, z）の分。rowPlane は x = 0 での面からの符号付き距離。
    void BeginRow(float y, float z, float rowPlane) {
        const float apy = y - a_.y;
        const float apz = z - a_.z;
        d1Row_ = _mm_set1_ps(ab_.y * apy + ab_.z * apz - ab_.x * a_.x);
        d2Row_ = _mm_set1_ps(ac_.y * apy + ac_.z * apz - ac_.x * a_.x);
        e3Row_ = _mm_set1_ps(bc_.y * (apy - ab_.y) + bc_.z * (apz - ab_.z) - bc_.x * (a_.x + ab_.x));
        yzSquared_ = _mm_set1_ps(apy * apy + apz * apz);
        planeRow_ = _mm_set1_ps(rowPlane);
    }

    __m128 DistanceSquared(__m128 x) const {
        const __m128 zero = _mm_setzero_ps();
        const __m128 one = _mm_set1_ps(1.0f);
        const __m128 apx = _mm_sub_ps(x, ax_);
        const __m128 d1 = _mm_add_ps(d1Row_, _mm_mul_ps(abx_, x)); // ab·ap
        const __m128 d2 = _mm_add_ps(d2Row_, _mm_mul_ps(acx_, x)); // ac·ap
        const __m128 e3 = _mm_add_ps(e3Row_, _mm_mul_ps(bcx_, x)); // bc·bp
        const __m128 ap2 = _mm_add_ps(_mm_mul_ps(apx, apx), yzSquared_);
        const __m128 plane = _mm_add_ps(_mm_mul_ps(normalX_, x), planeRow_);
        // 面への足の重心座標 (v, w)。どれかが三角形の外へ出た量が 0 以下なら内側。
        const __m128 v = _mm_mul_ps(_mm_sub_ps(_mm_mul_ps(d11_, d1), _mm_mul_ps(d01_, d2)), inverseDenominator_);
        const __m128 w = _mm_mul_ps(_mm_sub_ps(_mm_mul_ps(d00_, d2), _mm_mul_ps(d01_, d1)), inverseDenominator_);
        const __m128 outside =
            _mm_max_ps(_mm_max_ps(_mm_sub_ps(zero, v), _mm_sub_ps(zero, w)), _mm_sub_ps(_mm_add_ps(v, w), one));
        const __m128 inside = _mm_cmple_ps(outside, zero);
        // 線分 p + t·e（t は 0〜1 に切り詰め）までの距離の二乗 = |p|² − t (2 p·e − t |e|²)。
        const auto segment = [&](__m128 pp, __m128 pe, __m128 ee, __m128 inverseEe) {
            const __m128 t = _mm_min_ps(_mm_max_ps(_mm_mul_ps(pe, inverseEe), zero), one);
            return _mm_sub_ps(pp, _mm_mul_ps(t, _mm_sub_ps(_mm_add_ps(pe, pe), _mm_mul_ps(t, ee))));
        };
        const __m128 bp2 = _mm_add_ps(_mm_sub_ps(ap2, _mm_add_ps(d1, d1)), d00_);
        const __m128 edge =
            _mm_min_ps(_mm_min_ps(segment(ap2, d1, d00_, inverseAb_), segment(ap2, d2, d11_, inverseAc_)),
                       segment(bp2, e3, dbc_, inverseBc_));
        const __m128 distance = _mm_or_ps(_mm_and_ps(inside, _mm_mul_ps(plane, plane)), _mm_andnot_ps(inside, edge));
        // 引き算の丸めで僅かに負になった値を 0 に戻す。
        return _mm_max_ps(distance, zero);
    }

private:
    Vec3 a_, ab_, ac_, bc_, normal_;
    __m128 d00_, d01_, d11_, dbc_, inverseAb_, inverseAc_, inverseBc_, inverseDenominator_;
    __m128 abx_, acx_, bcx_, normalX_, ax_;
    __m128 d1Row_{}, d2Row_{}, e3Row_{}, yzSquared_{}, planeRow_{};
};

// 各三角形の近く（band の内側）の格の距離の二乗の最小を残す。訪ねるのは、band ぶん広げた包围盒、外接円を band ぶん
// 太らせた球、面から band の内側の板の重なり（板は行ごとに x の区間で切れる）。行の終わりの 4 格の組は、行の外の格を
// 元の値のまま書き戻す（distanceSquared は末尾に 3 格の余白を持つ）。
void MeasureDistances(const std::vector<Triangle>& triangles, const uint32_t cells[3], float band,
                      std::vector<float>& distanceSquared) {
    const auto range = [&](int axis, float from, float to, int& first, int& last) {
        first = (std::max)(0, static_cast<int>(std::ceil(from)));
        last = (std::min)(static_cast<int>(cells[axis]) - 1, static_cast<int>(std::floor(to)));
    };
    const __m128 laneOffset = _mm_setr_ps(0.0f, 1.0f, 2.0f, 3.0f);
    for (const Triangle& triangle : triangles) {
        RowDistance distance(triangle);
        const Vec3& a = triangle.corner[0];
        const Vec3& b = triangle.corner[1];
        const Vec3& c = triangle.corner[2];
        const Vec3 centre = (a + b + c) * (1.0f / 3.0f);
        const float radiusSquared =
            (std::max)(LengthSquared(a - centre), (std::max)(LengthSquared(b - centre), LengthSquared(c - centre)));
        const float reach = std::sqrt(radiusSquared) + band;
        const Vec3 low{(std::min)({a.x, b.x, c.x}) - band, (std::min)({a.y, b.y, c.y}) - band,
                       (std::min)({a.z, b.z, c.z}) - band};
        const Vec3 high{(std::max)({a.x, b.x, c.x}) + band, (std::max)({a.y, b.y, c.y}) + band,
                        (std::max)({a.z, b.z, c.z}) + band};
        const Vec3& normal = distance.Normal();

        int z0 = 0, z1 = 0;
        range(2, (std::max)(low.z, centre.z - reach), (std::min)(high.z, centre.z + reach), z0, z1);
        for (int z = z0; z <= z1; ++z) {
            const float fz = static_cast<float>(z);
            const float restZ = (std::max)(reach * reach - (fz - centre.z) * (fz - centre.z), 0.0f);
            const float halfY = std::sqrt(restZ);
            int y0 = 0, y1 = 0;
            range(1, (std::max)(low.y, centre.y - halfY), (std::min)(high.y, centre.y + halfY), y0, y1);
            for (int y = y0; y <= y1; ++y) {
                const float fy = static_cast<float>(y);
                const float halfX = std::sqrt((std::max)(restZ - (fy - centre.y) * (fy - centre.y), 0.0f));
                const float rowPlane = normal.y * (fy - a.y) + normal.z * (fz - a.z) - normal.x * a.x;
                float from = (std::max)(low.x, centre.x - halfX);
                float to = (std::min)(high.x, centre.x + halfX);
                if (std::abs(normal.x) > 1.0e-6f) {
                    const float enter = (-band - rowPlane) / normal.x;
                    const float leave = (band - rowPlane) / normal.x;
                    from = (std::max)(from, (std::min)(enter, leave));
                    to = (std::min)(to, (std::max)(enter, leave));
                } else if (std::abs(rowPlane) >= band) {
                    continue;
                }
                int x0 = 0, x1 = 0;
                range(0, from, to, x0, x1);
                if (x0 > x1)
                    continue;
                distance.BeginRow(fy, fz, rowPlane);
                float* row = distanceSquared.data() + static_cast<size_t>(cells[0]) *
                                                          (static_cast<size_t>(y) + cells[1] * static_cast<size_t>(z));
                for (int x = x0; x <= x1; x += 4) {
                    const __m128 inRow = _mm_cmple_ps(laneOffset, _mm_set1_ps(static_cast<float>(x1 - x)));
                    const __m128 measured =
                        distance.DistanceSquared(_mm_add_ps(_mm_set1_ps(static_cast<float>(x)), laneOffset));
                    const __m128 kept = _mm_loadu_ps(row + x);
                    const __m128 nearer = _mm_min_ps(kept, measured);
                    _mm_storeu_ps(row + x, _mm_or_ps(_mm_and_ps(inRow, nearer), _mm_andnot_ps(inRow, kept)));
                }
            }
        }
    }
}

// 投影した三角形の一辺 from→to の辺の関数（点が辺の左なら正）。端点を決まった順に並べてから式を立てるので、
// 隣の三角形が同じ辺を逆向きに持つと値はちょうど符号違いになり、光線が辺の上を通っても二つの三角形の判定が
// 食い違わない。値がちょうど 0 のとき交差を持つのは、辺を決まった向き（owns）に持つ側の三角形だけ（半開の規則）。
class ProjectedEdge {
public:
    ProjectedEdge(Point2 from, Point2 to) {
        const bool reversed = to.u < from.u || (to.u == from.u && to.v < from.v);
        origin_ = reversed ? to : from;
        const Point2 end = reversed ? from : to;
        du_ = end.u - origin_.u;
        dv_ = end.v - origin_.v;
        sign_ = reversed ? -1.0f : 1.0f;
        const float directedU = to.u - from.u;
        const float directedV = to.v - from.v;
        owns_ = directedV < 0.0f || (directedV == 0.0f && directedU > 0.0f);
    }

    float Evaluate(Point2 sample) const {
        return sign_ * (du_ * (sample.v - origin_.v) - dv_ * (sample.u - origin_.u));
    }
    bool Contains(float value) const { return value > 0.0f || (value == 0.0f && owns_); }

private:
    Point2 origin_{};
    float du_ = 0.0f, dv_ = 0.0f, sign_ = 1.0f;
    bool owns_ = false;
};

// axis の向きの光線を列（他の二軸の格）ごとに一本通し、格の中心より手前の交差の巻き数が 0 でない格に一票入れる。
// 光線は列の中心から格の無理数分の一だけずらし、格の上にちょうど並んだ頂点や辺（箱型の模型）を踏まないようにする。
// 列の番号は記憶の上で近い軸を u に取り、続く列の票が同じ記憶の行に入るようにする。
void VoteInside(const std::vector<Triangle>& triangles, const uint32_t cells[3], int axis, CrossingLists& lists,
                std::vector<uint8_t>& votes) {
    constexpr float kNudgeU = 0.000414213562f; // (√2 − 1) / 1000 格
    constexpr float kNudgeV = 0.000732050808f; // (√3 − 1) / 1000 格
    const int u = axis == 0 ? 1 : 0;
    const int v = axis == 2 ? 1 : 2;
    const size_t stride[3] = {1u, cells[0], static_cast<size_t>(cells[0]) * cells[1]};

    std::vector<Crossing>& found = lists.found;
    found.clear();
    for (const Triangle& triangle : triangles) {
        Point2 corner[3]{};
        float along[3]{};
        for (int k = 0; k < 3; ++k) {
            corner[k] = {Component(triangle.corner[k], u), Component(triangle.corner[k], v)};
            along[k] = Component(triangle.corner[k], axis);
        }
        // 投影の面積の符号は法線の axis 成分の符号（(u, v) が axis から巡回の順でない y 軸では逆）。−axis 側を向く面を
        // 入る交差とする（外向きの閉じた殻の内側で +1）。
        const float area = (corner[1].u - corner[0].u) * (corner[2].v - corner[0].v) -
                           (corner[1].v - corner[0].v) * (corner[2].u - corner[0].u);
        if (area == 0.0f)
            continue;
        const int32_t winding = (area < 0.0f) == (axis != 1) ? 1 : -1;
        // 反時計回りに揃え、内側の点で三つの辺の関数がどれも正になるようにする。
        if (area < 0.0f) {
            std::swap(corner[1], corner[2]);
            std::swap(along[1], along[2]);
        }
        // edge[k] は頂点 k の向かいの辺。その辺の関数が頂点 k の重心座標の重み。
        const ProjectedEdge edge[3] = {{corner[1], corner[2]}, {corner[2], corner[0]}, {corner[0], corner[1]}};
        const float lowU = (std::min)({corner[0].u, corner[1].u, corner[2].u}) - kNudgeU;
        const float highU = (std::max)({corner[0].u, corner[1].u, corner[2].u}) - kNudgeU;
        const float lowV = (std::min)({corner[0].v, corner[1].v, corner[2].v}) - kNudgeV;
        const float highV = (std::max)({corner[0].v, corner[1].v, corner[2].v}) - kNudgeV;
        const int u0 = (std::max)(0, static_cast<int>(std::ceil(lowU)));
        const int u1 = (std::min)(static_cast<int>(cells[u]) - 1, static_cast<int>(std::floor(highU)));
        const int v0 = (std::max)(0, static_cast<int>(std::ceil(lowV)));
        const int v1 = (std::min)(static_cast<int>(cells[v]) - 1, static_cast<int>(std::floor(highV)));
        for (int iv = v0; iv <= v1; ++iv) {
            for (int iu = u0; iu <= u1; ++iu) {
                const Point2 sample{static_cast<float>(iu) + kNudgeU, static_cast<float>(iv) + kNudgeV};
                const float w0 = edge[0].Evaluate(sample);
                if (!edge[0].Contains(w0))
                    continue;
                const float w1 = edge[1].Evaluate(sample);
                if (!edge[1].Contains(w1))
                    continue;
                const float w2 = edge[2].Evaluate(sample);
                if (!edge[2].Contains(w2))
                    continue;
                const float sum = w0 + w1 + w2;
                if (sum <= 0.0f)
                    continue;
                const float t = (w0 * along[0] + w1 * along[1] + w2 * along[2]) / sum;
                found.push_back({static_cast<uint32_t>(iu) + cells[u] * static_cast<uint32_t>(iv), t, winding});
            }
        }
    }
    // 列ごとに数えて並べ直し（計数の整列）、列の中を t の順にする（一つの列の交差はふつう数個）。
    const uint32_t columnCount = cells[u] * cells[v];
    std::vector<uint32_t>& first = lists.first;
    first.assign(columnCount + 1u, 0u);
    for (const Crossing& crossing : found)
        ++first[crossing.column + 1u];
    for (uint32_t column = 0u; column < columnCount; ++column)
        first[column + 1u] += first[column];
    std::vector<Crossing>& sorted = lists.sorted;
    sorted.resize(found.size());
    for (const Crossing& crossing : found)
        sorted[first[crossing.column]++] = crossing;
    // 置いた後の first[c] は列 c の終わり（= 列 c + 1 の始め）。
    for (uint32_t column = 0u, begin = 0u; column < columnCount; begin = first[column++]) {
        const uint32_t end = first[column];
        if (end - begin < 2u)
            continue;
        for (uint32_t k = begin + 1u; k < end; ++k)
            for (uint32_t m = k; m > begin && sorted[m].t < sorted[m - 1u].t; --m)
                std::swap(sorted[m], sorted[m - 1u]);
        const size_t base = column % cells[u] * stride[u] + column / cells[u] * stride[v];
        // 交差 k まで数えた巻き数は、t_k < i ≤ t_(k+1) の格 i のもの。最後の交差より先は外とみなす。
        int32_t winding = 0;
        for (uint32_t k = begin; k + 1u < end; ++k) {
            winding += sorted[k].winding;
            if (winding == 0)
                continue;
            const int from = (std::max)(0, static_cast<int>(std::floor(sorted[k].t)) + 1);
            const int to =
                (std::min)(static_cast<int>(cells[axis]) - 1, static_cast<int>(std::floor(sorted[k + 1u].t)));
            for (int i = from; i <= to; ++i)
                ++votes[base + static_cast<size_t>(i) * stride[axis]];
        }
    }
}

} // namespace

std::shared_ptr<const FluidShapeVolume> BakeFluidShapeVolume(const ModelAsset& model, uint32_t longestAxisCells) {
    std::vector<Triangle> triangles = CollectModelTriangles(model);
    if (triangles.empty())
        return nullptr;

    Bounds bounds = BOUNDS::EmptyBounds();
    for (const Triangle& triangle : triangles)
        for (const Vec3& corner : triangle.corner)
            BOUNDS::Encapsulate(bounds, corner);
    const Vec3 extent = bounds.max - bounds.min;
    const float longest = (std::max)(extent.x, (std::max)(extent.y, extent.z));
    const float cellSize = longest / static_cast<float>(std::clamp(longestAxisCells, 8u, 256u));
    const float band = kFluidShapeVolumeBandCells * cellSize;
    // band に半格を足して広げ、箱の面の格の中心を包围盒から band 以上離す（面の上の値がちょうど +1 になり、
    // 貼図の端で切り詰めて読んでも外になる）。
    const float margin = band + 0.5f * cellSize;

    auto volume = std::make_shared<FluidShapeVolume>();
    volume->band = band;
    for (int axis = 0; axis < 3; ++axis) {
        const float wanted = std::ceil((Component(extent, axis) + 2.0f * margin) / cellSize);
        volume->cells[axis] = (std::max)(4u, static_cast<uint32_t>(wanted));
    }
    const Vec3 half = Vec3{static_cast<float>(volume->cells[0]), static_cast<float>(volume->cells[1]),
                           static_cast<float>(volume->cells[2])} *
                      (0.5f * cellSize);
    const Vec3 centre = (bounds.min + bounds.max) * 0.5f;
    volume->boundsMin = centre - half;
    volume->boundsMax = centre + half;

    // 格の座標へ移す（格 i の中心が i）。以降の距離はすべて格の数。
    const float inverseCellSize = 1.0f / cellSize;
    const Vec3 origin = volume->boundsMin + Vec3{0.5f, 0.5f, 0.5f} * cellSize;
    for (Triangle& triangle : triangles)
        for (Vec3& corner : triangle.corner)
            corner = (corner - origin) * inverseCellSize;

    const size_t cellCount = static_cast<size_t>(volume->cells[0]) * volume->cells[1] * volume->cells[2];
    const float bandCells = kFluidShapeVolumeBandCells;
    std::vector<float> distanceSquared(cellCount + 3u, bandCells * bandCells);
    MeasureDistances(triangles, volume->cells, bandCells, distanceSquared);

    // 一つの軸だけでは、穴や裏返った面の影を通る光線が列ごと内外を取り違える。三軸のうち二つが内と言えば内とする。
    std::vector<uint8_t> votes(cellCount, 0u);
    CrossingLists crossings{};
    for (int axis = 0; axis < 3; ++axis)
        VoteInside(triangles, volume->cells, axis, crossings, votes);

    volume->distance.resize(cellCount);
    const float farSquared = bandCells * bandCells;
    for (size_t index = 0u; index < cellCount; ++index) {
        const bool inside = votes[index] >= 2u;
        if (distanceSquared[index] >= farSquared) {
            volume->distance[index] = inside ? int8_t{-127} : int8_t{127};
            continue;
        }
        // 0 から離れる向きに丸める（lround と同じ）。距離は band より短いので 127 を超えない。
        const int magnitude = static_cast<int>(std::sqrt(distanceSquared[index]) * (127.0f / bandCells) + 0.5f);
        volume->distance[index] = static_cast<int8_t>(inside ? -magnitude : magnitude);
    }
    return volume;
}

} // namespace HIKARI::RENDER3D::FLUID
