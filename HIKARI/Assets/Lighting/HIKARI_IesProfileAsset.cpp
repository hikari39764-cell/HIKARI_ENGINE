#include "Assets/Lighting/HIKARI_IesProfileAsset.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string_view>

#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::ASSETS::LIGHTING {
namespace {

constexpr uint32_t kMaximumAngleCount = 1024u;

// 空白とカンマで区切った数を順に読む。
class NumberReader {
public:
    explicit NumberReader(std::string_view text) : text_(text) {}

    bool Next(float& value) {
        while (position_ < text_.size() &&
               (text_[position_] == ' ' || text_[position_] == '\t' || text_[position_] == '\r' ||
                text_[position_] == '\n' || text_[position_] == ',')) {
            ++position_;
        }
        const char* begin = text_.data() + position_;
        const auto [end, ec] = std::from_chars(begin, text_.data() + text_.size(), value);
        if (ec != std::errc{} || !MATH::IsFinite(value)) {
            return false;
        }
        position_ += static_cast<size_t>(end - begin);
        return true;
    }

    bool NextCount(uint32_t& count) {
        float value = 0.0f;
        if (!Next(value) || value < 1.0f || value > static_cast<float>(kMaximumAngleCount) ||
            value != std::floor(value)) {
            return false;
        }
        count = static_cast<uint32_t>(value);
        return true;
    }

    bool NextList(uint32_t count, std::vector<float>& out) {
        out.resize(count);
        for (float& value : out) {
            if (!Next(value)) {
                return false;
            }
        }
        return true;
    }

private:
    std::string_view text_;
    size_t position_ = 0u;
};

bool Fail(std::string& error, const std::filesystem::path& path, const std::string& message) {
    error = "IES profile " + path.string() + ": " + message;
    return false;
}

bool IsAscending(const std::vector<float>& values) {
    return std::adjacent_find(values.begin(), values.end(), std::greater_equal<float>()) == values.end();
}

// 不等間隔の格子の線形補間の位置。範囲外は端で止める。
void Locate(const std::vector<float>& axis, float value, size_t& index, float& fraction) {
    if (axis.size() == 1u || value <= axis.front()) {
        index = 0u;
        fraction = 0.0f;
        return;
    }
    if (value >= axis.back()) {
        index = axis.size() - 2u;
        fraction = 1.0f;
        return;
    }
    index = static_cast<size_t>(std::upper_bound(axis.begin(), axis.end(), value) - axis.begin()) - 1u;
    fraction = (value - axis[index]) / (axis[index + 1u] - axis[index]);
}

// 水平角を表が持つ範囲へ折り返す（回転対称、四象限対称、0-180° 面か 90-270° 面での左右対称）。
float FoldHorizontal(const std::vector<float>& horizontal, float degrees) {
    const float first = horizontal.front();
    const float last = horizontal.back();
    if (horizontal.size() == 1u) {
        return first;
    }
    if (last <= 90.0f) {
        const float half = std::fmod(degrees, 180.0f);
        return half > 90.0f ? 180.0f - half : half;
    }
    if (last <= 180.0f) {
        return degrees > 180.0f ? 360.0f - degrees : degrees;
    }
    if (first >= 90.0f && last <= 270.0f) {
        return degrees < 90.0f ? 180.0f - degrees : (degrees > 270.0f ? 540.0f - degrees : degrees);
    }
    return degrees;
}

float Lookup(const IesProfileAsset& profile, float verticalDegrees, float horizontalDegrees) {
    const std::vector<float>& vertical = profile.verticalAngles;
    if (verticalDegrees < vertical.front() - 1e-3f || verticalDegrees > vertical.back() + 1e-3f) {
        return 0.0f;
    }
    size_t v = 0u, h = 0u;
    float fv = 0.0f, fh = 0.0f;
    Locate(vertical, verticalDegrees, v, fv);
    Locate(profile.horizontalAngles, FoldHorizontal(profile.horizontalAngles, horizontalDegrees), h, fh);
    const size_t verticalCount = vertical.size();
    const size_t v1 = (std::min)(v + 1u, verticalCount - 1u);
    const size_t h1 = (std::min)(h + 1u, profile.horizontalAngles.size() - 1u);
    const auto at = [&](size_t hi, size_t vi) { return profile.candela[hi * verticalCount + vi]; };
    const float low = at(h, v) + (at(h, v1) - at(h, v)) * fv;
    const float high = at(h1, v) + (at(h1, v1) - at(h1, v)) * fv;
    return low + (high - low) * fh;
}

} // namespace

bool LoadIesProfileAsset(const std::filesystem::path& path, IesProfileAsset& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return Fail(error, path, "cannot open file");
    }
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const size_t tilt = text.find("TILT=");
    if (tilt == std::string::npos) {
        return Fail(error, path, "missing TILT= line");
    }
    const size_t tiltEnd = text.find('\n', tilt);
    if (tiltEnd == std::string::npos) {
        return Fail(error, path, "no photometric data after TILT=");
    }
    std::string_view tiltValue = std::string_view(text).substr(tilt + 5u, tiltEnd - tilt - 5u);
    while (!tiltValue.empty() && (tiltValue.back() == '\r' || tiltValue.back() == ' ')) {
        tiltValue.remove_suffix(1u);
    }
    NumberReader reader(std::string_view(text).substr(tiltEnd + 1u));
    if (tiltValue == "INCLUDE") {
        // 灯具の傾きの補正表（幾何の種類、組数、角度、倍率）。配光の形には使わないので読み飛ばす。
        float geometry = 0.0f;
        uint32_t pairs = 0u;
        std::vector<float> skipped;
        if (!reader.Next(geometry) || !reader.NextCount(pairs) || !reader.NextList(pairs * 2u, skipped)) {
            return Fail(error, path, "malformed TILT=INCLUDE data");
        }
    } else if (tiltValue != "NONE") {
        return Fail(error, path, "external TILT files are not supported");
    }
    float lamps = 0.0f, lumens = 0.0f, multiplier = 0.0f, photometricType = 0.0f, units = 0.0f;
    float width = 0.0f, length = 0.0f, height = 0.0f, ballast = 0.0f, futureUse = 0.0f, watts = 0.0f;
    uint32_t verticalCount = 0u, horizontalCount = 0u;
    if (!reader.Next(lamps) || !reader.Next(lumens) || !reader.Next(multiplier) || !reader.NextCount(verticalCount) ||
        !reader.NextCount(horizontalCount) || !reader.Next(photometricType) || !reader.Next(units) ||
        !reader.Next(width) || !reader.Next(length) || !reader.Next(height) || !reader.Next(ballast) ||
        !reader.Next(futureUse) || !reader.Next(watts)) {
        return Fail(error, path, "malformed photometric header");
    }
    if (photometricType != 1.0f) {
        return Fail(error, path, "only Type C photometry is supported");
    }
    IesProfileAsset profile;
    if (!reader.NextList(verticalCount, profile.verticalAngles) ||
        !reader.NextList(horizontalCount, profile.horizontalAngles) ||
        !reader.NextList(verticalCount * horizontalCount, profile.candela)) {
        return Fail(error, path, "angle or candela table is shorter than declared");
    }
    if (!IsAscending(profile.verticalAngles) || !IsAscending(profile.horizontalAngles) ||
        profile.verticalAngles.front() < 0.0f || profile.verticalAngles.back() > 180.0f ||
        profile.horizontalAngles.front() < 0.0f || profile.horizontalAngles.back() > 360.0f) {
        return Fail(error, path, "angles must ascend within 0..180 (vertical) and 0..360 (horizontal)");
    }
    for (float& value : profile.candela) {
        value = (std::max)(value * multiplier, 0.0f);
        profile.maximumCandela = (std::max)(profile.maximumCandela, value);
    }
    if (profile.maximumCandela <= 0.0f) {
        return Fail(error, path, "all candela values are zero");
    }
    out = std::move(profile);
    return true;
}

IesProfileTable SampleIesProfile(const IesProfileAsset& profile) {
    IesProfileTable table;
    table.values.resize(static_cast<size_t>(kIesProfileVerticalSamples) * kIesProfileHorizontalSamples);
    // 全球の平均は鉛直を sin の重みで、水平を重複する 360° の列を除いて平均する。
    double weightedSum = 0.0;
    double weightSum = 0.0;
    for (uint32_t h = 0u; h < kIesProfileHorizontalSamples; ++h) {
        const float horizontal = 360.0f * static_cast<float>(h) / static_cast<float>(kIesProfileHorizontalSamples - 1u);
        for (uint32_t v = 0u; v < kIesProfileVerticalSamples; ++v) {
            const float vertical = 180.0f * static_cast<float>(v) / static_cast<float>(kIesProfileVerticalSamples - 1u);
            const float value = Lookup(profile, vertical, horizontal) / profile.maximumCandela;
            table.values[static_cast<size_t>(h) * kIesProfileVerticalSamples + v] = value;
            if (h + 1u < kIesProfileHorizontalSamples) {
                const double weight = std::sin(static_cast<double>(vertical) * 3.14159265358979323846 / 180.0);
                weightedSum += weight * value;
                weightSum += weight;
            }
        }
    }
    const double mean = weightSum > 0.0 ? weightedSum / weightSum : 0.0;
    table.peakToMean = mean > 1e-6 ? static_cast<float>(1.0 / mean) : 1.0f;
    return table;
}

} // namespace HIKARI::ASSETS::LIGHTING
