#include "HIKARI_LightProbeBaker.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <utility>

#include <json.hpp>

#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

namespace HIKARI::TOOLS::BAKING {

namespace {

// 最初の光線が裏面に当たった標本がこの割合を超える探針は形状に埋まっている（壁や床の中）とみなし、無効にする
// （RTXGI の探針の分類と同じ考え方）。光照贴图の texel より緩いのは、探針は部屋の角の近くでも表を多く見るため。
constexpr float kProbeBackfaceTolerance = 0.25f;

void AddError(LightProbeBakeResult& result, std::string message) {
    result.errors.push_back(std::move(message));
    result.success = false;
}

std::filesystem::path LightProbeOutputDirectory(const std::filesystem::path& projectRoot,
                                                const std::string& sceneGuid) {
    return (ASSETS::LIGHTING::BuildLightingBakeRoot(projectRoot, sceneGuid) / "light_probes").lexically_normal();
}

void EvaluateSh9Basis(const MATH::Vec3& dir, float outBasis[9]) {
    const float x = dir.x;
    const float y = dir.y;
    const float z = dir.z;
    outBasis[0] = 0.282095f;
    outBasis[1] = 0.488603f * y;
    outBasis[2] = 0.488603f * z;
    outBasis[3] = 0.488603f * x;
    outBasis[4] = 1.092548f * x * y;
    outBasis[5] = 1.092548f * y * z;
    outBasis[6] = 0.315392f * (3.0f * z * z - 1.0f);
    outBasis[7] = 1.092548f * x * z;
    outBasis[8] = 0.546274f * (x * x - y * y);
}

// 球面三角形の立体角（Van Oosterom & Strackee）。
double TriangleSolidAngle(const MATH::Vec3& a, const MATH::Vec3& b, const MATH::Vec3& c) {
    const double triple = MATH::Dot(a, MATH::Cross(b, c));
    const double denominator = 1.0 + MATH::Dot(a, b) + MATH::Dot(b, c) + MATH::Dot(c, a);
    return 2.0 * std::atan2(std::abs(triple), denominator);
}

// 八面体の方向（16x16）ごとの、立体角で重みづけた SH9 の基底の積分。方向の格子を 8x8 に細分し、小さな四角を二つの
// 球面三角形として数える。方向ごとの放射輝度（DXR が格子の中で一様に選んだ光線の平均）にこれを掛けて足すと投影になる。
using DirectionBasis =
    std::array<std::array<float, ASSETS::LIGHTING::kLightProbeShCoeffCount>, kDxrProbeDirectionCount>;
const DirectionBasis& GetDirectionBasis() {
    static const DirectionBasis basis = [] {
        constexpr uint32_t side = ASSETS::LIGHTING::kLightProbeVisibilityResolution;
        constexpr uint32_t subdivisions = 8u;
        static_assert(side * side == kDxrProbeDirectionCount);
        std::array<std::array<double, ASSETS::LIGHTING::kLightProbeShCoeffCount>, kDxrProbeDirectionCount> sums{};
        double total = 0.0;
        const auto direction = [](double u, double v) {
            return MATH::DecodeOctahedralDirection({static_cast<float>(u), static_cast<float>(v)});
        };
        for (uint32_t cell = 0; cell < kDxrProbeDirectionCount; ++cell) {
            const double cx = cell % side, cy = cell / side;
            for (uint32_t sy = 0; sy < subdivisions; ++sy)
                for (uint32_t sx = 0; sx < subdivisions; ++sx) {
                    const double u0 = (cx + double(sx) / subdivisions) / side,
                                 u1 = (cx + double(sx + 1u) / subdivisions) / side;
                    const double v0 = (cy + double(sy) / subdivisions) / side,
                                 v1 = (cy + double(sy + 1u) / subdivisions) / side;
                    const MATH::Vec3 d00 = direction(u0, v0), d10 = direction(u1, v0), d01 = direction(u0, v1),
                                     d11 = direction(u1, v1);
                    const double solidAngle = TriangleSolidAngle(d00, d10, d11) + TriangleSolidAngle(d00, d11, d01);
                    float values[ASSETS::LIGHTING::kLightProbeShCoeffCount]{};
                    EvaluateSh9Basis(direction(0.5 * (u0 + u1), 0.5 * (v0 + v1)), values);
                    for (uint32_t k = 0; k < ASSETS::LIGHTING::kLightProbeShCoeffCount; ++k)
                        sums[cell][k] += values[k] * solidAngle;
                    total += solidAngle;
                }
        }
        // 細分の誤差を全球の 4π へ合わせる。
        const double scale = total > 0.0 ? 4.0 * std::numbers::pi / total : 0.0;
        DirectionBasis result{};
        for (uint32_t cell = 0; cell < kDxrProbeDirectionCount; ++cell)
            for (uint32_t k = 0; k < ASSETS::LIGHTING::kLightProbeShCoeffCount; ++k)
                result[cell][k] = static_cast<float>(sums[cell][k] * scale);
        return result;
    }();
    return basis;
}

bool SaveDebugJson(const std::filesystem::path& path, const LightProbeBakeRequest& request,
                   const LightProbeBakeResult& result, std::string* outMessage) {
    LightProbeVolumeSettings settings = request.settings;
    ClampLightProbeVolumeSettings(settings);
    const nlohmann::json root{
        {"type", "VolumeGrid"},
        {"transport", "DxrPathTracing"},
        {"probeCount", GetLightProbeVolumeProbeCount(settings)},
        {"directionCount", kDxrProbeDirectionCount},
        {"samplesPerDirection", request.sampleCount},
        {"bounceCount", request.bounceCount},
        {"buriedProbeCount", result.buriedProbeCount},
        {"shOrder", settings.shOrder},
    };
    if (!SERIALIZATION::JSON::WriteJsonFile(path, root, outMessage)) {
        return false;
    }
    if (outMessage != nullptr) {
        *outMessage = "light probe debug json saved: " + path.generic_string();
    }
    return true;
}

} // namespace

LightProbeBakeResult LightProbeBaker::BuildVolume(const LightProbeBakeRequest& request,
                                                  std::span<const DxrTransportAccumulation> cells,
                                                  float visibilityDistanceScale) const {
    LightProbeBakeResult result{};
    result.success = false;

    if (request.projectRoot.empty()) {
        AddError(result, "Project root is empty.");
        return result;
    }
    if (request.sceneGuid.empty()) {
        AddError(result, "Scene GUID is empty.");
        return result;
    }

    LightProbeVolumeSettings settings = request.settings;
    ClampLightProbeVolumeSettings(settings);
    const uint32_t probeCount = GetLightProbeVolumeProbeCount(settings);
    if (!settings.enabled) {
        AddError(result, "Light probe volume is disabled.");
        return result;
    }
    if (cells.size() != static_cast<size_t>(probeCount) * kDxrProbeDirectionCount ||
        !(visibilityDistanceScale > 0.0f)) {
        AddError(result, "Light probe transport results do not match the volume grid.");
        return result;
    }

    ASSETS::LIGHTING::LightProbeVolumeFileData volume{};
    volume.origin = settings.origin;
    volume.size = settings.size;
    volume.spacing = GetLightProbeVolumeSpacing(settings);
    volume.countX = settings.countX;
    volume.countY = settings.countY;
    volume.countZ = settings.countZ;
    volume.shOrder = ASSETS::LIGHTING::kLightProbeShOrder;
    volume.coeffCount = ASSETS::LIGHTING::kLightProbeShCoeffCount;
    volume.probes.resize(probeCount);
    volume.visibilityDistanceScale = visibilityDistanceScale;
    volume.visibilityMoments.resize(static_cast<size_t>(probeCount) *
                                    ASSETS::LIGHTING::kLightProbeVisibilityTexelCount);

    // 方向ごとに、裏面に当たらなかった標本の放射輝度の平均を SH9 へ投影し、最初に当たるまでの距離の平均と二乗平均を
    // 可視性の moment にする（距離は visibilityDistanceScale で正規化）。埋まった探針は寄与も可視性も零にして、
    // 実行時の可視性の判定で補間から外す。
    const DirectionBasis& basis = GetDirectionBasis();
    const float inverseScale = 1.0f / visibilityDistanceScale;
    for (uint32_t probe = 0; probe < probeCount; ++probe) {
        const auto probeCells =
            cells.subspan(static_cast<size_t>(probe) * kDxrProbeDirectionCount, kDxrProbeDirectionCount);
        double samples = 0.0, backfaces = 0.0;
        for (const DxrTransportAccumulation& cell : probeCells) {
            samples += cell.radiance.w;
            backfaces += cell.statistics.x;
        }
        if (!(samples > 0.0)) {
            AddError(result, "Light probe " + std::to_string(probe) + " received no samples.");
            return result;
        }
        auto moments = std::span(volume.visibilityMoments)
                           .subspan(static_cast<size_t>(probe) * ASSETS::LIGHTING::kLightProbeVisibilityTexelCount,
                                    ASSETS::LIGHTING::kLightProbeVisibilityTexelCount);
        ASSETS::LIGHTING::LightProbeSh9& sh = volume.probes[probe];
        if (backfaces > kProbeBackfaceTolerance * samples) {
            ++result.buriedProbeCount;
            std::fill(moments.begin(), moments.end(), MATH::Vec2{0.0f, 0.0f});
            continue;
        }
        for (uint32_t direction = 0; direction < kDxrProbeDirectionCount; ++direction) {
            const DxrTransportAccumulation& cell = probeCells[direction];
            const float front = cell.radiance.w - cell.statistics.x;
            if (!(front > 0.0f)) {
                moments[direction] = {0.0f, 0.0f};
                continue;
            }
            const MATH::Vec3 radiance{cell.radiance.x / front, cell.radiance.y / front, cell.radiance.z / front};
            for (uint32_t k = 0; k < ASSETS::LIGHTING::kLightProbeShCoeffCount; ++k)
                sh.coeffs[k] = sh.coeffs[k] + radiance * basis[direction][k];
            moments[direction] = {std::clamp(cell.statistics.y / front * inverseScale, 0.0f, 1.0f),
                                  std::clamp(cell.statistics.w / front * inverseScale * inverseScale, 0.0f, 1.0f)};
        }
    }

    result.volumePath =
        (LightProbeOutputDirectory(request.projectRoot, request.sceneGuid) / (request.volumeId + ".hlpv"))
            .lexically_normal();
    std::string saveMessage{};
    if (!ASSETS::LIGHTING::SaveLightProbeVolumeFile(result.volumePath, volume, &saveMessage)) {
        AddError(result, saveMessage);
        return result;
    }
    result.volumeWritten = true;
    result.messages.push_back(saveMessage);
    if (result.buriedProbeCount > 0u)
        result.messages.push_back(std::to_string(result.buriedProbeCount) + " of " + std::to_string(probeCount) +
                                  " probes are inside geometry and were disabled.");

    result.debugJsonPath =
        LightProbeOutputDirectory(request.projectRoot, request.sceneGuid) / (request.volumeId + "_debug.json");
    if (SaveDebugJson(result.debugJsonPath, request, result, &saveMessage)) {
        result.debugJsonWritten = true;
        result.messages.push_back(saveMessage);
    } else if (!saveMessage.empty()) {
        result.warnings.push_back(saveMessage);
    }

    result.record.id = request.volumeId;
    result.record.name = request.volumeName;
    result.record.type = "VolumeGrid";
    result.record.position = settings.origin + settings.size * 0.5f;
    result.record.origin = settings.origin;
    result.record.size = settings.size;
    result.record.countX = settings.countX;
    result.record.countY = settings.countY;
    result.record.countZ = settings.countZ;
    result.record.shOrder = settings.shOrder;
    result.record.probeCount = probeCount;
    result.record.blendDistance = settings.blendDistance;
    result.record.priority = request.priority;
    result.record.shDataPath = PROJECT_PATHS::MakeProjectRelativeString(request.projectRoot, result.volumePath);
    result.record.authoringHash = request.authoringHash;
    result.record.captureSceneHash = request.sceneHash;
    result.record.dependencyHash = request.dependencyHash;
    result.record.bakeSet = request.bakeSet;
    result.record.scenario = request.scenario;

    result.probeCount = probeCount;
    result.success = true;
    HIKARI_LOG_INFO("[LightProbeBaker] baked volume scene=" + request.sceneGuid +
                    " probes=" + std::to_string(probeCount) + " buried=" + std::to_string(result.buriedProbeCount) +
                    " path=" + result.volumePath.generic_string());
    return result;
}

} // namespace HIKARI::TOOLS::BAKING
