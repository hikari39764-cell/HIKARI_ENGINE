#include "RenderContracts/Color/HIKARI_ColorLutAsset.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <string_view>

#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::ASSETS::COLOR {
namespace {

std::string_view Trim(std::string_view text) noexcept {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1u);
}

// 空白区切りの数を count 個読む。余りがあれば失敗。
bool ParseFloats(std::string_view text, float* values, size_t count) noexcept {
    for (size_t i = 0; i < count; ++i) {
        text = Trim(text);
        const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), values[i]);
        if (ec != std::errc{} || !MATH::IsFinite(values[i])) {
            return false;
        }
        text.remove_prefix(static_cast<size_t>(end - text.data()));
    }
    return Trim(text).empty();
}

bool Fail(std::string& error, size_t line, const std::string& message) {
    error = ".cube line " + std::to_string(line) + ": " + message;
    return false;
}

} // namespace

bool LoadColorLutAsset(const std::filesystem::path& path, ColorLutAsset& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "Cannot open color LUT: " + path.string();
        return false;
    }
    ColorLutAsset lut;
    std::string text;
    size_t lineNumber = 0u;
    while (std::getline(file, text)) {
        ++lineNumber;
        std::string_view line = Trim(text);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const char first = line.front();
        if (first == '-' || first == '+' || first == '.' || (first >= '0' && first <= '9')) {
            if (lut.size == 0u) {
                return Fail(error, lineNumber, "data before LUT_3D_SIZE");
            }
            MATH::Vec3 value{};
            if (!ParseFloats(line, &value.x, 3u)) {
                return Fail(error, lineNumber, "expected three finite numbers");
            }
            lut.table.push_back(value);
            continue;
        }
        if (!lut.table.empty()) {
            return Fail(error, lineNumber, "keyword after table data");
        }
        const size_t split = line.find_first_of(" \t");
        const std::string_view keyword = line.substr(0u, split);
        const std::string_view arguments = split == std::string_view::npos ? std::string_view{} : line.substr(split);
        if (keyword == "TITLE") {
            lut.title = std::string(Trim(arguments));
            if (lut.title.size() >= 2u && lut.title.front() == '"' && lut.title.back() == '"') {
                lut.title = lut.title.substr(1u, lut.title.size() - 2u);
            }
        } else if (keyword == "LUT_3D_SIZE") {
            float size = 0.0f;
            if (!ParseFloats(arguments, &size, 1u) || size != std::floor(size) || size < kMinColorLutSize ||
                size > kMaxColorLutSize) {
                return Fail(error, lineNumber,
                            "LUT_3D_SIZE must be an integer in " + std::to_string(kMinColorLutSize) + ".." +
                                std::to_string(kMaxColorLutSize));
            }
            lut.size = static_cast<uint32_t>(size);
        } else if (keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX") {
            MATH::Vec3& domain = keyword == "DOMAIN_MIN" ? lut.domainMin : lut.domainMax;
            if (!ParseFloats(arguments, &domain.x, 3u)) {
                return Fail(error, lineNumber, std::string(keyword) + " expects three numbers");
            }
        } else if (keyword == "LUT_3D_INPUT_RANGE") {
            float range[2]{};
            if (!ParseFloats(arguments, range, 2u)) {
                return Fail(error, lineNumber, "LUT_3D_INPUT_RANGE expects two numbers");
            }
            lut.domainMin = {range[0], range[0], range[0]};
            lut.domainMax = {range[1], range[1], range[1]};
        } else if (keyword == "LUT_1D_SIZE" || keyword == "LUT_1D_INPUT_RANGE") {
            return Fail(error, lineNumber, "1D LUTs are not supported");
        } else {
            return Fail(error, lineNumber, "unsupported keyword " + std::string(keyword));
        }
    }
    if (lut.size == 0u) {
        error = "Color LUT has no LUT_3D_SIZE: " + path.string();
        return false;
    }
    const size_t expected = static_cast<size_t>(lut.size) * lut.size * lut.size;
    if (lut.table.size() != expected) {
        error = "Color LUT expects " + std::to_string(expected) + " entries but has " +
                std::to_string(lut.table.size()) + ": " + path.string();
        return false;
    }
    if (!(lut.domainMax.x > lut.domainMin.x && lut.domainMax.y > lut.domainMin.y &&
          lut.domainMax.z > lut.domainMin.z)) {
        error = "Color LUT DOMAIN_MAX must exceed DOMAIN_MIN: " + path.string();
        return false;
    }
    out = std::move(lut);
    return true;
}

MATH::Vec3 SampleColorLut(const ColorLutAsset& lut, const MATH::Vec3& color) noexcept {
    const int last = static_cast<int>(lut.size) - 1;
    const auto coordinate = [&](float value, float minimum, float maximum, int& index, float& fraction) {
        const float grid = std::clamp((value - minimum) / (maximum - minimum), 0.0f, 1.0f) * static_cast<float>(last);
        index = (std::min)(static_cast<int>(grid), last - 1);
        fraction = grid - static_cast<float>(index);
    };
    int r = 0, g = 0, b = 0;
    float fr = 0.0f, fg = 0.0f, fb = 0.0f;
    coordinate(color.x, lut.domainMin.x, lut.domainMax.x, r, fr);
    coordinate(color.y, lut.domainMin.y, lut.domainMax.y, g, fg);
    coordinate(color.z, lut.domainMin.z, lut.domainMax.z, b, fb);
    const size_t size = lut.size;
    const auto at = [&](int x, int y, int z) -> const MATH::Vec3& {
        return lut.table[(static_cast<size_t>(z) * size + static_cast<size_t>(y)) * size + static_cast<size_t>(x)];
    };
    const auto lerp = [](const MATH::Vec3& a, const MATH::Vec3& c, float t) { return a + (c - a) * t; };
    const MATH::Vec3 c00 = lerp(at(r, g, b), at(r + 1, g, b), fr);
    const MATH::Vec3 c10 = lerp(at(r, g + 1, b), at(r + 1, g + 1, b), fr);
    const MATH::Vec3 c01 = lerp(at(r, g, b + 1), at(r + 1, g, b + 1), fr);
    const MATH::Vec3 c11 = lerp(at(r, g + 1, b + 1), at(r + 1, g + 1, b + 1), fr);
    return lerp(lerp(c00, c10, fg), lerp(c01, c11, fg), fb);
}

} // namespace HIKARI::ASSETS::COLOR
