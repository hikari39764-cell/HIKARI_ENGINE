#include "TerrainKit/Data/HIKARI_TerrainDataImage.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstdlib>
#include <limits>

#include <tinyexr.h>

namespace HIKARI::TERRAIN::DATA {
namespace {

constexpr uint32_t kMaximumImageEdge = 65536u;

std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
    return extension;
}

bool ReadFileBytes(const std::filesystem::path& path, std::vector<uint8_t>& bytes, std::string& error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "cannot open " + path.string();
        return false;
    }
    const std::streamoff size = file.tellg();
    if (size <= 0 || size > (std::numeric_limits<int32_t>::max)()) {
        error = "unsupported file size: " + path.string();
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        error = "cannot read " + path.string();
        return false;
    }
    return true;
}

bool LoadExr(const std::filesystem::path& path, HeightImage& out, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes, error)) {
        return false;
    }
    float* rgba = nullptr;
    int width = 0;
    int height = 0;
    const char* exrError = nullptr;
    // 1 チャンネル（Y など）の EXR も R に入る。
    const int result = LoadEXRFromMemory(&rgba, &width, &height, bytes.data(), bytes.size(), &exrError);
    if (result != TINYEXR_SUCCESS) {
        error = "EXR " + path.string() + ": " + (exrError != nullptr ? exrError : "unknown error");
        if (exrError != nullptr) {
            FreeEXRErrorMessage(exrError);
        }
        return false;
    }
    if (width < 2 || height < 2 || static_cast<uint32_t>(width) > kMaximumImageEdge ||
        static_cast<uint32_t>(height) > kMaximumImageEdge) {
        free(rgba);
        error = "EXR " + path.string() + " must be 2..65536 pixels per edge";
        return false;
    }
    HeightImage image{};
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.values.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    bool finite = true;
    for (std::size_t i = 0u; i < image.values.size(); ++i) {
        image.values[i] = rgba[i * 4u];
        finite = finite && std::isfinite(image.values[i]);
    }
    free(rgba);
    if (!finite) {
        error = "EXR " + path.string() + " contains non-finite heights";
        return false;
    }
    out = std::move(image);
    return true;
}

bool LoadRaw16(const std::filesystem::path& path, uint32_t rawWidth, HeightImage& out, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes, error)) {
        return false;
    }
    if (bytes.size() % 2u != 0u) {
        error = "R16 " + path.string() + " has an odd byte count";
        return false;
    }
    const std::size_t pixels = bytes.size() / 2u;
    uint32_t width = rawWidth;
    if (width == 0u) {
        width = static_cast<uint32_t>(std::llround(std::sqrt(static_cast<double>(pixels))));
    }
    if (width < 2u || width > kMaximumImageEdge || pixels % width != 0u || pixels / width < 2u ||
        pixels / width > kMaximumImageEdge) {
        error = "R16 " + path.string() + " does not match width " + std::to_string(width);
        return false;
    }
    HeightImage image{};
    image.width = width;
    image.height = static_cast<uint32_t>(pixels / width);
    image.values.resize(pixels);
    for (std::size_t i = 0u; i < pixels; ++i) {
        const uint16_t value = static_cast<uint16_t>(bytes[i * 2u] | (bytes[i * 2u + 1u] << 8u));
        image.values[i] = static_cast<float>(value) / 65535.0f;
    }
    out = std::move(image);
    return true;
}

} // namespace

bool LoadHeightImage(const std::filesystem::path& path, uint32_t rawWidth, HeightImage& out, std::string& error) {
    const std::string extension = LowerExtension(path);
    if (extension == ".exr") {
        return LoadExr(path, out, error);
    }
    if (extension == ".r16" || extension == ".raw") {
        return LoadRaw16(path, rawWidth, out, error);
    }
    error = "unsupported height image format: " + path.string() + " (use .exr, .r16 or .raw)";
    return false;
}

bool SaveHeightImageExr(const std::filesystem::path& path, const HeightImage& image, std::string& error) {
    if (!image.Valid()) {
        error = "height image is empty or inconsistent";
        return false;
    }
    EXRHeader header;
    InitEXRHeader(&header);
    EXRImage exrImage;
    InitEXRImage(&exrImage);
    float* channels[1] = {const_cast<float*>(image.values.data())};
    exrImage.images = reinterpret_cast<unsigned char**>(channels);
    exrImage.num_channels = 1;
    exrImage.width = static_cast<int>(image.width);
    exrImage.height = static_cast<int>(image.height);
    EXRChannelInfo channel{};
    channel.name[0] = 'Y';
    header.num_channels = 1;
    header.channels = &channel;
    int pixelType = TINYEXR_PIXELTYPE_FLOAT;
    header.pixel_types = &pixelType;
    header.requested_pixel_types = &pixelType;
    header.compression_type = TINYEXR_COMPRESSIONTYPE_ZIP;
    unsigned char* memory = nullptr;
    const char* exrError = nullptr;
    const size_t size = SaveEXRImageToMemory(&exrImage, &header, &memory, &exrError);
    if (size == 0u || memory == nullptr) {
        error = std::string("EXR write failed: ") + (exrError != nullptr ? exrError : "unknown error");
        if (exrError != nullptr) {
            FreeEXRErrorMessage(exrError);
        }
        return false;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    const bool written = file && file.write(reinterpret_cast<const char*>(memory), static_cast<std::streamsize>(size));
    free(memory);
    if (!written) {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
}

bool SaveHeightImageRaw16(const std::filesystem::path& path, const HeightImage& image, std::string& error) {
    if (!image.Valid()) {
        error = "height image is empty or inconsistent";
        return false;
    }
    std::vector<uint8_t> bytes(image.values.size() * 2u);
    for (std::size_t i = 0u; i < image.values.size(); ++i) {
        const float clamped = std::isfinite(image.values[i]) ? std::clamp(image.values[i], 0.0f, 1.0f) : 0.0f;
        const auto value = static_cast<uint16_t>(clamped * 65535.0f + 0.5f);
        bytes[i * 2u] = static_cast<uint8_t>(value & 0xFFu);
        bytes[i * 2u + 1u] = static_cast<uint8_t>(value >> 8u);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file || !file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
}

float SampleHeightImage(const HeightImage& image, double pixelX, double pixelY) noexcept {
    if (!image.Valid()) {
        return 0.0f;
    }
    const double maxX = static_cast<double>(image.width - 1u);
    const double maxY = static_cast<double>(image.height - 1u);
    const double x = std::clamp(pixelX, 0.0, maxX);
    const double y = std::clamp(pixelY, 0.0, maxY);
    const auto x0 = static_cast<uint32_t>((std::min)(std::floor(x), maxX - 1.0));
    const auto y0 = static_cast<uint32_t>((std::min)(std::floor(y), maxY - 1.0));
    const float fx = static_cast<float>(x - x0);
    const float fy = static_cast<float>(y - y0);
    const std::size_t row0 = static_cast<std::size_t>(y0) * image.width;
    const std::size_t row1 = row0 + image.width;
    const float top = image.values[row0 + x0] + (image.values[row0 + x0 + 1u] - image.values[row0 + x0]) * fx;
    const float bottom = image.values[row1 + x0] + (image.values[row1 + x0 + 1u] - image.values[row1 + x0]) * fx;
    return top + (bottom - top) * fy;
}

} // namespace HIKARI::TERRAIN::DATA
