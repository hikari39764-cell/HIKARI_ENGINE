#include "HIKARI_IblBaker.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <DirectXTex.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <utility>
#include <vector>

#include "Core/HIKARI_Logger.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {
    namespace {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kEpsilon = 1.0e-5f;

        struct Float4 {
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            float a = 1.0f;
        };

        struct CubemapImage {
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t mipLevels = 1;
            DXGI_FORMAT format = DXGI_FORMAT_R32G32B32A32_FLOAT;

            // DirectX の cubemap face 順序: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z.
            std::vector<std::vector<std::vector<Float4>>> faces{};
        };

        struct ScopedTimer {
            explicit ScopedTimer(std::string label)
                : label(std::move(label)),
                  begin(std::chrono::steady_clock::now()) {
            }

            ~ScopedTimer() {
                const auto end = std::chrono::steady_clock::now();
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
                HIKARI_LOG_INFO("[IblBaker] " + label + " time=" + std::to_string(ms) + "ms");
            }

            std::string label{};
            std::chrono::steady_clock::time_point begin{};
        };

        uint32_t ClampPowerOfTwoSize(uint32_t value, uint32_t minValue, uint32_t maxValue) {
            value = std::clamp(value, minValue, maxValue);
            uint32_t result = minValue;
            while ((result << 1u) <= value && (result << 1u) <= maxValue) {
                result <<= 1u;
            }
            return result;
        }

        uint32_t MaxMipCount(uint32_t size) {
            uint32_t count = 1;
            while (size > 1) {
                size >>= 1u;
                ++count;
            }
            return count;
        }

        MATH::Vec3 Add(const MATH::Vec3& lhs, const MATH::Vec3& rhs) {
            return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
        }

        MATH::Vec3 Sub(const MATH::Vec3& lhs, const MATH::Vec3& rhs) {
            return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
        }

        MATH::Vec3 Mul(const MATH::Vec3& value, float scalar) {
            return { value.x * scalar, value.y * scalar, value.z * scalar };
        }

        Float4 Add(const Float4& lhs, const Float4& rhs) {
            return { lhs.r + rhs.r, lhs.g + rhs.g, lhs.b + rhs.b, lhs.a + rhs.a };
        }

        Float4 Mul(const Float4& value, float scalar) {
            return { value.r * scalar, value.g * scalar, value.b * scalar, value.a * scalar };
        }

        Float4 Lerp(const Float4& lhs, const Float4& rhs, float t) {
            return Add(Mul(lhs, 1.0f - t), Mul(rhs, t));
        }

        float Saturate(float value) {
            return std::clamp(value, 0.0f, 1.0f);
        }

        float RadicalInverseVdc(uint32_t bits) {
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            return static_cast<float>(bits) * 2.3283064365386963e-10f;
        }

        MATH::Vec3 TangentToWorld(const MATH::Vec3& local, const MATH::Vec3& normal) {
            const MATH::Vec3 up = std::abs(normal.z) < 0.999f
                ? MATH::Vec3{ 0.0f, 0.0f, 1.0f }
                : MATH::Vec3{ 1.0f, 0.0f, 0.0f };
            const MATH::Vec3 tangent = MATH::Normalize(MATH::Cross(up, normal));
            const MATH::Vec3 bitangent = MATH::Cross(normal, tangent);

            return MATH::Normalize(Add(
                Add(Mul(tangent, local.x), Mul(bitangent, local.y)),
                Mul(normal, local.z)));
        }

        MATH::Vec3 ImportanceSampleCosineHemisphere(float u1, float u2, const MATH::Vec3& normal) {
            const float phi = 2.0f * kPi * u1;
            const float cosTheta = std::sqrt(std::max(0.0f, 1.0f - u2));
            const float sinTheta = std::sqrt(std::max(0.0f, u2));
            const MATH::Vec3 local{
                std::cos(phi) * sinTheta,
                std::sin(phi) * sinTheta,
                cosTheta
            };
            return TangentToWorld(local, normal);
        }

        MATH::Vec3 ImportanceSampleGGX(float u1, float u2, const MATH::Vec3& normal, float roughness) {
            const float a = std::max(roughness * roughness, 0.001f);
            const float phi = 2.0f * kPi * u1;
            const float cosTheta = std::sqrt((1.0f - u2) / (1.0f + (a * a - 1.0f) * u2));
            const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
            const MATH::Vec3 local{
                std::cos(phi) * sinTheta,
                std::sin(phi) * sinTheta,
                cosTheta
            };
            return TangentToWorld(local, normal);
        }

        void DirectionToCubeFaceUv(
            const MATH::Vec3& dir,
            uint32_t& outFace,
            float& outU,
            float& outV) {

            const float ax = std::abs(dir.x);
            const float ay = std::abs(dir.y);
            const float az = std::abs(dir.z);

            float u = 0.0f;
            float v = 0.0f;
            if (ax >= ay && ax >= az) {
                if (dir.x >= 0.0f) {
                    outFace = 0;
                    u = -dir.z / ax;
                    v = -dir.y / ax;
                } else {
                    outFace = 1;
                    u = dir.z / ax;
                    v = -dir.y / ax;
                }
            } else if (ay >= ax && ay >= az) {
                if (dir.y >= 0.0f) {
                    outFace = 2;
                    u = dir.x / ay;
                    v = dir.z / ay;
                } else {
                    outFace = 3;
                    u = dir.x / ay;
                    v = -dir.z / ay;
                }
            } else {
                if (dir.z >= 0.0f) {
                    outFace = 4;
                    u = dir.x / az;
                    v = -dir.y / az;
                } else {
                    outFace = 5;
                    u = -dir.x / az;
                    v = -dir.y / az;
                }
            }

            outU = Saturate(u * 0.5f + 0.5f);
            outV = Saturate(v * 0.5f + 0.5f);
        }

        MATH::Vec3 CubeFaceTexelDirection(uint32_t face, uint32_t x, uint32_t y, uint32_t size) {
            const float u = (2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(size)) - 1.0f;
            const float v = (2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(size)) - 1.0f;

            MATH::Vec3 dir{};
            switch (face) {
            case 0: dir = { 1.0f, -v, -u }; break; // +X
            case 1: dir = { -1.0f, -v, u }; break; // -X
            case 2: dir = { u, 1.0f, v }; break; // +Y
            case 3: dir = { u, -1.0f, -v }; break; // -Y
            case 4: dir = { u, -v, 1.0f }; break; // +Z
            case 5: dir = { -u, -v, -1.0f }; break; // -Z
            default: dir = { 0.0f, 0.0f, 1.0f }; break;
            }
            return MATH::Normalize(dir);
        }

        uint32_t MipSize(const CubemapImage& cube, uint32_t mip) {
            return std::max(1u, cube.width >> mip);
        }

        Float4 SampleCubemapLinear(
            const CubemapImage& cube,
            const MATH::Vec3& direction,
            uint32_t mipLevel = 0) {

            if (cube.faces.size() < 6 || cube.width == 0) {
                return {};
            }

            uint32_t face = 0;
            float u = 0.0f;
            float v = 0.0f;
            DirectionToCubeFaceUv(MATH::Normalize(direction), face, u, v);
            mipLevel = std::min(mipLevel, cube.mipLevels - 1u);
            const uint32_t size = MipSize(cube, mipLevel);
            const std::vector<Float4>& pixels = cube.faces[face][mipLevel];
            if (pixels.empty()) {
                return {};
            }

            const float fx = u * static_cast<float>(size - 1u);
            const float fy = v * static_cast<float>(size - 1u);
            const uint32_t x0 = static_cast<uint32_t>(std::floor(fx));
            const uint32_t y0 = static_cast<uint32_t>(std::floor(fy));
            const uint32_t x1 = std::min(x0 + 1u, size - 1u);
            const uint32_t y1 = std::min(y0 + 1u, size - 1u);
            const float tx = fx - static_cast<float>(x0);
            const float ty = fy - static_cast<float>(y0);

            const Float4 c00 = pixels[y0 * size + x0];
            const Float4 c10 = pixels[y0 * size + x1];
            const Float4 c01 = pixels[y1 * size + x0];
            const Float4 c11 = pixels[y1 * size + x1];
            return Lerp(Lerp(c00, c10, tx), Lerp(c01, c11, tx), ty);
        }

        CubemapImage LoadCubemapAsFloat4(const std::filesystem::path& ddsPath, std::string& outError) {
            DirectX::TexMetadata metadata{};
            DirectX::ScratchImage loaded{};
            HRESULT hr = DirectX::LoadFromDDSFile(
                ddsPath.wstring().c_str(),
                DirectX::DDS_FLAGS_NONE,
                &metadata,
                loaded);
            if (FAILED(hr)) {
                std::ostringstream oss;
                oss << "LoadFromDDSFile failed hr=0x" << std::hex << static_cast<unsigned long>(hr);
                outError = oss.str();
                return {};
            }
            if (!metadata.IsCubemap() || metadata.arraySize < 6) {
                outError = "source DDS is not a cubemap";
                return {};
            }

            DirectX::ScratchImage linear{};
            const DirectX::ScratchImage* readImage = &loaded;
            const DirectX::Image* sourceImages = loaded.GetImages();
            size_t sourceImageCount = loaded.GetImageCount();
            DirectX::TexMetadata sourceMetadata = metadata;

            DirectX::ScratchImage decompressed{};
            if (DirectX::IsCompressed(sourceMetadata.format)) {
                hr = DirectX::Decompress(
                    sourceImages,
                    sourceImageCount,
                    sourceMetadata,
                    DXGI_FORMAT_R32G32B32A32_FLOAT,
                    decompressed);
                if (FAILED(hr)) {
                    std::ostringstream oss;
                    oss << "Decompress failed hr=0x" << std::hex << static_cast<unsigned long>(hr);
                    outError = oss.str();
                    return {};
                }
                sourceImages = decompressed.GetImages();
                sourceImageCount = decompressed.GetImageCount();
                sourceMetadata = decompressed.GetMetadata();
                readImage = &decompressed;
            }

            if (sourceMetadata.format != DXGI_FORMAT_R32G32B32A32_FLOAT) {
                hr = DirectX::Convert(
                    sourceImages,
                    sourceImageCount,
                    sourceMetadata,
                    DXGI_FORMAT_R32G32B32A32_FLOAT,
                    DirectX::TEX_FILTER_DEFAULT,
                    0.0f,
                    linear);
                if (FAILED(hr)) {
                    std::ostringstream oss;
                    oss << "Convert to float4 failed hr=0x" << std::hex << static_cast<unsigned long>(hr);
                    outError = oss.str();
                    return {};
                }
                sourceImages = linear.GetImages();
                sourceMetadata = linear.GetMetadata();
                readImage = &linear;
            }

            CubemapImage cube{};
            cube.width = static_cast<uint32_t>(sourceMetadata.width);
            cube.height = static_cast<uint32_t>(sourceMetadata.height);
            cube.mipLevels = static_cast<uint32_t>(std::max<size_t>(1, sourceMetadata.mipLevels));
            cube.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            cube.faces.resize(6);

            for (uint32_t face = 0; face < 6; ++face) {
                cube.faces[face].resize(cube.mipLevels);
                for (uint32_t mip = 0; mip < cube.mipLevels; ++mip) {
                    const uint32_t size = std::max(1u, cube.width >> mip);
                    cube.faces[face][mip].resize(static_cast<size_t>(size) * size);
                    const DirectX::Image* image = readImage->GetImage(mip, face, 0);
                    if (!image) {
                        outError = "missing cubemap face image";
                        return {};
                    }

                    for (uint32_t y = 0; y < size; ++y) {
                        const auto* row = reinterpret_cast<const float*>(image->pixels + image->rowPitch * y);
                        for (uint32_t x = 0; x < size; ++x) {
                            const size_t src = static_cast<size_t>(x) * 4u;
                            cube.faces[face][mip][static_cast<size_t>(y) * size + x] = {
                                row[src + 0],
                                row[src + 1],
                                row[src + 2],
                                row[src + 3]
                            };
                        }
                    }
                }
            }

            return cube;
        }

        CubemapImage GenerateIrradianceCubemap(
            const CubemapImage& source,
            uint32_t size,
            uint32_t sampleCount) {

            CubemapImage output{};
            output.width = size;
            output.height = size;
            output.mipLevels = 1;
            output.faces.resize(6, std::vector<std::vector<Float4>>(1));

            for (uint32_t face = 0; face < 6; ++face) {
                output.faces[face][0].resize(static_cast<size_t>(size) * size);
                for (uint32_t y = 0; y < size; ++y) {
                    for (uint32_t x = 0; x < size; ++x) {
                        const MATH::Vec3 n = CubeFaceTexelDirection(face, x, y, size);
                        Float4 sum{};
                        float weightSum = 0.0f;
                        for (uint32_t i = 0; i < sampleCount; ++i) {
                            const float u1 = static_cast<float>(i) / static_cast<float>(sampleCount);
                            const float u2 = RadicalInverseVdc(i);
                            const MATH::Vec3 l = ImportanceSampleCosineHemisphere(u1, u2, n);
                            const float weight = std::max(0.0f, MATH::Dot(n, l));
                            sum = Add(sum, Mul(SampleCubemapLinear(source, l), weight));
                            weightSum += weight;
                        }

                        output.faces[face][0][static_cast<size_t>(y) * size + x] =
                            weightSum > kEpsilon ? Mul(sum, 1.0f / weightSum) : Float4{};
                    }
                }
            }

            return output;
        }

        CubemapImage GeneratePrefilteredCubemap(
            const CubemapImage& source,
            uint32_t baseSize,
            uint32_t mipCount,
            uint32_t sampleCount) {

            CubemapImage output{};
            output.width = baseSize;
            output.height = baseSize;
            output.mipLevels = mipCount;
            output.faces.resize(6, std::vector<std::vector<Float4>>(mipCount));

            for (uint32_t mip = 0; mip < mipCount; ++mip) {
                const uint32_t size = std::max(1u, baseSize >> mip);
                const float roughness = mipCount > 1
                    ? static_cast<float>(mip) / static_cast<float>(mipCount - 1u)
                    : 0.0f;

                for (uint32_t face = 0; face < 6; ++face) {
                    output.faces[face][mip].resize(static_cast<size_t>(size) * size);
                    for (uint32_t y = 0; y < size; ++y) {
                        for (uint32_t x = 0; x < size; ++x) {
                            const MATH::Vec3 r = CubeFaceTexelDirection(face, x, y, size);
                            const MATH::Vec3 n = r;
                            const MATH::Vec3 v = r;

                            Float4 sum{};
                            float totalWeight = 0.0f;
                            for (uint32_t i = 0; i < sampleCount; ++i) {
                                const float u1 = static_cast<float>(i) / static_cast<float>(sampleCount);
                                const float u2 = RadicalInverseVdc(i);
                                const MATH::Vec3 h = ImportanceSampleGGX(u1, u2, n, roughness);
                                const MATH::Vec3 l = MATH::Normalize(Sub(Mul(h, 2.0f * MATH::Dot(v, h)), v));
                                const float noL = std::max(0.0f, MATH::Dot(n, l));
                                if (noL > 0.0f) {
                                    sum = Add(sum, Mul(SampleCubemapLinear(source, l), noL));
                                    totalWeight += noL;
                                }
                            }

                            output.faces[face][mip][static_cast<size_t>(y) * size + x] =
                                totalWeight > kEpsilon ? Mul(sum, 1.0f / totalWeight) : Float4{};
                        }
                    }
                }
            }

            return output;
        }

        float GeometrySchlickGgx(float noV, float roughness) {
            const float a = roughness;
            const float k = (a * a) * 0.5f;
            return noV / std::max(noV * (1.0f - k) + k, kEpsilon);
        }

        float GeometrySmith(float noV, float noL, float roughness) {
            return GeometrySchlickGgx(noV, roughness) * GeometrySchlickGgx(noL, roughness);
        }

        std::pair<float, float> IntegrateBrdf(float noV, float roughness, uint32_t sampleCount) {
            const MATH::Vec3 v{ std::sqrt(std::max(0.0f, 1.0f - noV * noV)), 0.0f, noV };
            float a = 0.0f;
            float b = 0.0f;

            for (uint32_t i = 0; i < sampleCount; ++i) {
                const float u1 = static_cast<float>(i) / static_cast<float>(sampleCount);
                const float u2 = RadicalInverseVdc(i);
                const MATH::Vec3 h = ImportanceSampleGGX(u1, u2, { 0.0f, 0.0f, 1.0f }, roughness);
                const MATH::Vec3 l = MATH::Normalize(Sub(Mul(h, 2.0f * MATH::Dot(v, h)), v));
                const float noL = Saturate(l.z);
                const float noH = Saturate(h.z);
                const float voH = Saturate(MATH::Dot(v, h));

                if (noL > 0.0f) {
                    const float g = GeometrySmith(noV, noL, roughness);
                    const float gVis = (g * voH) / std::max(noH * noV, kEpsilon);
                    const float fc = std::pow(1.0f - voH, 5.0f);
                    a += (1.0f - fc) * gVis;
                    b += fc * gVis;
                }
            }

            const float invSampleCount = 1.0f / static_cast<float>(sampleCount);
            return { a * invSampleCount, b * invSampleCount };
        }

        DirectX::ScratchImage GenerateBrdfLutImage(uint32_t size, uint32_t sampleCount) {
            DirectX::ScratchImage image{};
            HRESULT hr = image.Initialize2D(DXGI_FORMAT_R32G32B32A32_FLOAT, size, size, 1, 1);
            if (FAILED(hr)) {
                return {};
            }

            const DirectX::Image* tex = image.GetImage(0, 0, 0);
            for (uint32_t y = 0; y < size; ++y) {
                auto* row = reinterpret_cast<float*>(tex->pixels + tex->rowPitch * y);
                const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
                for (uint32_t x = 0; x < size; ++x) {
                    const float noV = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                    const auto [a, b] = IntegrateBrdf(noV, roughness, sampleCount);
                    const size_t dst = static_cast<size_t>(x) * 4u;
                    row[dst + 0] = a;
                    row[dst + 1] = b;
                    row[dst + 2] = 0.0f;
                    row[dst + 3] = 1.0f;
                }
            }
            return image;
        }

        bool SaveCubemapToDds(
            const CubemapImage& cube,
            const std::filesystem::path& outputPath,
            DXGI_FORMAT format,
            std::string& outError) {

            std::error_code ec{};
            std::filesystem::create_directories(outputPath.parent_path(), ec);
            if (ec) {
                outError = "failed to create output directory: " + ec.message();
                return false;
            }

            DirectX::ScratchImage scratch{};
            HRESULT hr = scratch.InitializeCube(
                DXGI_FORMAT_R32G32B32A32_FLOAT,
                cube.width,
                cube.height,
                1,
                cube.mipLevels);
            if (FAILED(hr)) {
                std::ostringstream oss;
                oss << "InitializeCube failed hr=0x" << std::hex << static_cast<unsigned long>(hr);
                outError = oss.str();
                return false;
            }

            for (uint32_t face = 0; face < 6; ++face) {
                for (uint32_t mip = 0; mip < cube.mipLevels; ++mip) {
                    const uint32_t size = MipSize(cube, mip);
                    const DirectX::Image* image = scratch.GetImage(mip, face, 0);
                    const std::vector<Float4>& source = cube.faces[face][mip];
                    for (uint32_t y = 0; y < size; ++y) {
                        auto* row = reinterpret_cast<float*>(image->pixels + image->rowPitch * y);
                        for (uint32_t x = 0; x < size; ++x) {
                            const Float4& color = source[static_cast<size_t>(y) * size + x];
                            const size_t dst = static_cast<size_t>(x) * 4u;
                            row[dst + 0] = color.r;
                            row[dst + 1] = color.g;
                            row[dst + 2] = color.b;
                            row[dst + 3] = color.a;
                        }
                    }
                }
            }

            DirectX::ScratchImage converted{};
            const DirectX::ScratchImage* saveImage = &scratch;
            if (format != DXGI_FORMAT_R32G32B32A32_FLOAT) {
                hr = DirectX::Convert(
                    scratch.GetImages(),
                    scratch.GetImageCount(),
                    scratch.GetMetadata(),
                    format,
                    DirectX::TEX_FILTER_DEFAULT,
                    0.0f,
                    converted);
                if (FAILED(hr)) {
                    std::ostringstream oss;
                    oss << "Convert output failed hr=0x" << std::hex << static_cast<unsigned long>(hr);
                    outError = oss.str();
                    return false;
                }
                saveImage = &converted;
            }

            hr = DirectX::SaveToDDSFile(
                saveImage->GetImages(),
                saveImage->GetImageCount(),
                saveImage->GetMetadata(),
                DirectX::DDS_FLAGS_NONE,
                outputPath.wstring().c_str());
            if (FAILED(hr)) {
                std::ostringstream oss;
                oss << "SaveToDDSFile failed hr=0x" << std::hex << static_cast<unsigned long>(hr);
                outError = oss.str();
                return false;
            }
            return true;
        }

        bool SaveScratchToDds(
            const DirectX::ScratchImage& source,
            const std::filesystem::path& outputPath,
            DXGI_FORMAT format,
            std::string& outError) {

            std::error_code ec{};
            std::filesystem::create_directories(outputPath.parent_path(), ec);
            if (ec) {
                outError = "failed to create output directory: " + ec.message();
                return false;
            }

            DirectX::ScratchImage converted{};
            const DirectX::ScratchImage* saveImage = &source;
            if (source.GetMetadata().format != format) {
                const HRESULT hr = DirectX::Convert(
                    source.GetImages(),
                    source.GetImageCount(),
                    source.GetMetadata(),
                    format,
                    DirectX::TEX_FILTER_DEFAULT,
                    0.0f,
                    converted);
                if (FAILED(hr)) {
                    std::ostringstream oss;
                    oss << "Convert BRDF LUT failed hr=0x" << std::hex << static_cast<unsigned long>(hr);
                    outError = oss.str();
                    return false;
                }
                saveImage = &converted;
            }

            const HRESULT hr = DirectX::SaveToDDSFile(
                saveImage->GetImages(),
                saveImage->GetImageCount(),
                saveImage->GetMetadata(),
                DirectX::DDS_FLAGS_NONE,
                outputPath.wstring().c_str());
            if (FAILED(hr)) {
                std::ostringstream oss;
                oss << "Save BRDF LUT failed hr=0x" << std::hex << static_cast<unsigned long>(hr);
                outError = oss.str();
                return false;
            }
            return true;
        }

        void SetMessage(std::string* outMessage, std::string message) {
            if (outMessage) {
                *outMessage = std::move(message);
            }
        }
    }

    bool IblBaker::BakePrefilteredCubemapOnly(
        const std::filesystem::path& sourceCubemapDds,
        const std::filesystem::path& outputPrefilteredDds,
        uint32_t prefilteredSize,
        uint32_t prefilteredMipCount,
        uint32_t sampleCount,
        std::string* outMessage) {

        const uint32_t size = ClampPowerOfTwoSize(prefilteredSize, 32, 256);
        const uint32_t mipCount = std::clamp(prefilteredMipCount, 1u, MaxMipCount(size));
        const uint32_t samples = std::clamp(sampleCount, 32u, 256u);

        std::string error{};
        CubemapImage source{};
        {
            ScopedTimer timer("load reflection probe cubemap");
            source = LoadCubemapAsFloat4(sourceCubemapDds, error);
        }
        if (source.faces.empty()) {
            SetMessage(outMessage, error.empty() ? "failed to load probe cubemap" : error);
            return false;
        }

        {
            ScopedTimer timer("bake reflection probe prefiltered size=" + std::to_string(size) +
                " mips=" + std::to_string(mipCount) +
                " samples=" + std::to_string(samples));
            const CubemapImage prefiltered =
                GeneratePrefilteredCubemap(source, size, mipCount, samples);
            if (!SaveCubemapToDds(prefiltered, outputPrefilteredDds, DXGI_FORMAT_R16G16B16A16_FLOAT, error)) {
                SetMessage(outMessage, "prefiltered probe save failed: " + error);
                return false;
            }
        }

        SetMessage(outMessage, "prefiltered reflection probe baked: " + outputPrefilteredDds.generic_string());
        HIKARI_LOG_INFO("[IblBaker] baked reflection probe prefiltered: " +
            outputPrefilteredDds.generic_string());
        return true;
    }

    bool IblBaker::EnsureSharedBrdfLut(
        const std::filesystem::path& outputBrdfLutDds,
        uint32_t brdfLutSize,
        uint32_t sampleCount,
        bool forceRebake,
        std::string* outMessage) {

        if (!forceRebake && std::filesystem::exists(outputBrdfLutDds)) {
            SetMessage(outMessage, "BRDF LUT reused: " + outputBrdfLutDds.generic_string());
            return true;
        }

        const uint32_t size = ClampPowerOfTwoSize(brdfLutSize, 64, 256);
        const uint32_t samples = std::clamp(sampleCount, 64u, 512u);
        std::string error{};
        {
            ScopedTimer timer("bake shared brdf lut size=" + std::to_string(size) +
                " samples=" + std::to_string(samples));
            DirectX::ScratchImage brdf = GenerateBrdfLutImage(size, samples);
            if (brdf.GetImageCount() == 0) {
                SetMessage(outMessage, "BRDF LUT generation failed");
                return false;
            }
            if (!SaveScratchToDds(brdf, outputBrdfLutDds, DXGI_FORMAT_R16G16B16A16_FLOAT, error)) {
                SetMessage(outMessage, "BRDF LUT save failed: " + error);
                return false;
            }
        }

        SetMessage(outMessage, "BRDF LUT ready: " + outputBrdfLutDds.generic_string());
        HIKARI_LOG_INFO("[IblBaker] brdf lut ready: " + outputBrdfLutDds.generic_string());
        return true;
    }

    IblBakeResult IblBaker::BakeSkyCubemapToIbl(
        const std::filesystem::path& sourceSkyDds,
        const std::filesystem::path& outputDirectory,
        const std::filesystem::path& sharedGeneratedDirectory,
        const IblBakeSettings& settings) {

        IblBakeResult result{};
        result.irradiancePath = outputDirectory / "irradiance.dds";
        result.prefilteredPath = outputDirectory / "prefiltered.dds";
        result.brdfLutPath = sharedGeneratedDirectory / "brdf_lut.dds";

        const uint32_t irradianceSize = ClampPowerOfTwoSize(settings.irradianceSize, 16, 128);
        const uint32_t prefilteredSize = ClampPowerOfTwoSize(settings.prefilteredSize, 32, 256);
        const uint32_t maxMips = MaxMipCount(prefilteredSize);
        const uint32_t mipCount = std::clamp(settings.prefilteredMipCount, 1u, maxMips);
        const uint32_t irradianceSamples = std::clamp(settings.irradianceSampleCount, 16u, 256u);
        const uint32_t prefilteredSamples = std::clamp(settings.prefilteredSampleCount, 32u, 256u);
        const uint32_t brdfSize = ClampPowerOfTwoSize(settings.brdfLutSize, 64, 256);
        const uint32_t brdfSamples = std::clamp(settings.brdfSampleCount, 64u, 512u);

        result.irradianceSize = irradianceSize;
        result.prefilteredSize = prefilteredSize;
        result.prefilteredMipCount = mipCount;
        result.brdfLutSize = brdfSize;

        std::string error{};
        HIKARI_LOG_INFO("[IblBaker] load sky.dds " + sourceSkyDds.generic_string());
        CubemapImage source{};
        {
            ScopedTimer timer("load source cubemap");
            source = LoadCubemapAsFloat4(sourceSkyDds, error);
        }
        if (source.faces.empty()) {
            result.message = error.empty() ? "failed to load source cubemap" : error;
            return result;
        }

        {
            ScopedTimer timer("bake irradiance size=" + std::to_string(irradianceSize) +
                " samples=" + std::to_string(irradianceSamples));
            const CubemapImage irradiance = GenerateIrradianceCubemap(source, irradianceSize, irradianceSamples);
            if (!SaveCubemapToDds(irradiance, result.irradiancePath, DXGI_FORMAT_R16G16B16A16_FLOAT, error)) {
                result.message = "irradiance save failed: " + error;
                return result;
            }
        }

        {
            ScopedTimer timer("bake prefiltered size=" + std::to_string(prefilteredSize) +
                " mips=" + std::to_string(mipCount) +
                " samples=" + std::to_string(prefilteredSamples));
            const CubemapImage prefiltered =
                GeneratePrefilteredCubemap(source, prefilteredSize, mipCount, prefilteredSamples);
            if (!SaveCubemapToDds(prefiltered, result.prefilteredPath, DXGI_FORMAT_R16G16B16A16_FLOAT, error)) {
                result.message = "prefiltered save failed: " + error;
                return result;
            }
        }

        if (settings.forceRebake || !std::filesystem::exists(result.brdfLutPath)) {
            ScopedTimer timer("bake brdf lut size=" + std::to_string(brdfSize) +
                " samples=" + std::to_string(brdfSamples));
            DirectX::ScratchImage brdf = GenerateBrdfLutImage(brdfSize, brdfSamples);
            if (brdf.GetImageCount() == 0) {
                result.message = "BRDF LUT generation failed";
                return result;
            }
            if (!SaveScratchToDds(brdf, result.brdfLutPath, DXGI_FORMAT_R16G16B16A16_FLOAT, error)) {
                result.message = "BRDF LUT save failed: " + error;
                return result;
            }
        } else {
            HIKARI_LOG_INFO("[IblBaker] reuse BRDF LUT " + result.brdfLutPath.generic_string());
        }

        result.success = true;
        result.message = "IBL baked";
        HIKARI_LOG_INFO("[IblBaker] baked irradiance: " + result.irradiancePath.generic_string());
        HIKARI_LOG_INFO("[IblBaker] baked prefiltered: " + result.prefilteredPath.generic_string());
        HIKARI_LOG_INFO("[IblBaker] brdf lut ready: " + result.brdfLutPath.generic_string());
        return result;
    }

} // namespace HIKARI
