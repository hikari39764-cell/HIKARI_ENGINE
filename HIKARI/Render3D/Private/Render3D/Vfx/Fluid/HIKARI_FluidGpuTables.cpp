#include "Render3D/Vfx/Fluid/HIKARI_FluidSolver.h"

#include <algorithm>
#include <cmath>
#include <limits>

// 解算へ渡す GPU の表：流体源（形・注入・力場・形状引导・体積の湧き出し・子步の間の掃き）と押すもの（障碍物・冲量・力）を
// 域の局所空間へ移して詰める。並びは HIKARI_FluidGrid.hlsli の HikariFluidSource / HikariFluidInteraction と同じ。
namespace HIKARI::RENDER3D::FLUID::INTERNAL {
namespace {
// 形の表面積 / 体積 [1/m]（源の局所、HikariFluidSourceDistance と同じ形）。outflow をこれに掛けると、形の中で一様な湧き出し率
// が表面から outflow [m/s] で流れ出る量になる。Domain は 0（域全体の湧き出しは意味を持たない）。
float SourceAreaPerVolume(const FluidSourceSubmission& source, float softEdge) noexcept {
    const auto positive = [](float value) { return (std::max)(value, 1.0e-4f); };
    switch (source.shape) {
    case FluidSourceShape::Sphere: {
        const float outer = positive(source.radius);
        const float inner = (std::clamp)(source.innerRadius, 0.0f, 0.99f * outer);
        return 3.0f * (outer * outer + inner * inner) / (outer * outer * outer - inner * inner * inner);
    }
    case FluidSourceShape::Cone: {
        const float length = positive(source.radius);
        const float base = positive(length * std::tan((std::clamp)(source.coneAngleRadians, 0.0f, 1.55f)));
        return 3.0f * (base + std::sqrt(base * base + length * length)) / (base * length);
    }
    case FluidSourceShape::Box:
        return 1.0f / positive(source.boxHalfExtents.x) + 1.0f / positive(source.boxHalfExtents.y) +
               1.0f / positive(source.boxHalfExtents.z);
    case FluidSourceShape::Cylinder: return 2.0f / positive(source.radius) + 2.0f / positive(source.height);
    case FluidSourceShape::Circle:
        return 1.0f / positive((std::max)(0.5f * (source.radius - source.innerRadius), softEdge)) +
               1.0f / positive(softEdge);
    case FluidSourceShape::Edge: return 2.0f / positive((std::max)(source.radius, softEdge));
    case FluidSourceShape::Domain: return 0.0f;
    default: return 3.0f / positive((std::max)(source.radius, softEdge));
    }
}

// 形を単位の向き d（源の局所）へ動かした時の、体積 V と d に垂直な面への投影面積 A の比（d の向きの平均の厚さ、源の局所
// [m]）。距離 L を掃いた形の体積はおよそ V + A L なので、注入を V / (V + A L) 倍すると掃いた形へ入る量は止まった形と同じ
// になり、各格の量は形が通り過ぎる時間に比例する（速い源ほど薄い尾迹）。
float SourceThickness(const FluidSourceSubmission& source, const MATH::Vec3& d, float softEdge) noexcept {
    constexpr float pi = 3.14159265f;
    const auto positive = [](float value) { return (std::max)(value, 1.0e-4f); };
    const float along = std::abs(d.y);
    const float across = std::sqrt((std::max)(1.0f - along * along, 0.0f));
    float volume = 0.0f;
    float area = 0.0f;
    switch (source.shape) {
    case FluidSourceShape::Sphere: {
        const float outer = positive(source.radius);
        const float inner = (std::clamp)(source.innerRadius, 0.0f, 0.99f * outer);
        volume = 4.0f / 3.0f * pi * (outer * outer * outer - inner * inner * inner);
        area = pi * outer * outer;
        break;
    }
    case FluidSourceShape::Cone: {
        const float length = positive(source.radius);
        const float base = positive(length * std::tan((std::clamp)(source.coneAngleRadians, 0.0f, 1.55f)));
        volume = pi * base * base * length / 3.0f;
        area = along * pi * base * base + across * base * length;
        break;
    }
    case FluidSourceShape::Box: {
        const MATH::Vec3 h{positive(source.boxHalfExtents.x), positive(source.boxHalfExtents.y),
                           positive(source.boxHalfExtents.z)};
        volume = 8.0f * h.x * h.y * h.z;
        area = 4.0f * (std::abs(d.x) * h.y * h.z + std::abs(d.y) * h.x * h.z + std::abs(d.z) * h.x * h.y);
        break;
    }
    case FluidSourceShape::Cylinder: {
        const float radius = positive(source.radius);
        const float height = positive(source.height);
        volume = pi * radius * radius * height;
        area = along * pi * radius * radius + across * 2.0f * radius * height;
        break;
    }
    case FluidSourceShape::Circle: {
        // 円環の帯（XZ 平面、中心の半径 c、半幅 w、厚さ 2 × 縁の柔らかさ。HikariFluidSourceDistance と同じ形）。
        const float center = 0.5f * (source.innerRadius + source.radius);
        const float halfWidth = (std::max)(0.5f * (source.radius - source.innerRadius), softEdge);
        const float outer = center + halfWidth;
        const float inner = (std::max)(center - halfWidth, 0.0f);
        volume = 2.0f * pi * positive(center) * 2.0f * halfWidth * 2.0f * softEdge;
        area = along * pi * (outer * outer - inner * inner) + across * 2.0f * outer * 2.0f * softEdge;
        break;
    }
    case FluidSourceShape::Edge: {
        const float radius = positive((std::max)(source.radius, softEdge));
        const float length = (std::max)(source.height, 0.0f);
        const float sideways = std::sqrt((std::max)(1.0f - d.x * d.x, 0.0f));
        volume = pi * radius * radius * length + 4.0f / 3.0f * pi * radius * radius * radius;
        area = sideways * 2.0f * radius * length + pi * radius * radius;
        break;
    }
    default: {
        const float radius = positive((std::max)(source.radius, softEdge));
        volume = 4.0f / 3.0f * pi * radius * radius * radius;
        area = pi * radius * radius;
        break;
    }
    }
    return volume / (std::max)(area, 1.0e-8f);
}

// 拡縮の逆の平均（源の局所の長さ ≒ 世界の長さ × これ）。
float AverageInverseScale(const FluidSourceSubmission& source) noexcept {
    const auto inverse = [](float value) { return 1.0f / (std::max)(std::abs(value), 1.0e-4f); };
    return (inverse(source.worldScale.x) + inverse(source.worldScale.y) + inverse(source.worldScale.z)) / 3.0f;
}
} // namespace

float FluidSourceReach(const FluidSourceSubmission& source, float cellSize) noexcept {
    constexpr float kUnbounded = std::numeric_limits<float>::infinity();
    const float softEdge = 1.5f * cellSize * AverageInverseScale(source);
    const auto positive = [](float value) { return (std::max)(value, 0.0f); };
    float shape = 0.0f;
    switch (source.shape) {
    case FluidSourceShape::Sphere: shape = positive(source.radius); break;
    case FluidSourceShape::Cone: {
        const float length = positive(source.radius);
        const float base = length * std::tan((std::clamp)(source.coneAngleRadians, 0.0f, 1.55f));
        shape = std::sqrt(length * length + base * base);
        break;
    }
    case FluidSourceShape::Box: shape = MATH::Length(source.boxHalfExtents); break;
    case FluidSourceShape::Cylinder:
        shape = std::sqrt(source.radius * source.radius + 0.25f * source.height * source.height);
        break;
    case FluidSourceShape::Circle: shape = positive(source.radius) + softEdge; break;
    case FluidSourceShape::Edge: shape = 0.5f * positive(source.height) + (std::max)(source.radius, softEdge); break;
    case FluidSourceShape::Domain: return kUnbounded;
    default: shape = (std::max)(source.radius, softEdge); break;
    }
    // 注入は表面の外 2.5 縁まで（EmissionMask）、形状引导は帯の厚さまで届く。
    float reach = shape + 2.5f * softEdge +
                  (source.settings.guideStrength > 0.0f ? (std::max)(source.settings.guideThickness, 0.0f) : 0.0f);
    // 力場・風・減衰は力場の半径の中（半径 0 は域全体）。流体注入層は場面の域を共有するので、半径 0 でも形の届く範囲に
    // とどめる（特効一つが場地の烟全体を翻搅しない）。
    if (PARTICLES::IsParticleForceFieldActive(source.field) || source.drag > 0.0f || MATH::Length(source.wind) > 0.0f) {
        if (source.field.radius > 0.0f)
            reach = (std::max)(reach, MATH::Length(source.field.center) + source.field.radius);
        else if (!source.sceneInjection)
            return kUnbounded;
    }
    const float scale = (std::max)(
        {std::abs(source.worldScale.x), std::abs(source.worldScale.y), std::abs(source.worldScale.z), 1.0e-4f});
    return reach * scale;
}

// 回転と倍率の逆で、域の局所の点を源の局所へ、源の局所の向きを域の局所へ移す行と、形・注入・力場を詰める。
FluidSourceGpu PackFluidSource(const FluidSourceSubmission& source, const MATH::Vec3& domainPosition,
                               const MATH::Quat& domainRotation, float cellSize, float burstDensity,
                               const MATH::Vec3& sweep) {
    FluidSourceGpu gpu{};
    const MATH::Vec3 inverseScale{1.0f / (std::max)(std::abs(source.worldScale.x), 1.0e-4f),
                                  1.0f / (std::max)(std::abs(source.worldScale.y), 1.0e-4f),
                                  1.0f / (std::max)(std::abs(source.worldScale.z), 1.0e-4f)};
    const auto scaled = [&](const MATH::Vec3& v) {
        return MATH::Vec3{v.x * inverseScale.x, v.y * inverseScale.y, v.z * inverseScale.z};
    };
    const MATH::Vec3 basis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    MATH::Vec3 columns[3];
    MATH::Vec3 back[3];
    for (int axis = 0; axis < 3; ++axis) {
        columns[axis] =
            scaled(RotateToFluidLocal(source.worldRotation, MATH::RotateVector(domainRotation, basis[axis])));
        back[axis] = RotateToFluidLocal(domainRotation, MATH::RotateVector(source.worldRotation, basis[axis]));
    }
    const MATH::Vec3 translation =
        scaled(RotateToFluidLocal(source.worldRotation, domainPosition - source.worldPosition));
    const float* t = &translation.x;
    for (int row = 0; row < 3; ++row) {
        gpu.rows[row][0] = (&columns[0].x)[row];
        gpu.rows[row][1] = (&columns[1].x)[row];
        gpu.rows[row][2] = (&columns[2].x)[row];
        gpu.rows[row][3] = t[row];
        gpu.rows[3 + row][0] = (&back[0].x)[row];
        gpu.rows[3 + row][1] = (&back[1].x)[row];
        gpu.rows[3 + row][2] = (&back[2].x)[row];
    }
    const float averageScale = AverageInverseScale(source);
    const float softEdge = 1.5f * cellSize * averageScale;
    const auto set = [&](int row, float x, float y, float z, float w) {
        gpu.rows[row][0] = x;
        gpu.rows[row][1] = y;
        gpu.rows[row][2] = z;
        gpu.rows[row][3] = w;
    };
    set(6, static_cast<float>(source.shape), source.radius, source.innerRadius, source.height);
    set(7, source.boxHalfExtents.x, source.boxHalfExtents.y, source.boxHalfExtents.z, source.coneAngleRadians);
    set(8, (std::max)(source.ratePerSecond, 0.0f), burstDensity, source.settings.temperature,
        source.settings.absorption);
    set(9, source.settings.velocity.x, source.settings.velocity.y, source.settings.velocity.z,
        source.settings.radialSpeed);
    const auto& field = source.field;
    set(10, field.center.x, field.center.y, field.center.z, field.radius);
    set(11, field.axis.x, field.axis.y, field.axis.z, field.vortex);
    set(12, field.turbulence, field.frequency, field.evolution, field.attraction);
    const MATH::Vec3 wind = RotateToFluidLocal(domainRotation, source.wind);
    set(13, wind.x, wind.y, wind.z, (std::max)(source.drag, 0.0f));
    set(14, source.settings.guideStrength, source.settings.guideThickness,
        source.settings.forceMode == FluidForceMode::TargetVelocity ? 1.0f : 0.0f, softEdge);
    // 形の大きさは源の局所（拡縮を外した値）なので、世界の表面積 / 体積は拡縮の逆の平均を掛けて近似する。
    set(15, source.settings.outflow * SourceAreaPerVolume(source, softEdge) * averageScale,
        source.settings.emissionNoise, 1.0f / (std::max)(source.settings.emissionNoiseScale, 0.05f), 0.0f);
    // 掃き：この子步の終わりの形から始めの位置へ戻る線分（源の局所）と、掃いた形へ入れる注入の割合。
    MATH::Vec3 toStart = scaled(RotateToFluidLocal(source.worldRotation, sweep * -1.0f));
    const float swept = MATH::Length(toStart);
    float exposure = 1.0f;
    if (source.shape == FluidSourceShape::Domain || swept <= 1.0e-6f) {
        toStart = {};
    } else {
        const float thickness = SourceThickness(source, toStart * (1.0f / swept), softEdge);
        exposure = thickness / (thickness + swept);
    }
    set(16, toStart.x, toStart.y, toStart.z, exposure);
    // 届く範囲（域の局所の中心と半径、0 は域全体）：外の格は源を読まない。掃いた分だけ広げる。
    const float reach = FluidSourceReach(source, cellSize);
    const MATH::Vec3 center = RotateToFluidLocal(domainRotation, source.worldPosition - domainPosition);
    set(17, center.x, center.y, center.z, std::isfinite(reach) ? reach + MATH::Length(sweep) : 0.0f);
    // 注入する烟の色（cellColor の域では密度 × 色を足す。R16 の域では捨てられる）と、注入量のうち燃料になる割合。
    set(18, (std::max)(source.color.x, 0.0f), (std::max)(source.color.y, 0.0f), (std::max)(source.color.z, 0.0f),
        std::clamp(source.settings.fuelFraction, 0.0f, 1.0f));
    return gpu;
}

FluidInteractionGpu PackFluidInteraction(const FluidInteraction& interaction, const MATH::Vec3& domainPosition,
                                         const MATH::Quat& domainRotation, const MATH::Vec3& domainVelocity,
                                         float frameSeconds, float strengthScale) {
    FluidInteractionGpu gpu{};
    const MATH::Vec3 basis[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    MATH::Vec3 columns[3];
    for (int axis = 0; axis < 3; ++axis)
        columns[axis] = RotateToFluidLocal(interaction.worldRotation, MATH::RotateVector(domainRotation, basis[axis]));
    const MATH::Vec3 translation =
        RotateToFluidLocal(interaction.worldRotation, domainPosition - interaction.worldPosition);
    for (int row = 0; row < 3; ++row) {
        gpu.rows[row][0] = (&columns[0].x)[row];
        gpu.rows[row][1] = (&columns[1].x)[row];
        gpu.rows[row][2] = (&columns[2].x)[row];
        gpu.rows[row][3] = (&translation.x)[row];
    }
    const auto set = [&](int row, const MATH::Vec3& v, float w) {
        gpu.rows[row][0] = v.x;
        gpu.rows[row][1] = v.y;
        gpu.rows[row][2] = v.z;
        gpu.rows[row][3] = w;
    };
    // 障碍物は域に対する速度、注入は注入する烟の速さ（そのまま）、冲量・力は向き（正規化）。
    const bool obstacle = interaction.kind == FluidInteractionKind::Obstacle;
    const float directionLength = MATH::Length(interaction.direction);
    const MATH::Vec3 motion = obstacle                                         ? interaction.velocity - domainVelocity
                              : interaction.kind == FluidInteractionKind::Emit ? interaction.direction
                              : directionLength > 0.0f ? interaction.direction * (1.0f / directionLength)
                                                       : MATH::Vec3{};
    set(3, interaction.extents, static_cast<float>(interaction.kind));
    set(4, RotateToFluidLocal(domainRotation, motion), static_cast<float>(interaction.shape));
    set(5, RotateToFluidLocal(domainRotation, interaction.worldPosition - domainPosition),
        interaction.strength * strengthScale);
    // 注入の掃き：そのフレームに動いた線分（中心から始めの位置へ、域の局所）と、掃いた形へ入れる割合。動いた量は服务が
    // 記した実際の移動（域の時間倍率に左右されない）から、局所空間の域自身の動き（域の時間）を引く。二乗の減衰の重みの
    // 積分は球で 2πR³/15、線分 L の分で πR²L/6 増えるので、割合 1 / (1 + 5L / 4R) で総量を止まった時と同じにする。
    // 力の行 6 は x に吸収率 [1/s]。
    MATH::Vec3 toStart{};
    float exposure = 1.0f;
    if (interaction.kind == FluidInteractionKind::Emit) {
        toStart =
            RotateToFluidLocal(domainRotation, (interaction.displacement - domainVelocity * frameSeconds) * -1.0f);
        exposure = 1.0f / (1.0f + 1.25f * MATH::Length(toStart) / (std::max)(interaction.extents.x, 1.0e-4f));
    } else if (interaction.kind == FluidInteractionKind::Force) {
        toStart.x = interaction.absorption;
    }
    set(6, toStart, exposure);
    return gpu;
}

FluidDomainConstantsGpu PackFluidDomainConstants(const FluidDomainSettings& authored,
                                                 const FluidDomainResources& resources, uint32_t sourceCount,
                                                 double step, uint64_t stepIndex, const MATH::Vec3& up,
                                                 const MATH::Vec3& inertia, const MATH::Vec3& frameWind,
                                                 uint32_t interactionCount, uint32_t flags) noexcept {
    FluidDomainConstantsGpu constants{};
    constants.size[0] = resources.layout.cells[0];
    constants.size[1] = resources.layout.cells[1];
    constants.size[2] = resources.layout.cells[2];
    constants.size[3] = sourceCount;
    constants.cell[0] = resources.layout.cellSize;
    constants.cell[1] = 1.0f / resources.layout.cellSize;
    constants.cell[2] = static_cast<float>(step);
    constants.cell[3] = static_cast<float>(std::fmod(static_cast<double>(stepIndex) * step, 65536.0));
    constants.motion[0] = static_cast<float>(FluidClosedFaces(authored.boundary));
    constants.motion[1] = authored.vorticity;
    constants.motion[2] = authored.buoyancy;
    constants.motion[3] = authored.weight;
    constants.dissipation[0] = authored.densityDissipation;
    constants.dissipation[1] = authored.temperatureDissipation;
    constants.up[0] = up.x;
    constants.up[1] = up.y;
    constants.up[2] = up.z;
    constants.inertia[0] = inertia.x;
    constants.inertia[1] = inertia.y;
    constants.inertia[2] = inertia.z;
    constants.interactions[0] = interactionCount;
    constants.interactions[1] = resources.ActivityOffset();
    constants.interactions[2] = resources.SinkOffset();
    constants.interactions[3] = flags;
    constants.combustion0[0] = authored.ignitionTemperature;
    constants.combustion0[1] = authored.burnRate;
    constants.combustion0[2] = (std::max)(authored.fuelPerBurn, 1.0e-3f);
    constants.combustion0[3] = authored.heatPerBurn;
    constants.combustion1[0] = authored.smokePerBurn;
    constants.combustion1[1] = authored.expansionPerBurn;
    constants.combustion1[2] = resources.layout.combustion ? 1.0f : 0.0f;
    constants.combustion1[3] = 0.0f;
    std::copy(std::begin(resources.injectionCells), std::end(resources.injectionCells), constants.injection);
    constants.frameWind[0] = frameWind.x;
    constants.frameWind[1] = frameWind.y;
    constants.frameWind[2] = frameWind.z;
    constants.frameWind[3] = FluidOccupiedDensity(authored.extinction, resources.layout.cellSize);
    return constants;
}

} // namespace HIKARI::RENDER3D::FLUID::INTERNAL
