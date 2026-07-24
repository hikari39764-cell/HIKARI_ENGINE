#include "Assets/Collision/HIKARI_CoacdCollisionGenerator.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <mutex>

#define NOMINMAX
#include <Windows.h>

#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "coacd_c_api.h"

namespace HIKARI::ASSETS::COLLISION {
    namespace {
        class CoacdModule final {
        public:
            ~CoacdModule() {
                if (module_ != nullptr) {
                    FreeLibrary(module_);
                }
            }

            bool Load(std::string& outMessage) {
                if (module_ != nullptr) {
                    if (run_ != nullptr && free_ != nullptr &&
                        setLogLevel_ != nullptr) {
                        outMessage.clear();
                        return true;
                    }
                    outMessage = "CoACD DLL has an incompatible C API";
                    return false;
                }
                std::array<wchar_t, 32768> executablePath{};
                const DWORD length = GetModuleFileNameW(
                    nullptr,
                    executablePath.data(),
                    static_cast<DWORD>(executablePath.size()));
                std::vector<std::filesystem::path> candidates{};
                if (length > 0u && length < executablePath.size()) {
                    candidates.push_back(
                        std::filesystem::path(executablePath.data())
                            .parent_path() / "lib_coacd.dll");
                }
                candidates.emplace_back(
                    "ThirdParty/CoACD/bin/win-x64/lib_coacd.dll");
                for (const std::filesystem::path& path : candidates) {
                    module_ = LoadLibraryW(path.c_str());
                    if (module_ != nullptr) {
                        loadedPath_ = path;
                        break;
                    }
                }
                if (module_ == nullptr) {
                    outMessage =
                        "CoACD 1.0.11 is unavailable; lib_coacd.dll was not found";
                    return false;
                }
                run_ = reinterpret_cast<CoACD_RunFn>(GetProcAddress(
                    module_,
                    "CoACD_run"));
                free_ = reinterpret_cast<CoACD_FreeMeshArrayFn>(GetProcAddress(
                    module_,
                    "CoACD_freeMeshArray"));
                setLogLevel_ = reinterpret_cast<CoACD_SetLogLevelFn>(
                    GetProcAddress(module_, "CoACD_setLogLevel"));
                if (run_ == nullptr || free_ == nullptr ||
                    setLogLevel_ == nullptr) {
                    outMessage = "CoACD DLL has an incompatible C API";
                    return false;
                }
                setLogLevel_("warn");
                outMessage.clear();
                return true;
            }

            CoACD_RunFn Run() const noexcept { return run_; }
            CoACD_FreeMeshArrayFn Free() const noexcept { return free_; }

        private:
            HMODULE module_ = nullptr;
            std::filesystem::path loadedPath_{};
            CoACD_RunFn run_ = nullptr;
            CoACD_FreeMeshArrayFn free_ = nullptr;
            CoACD_SetLogLevelFn setLogLevel_ = nullptr;
        };

        void ComputeBounds(ModelCollisionMeshData& mesh) {
            Bounds bounds = BOUNDS::EmptyBounds();
            for (const MATH::Vec3& vertex : mesh.vertices) {
                BOUNDS::Encapsulate(bounds, vertex);
            }
            mesh.bounds = bounds;
        }

        CoacdModule& SharedModule() {
            static CoacdModule module{};
            return module;
        }

        std::mutex& SharedModuleMutex() {
            static std::mutex mutex{};
            return mutex;
        }
    }

    bool IsCoacdCollisionGeneratorAvailable(std::string& outMessage) {
        std::scoped_lock lock(SharedModuleMutex());
        return SharedModule().Load(outMessage);
    }

    bool GenerateCoacdCollisionParts(
        const ModelCollisionMeshData& input,
        const CoacdCollisionSettings& settings,
        std::vector<ModelCollisionMeshData>& outParts,
        std::string& outMessage) {

        outParts.clear();
        if (!input.IsUsable() ||
            input.vertices.size() >
                static_cast<size_t>((std::numeric_limits<int>::max)()) ||
            input.indices.size() / 3u >
                static_cast<size_t>((std::numeric_limits<int>::max)())) {
            outMessage = "CoACD input mesh is invalid or too large";
            return false;
        }
        std::scoped_lock lock(SharedModuleMutex());
        CoacdModule& module = SharedModule();
        if (!module.Load(outMessage)) {
            return false;
        }

        std::vector<double> vertices{};
        vertices.reserve(input.vertices.size() * 3u);
        for (const MATH::Vec3& vertex : input.vertices) {
            vertices.push_back(vertex.x);
            vertices.push_back(vertex.y);
            vertices.push_back(vertex.z);
        }
        std::vector<int> triangles{};
        triangles.reserve(input.indices.size());
        for (uint32_t index : input.indices) {
            if (index >= input.vertices.size()) {
                outMessage = "CoACD input contains an invalid triangle index";
                return false;
            }
            triangles.push_back(static_cast<int>(index));
        }
        CoACD_Mesh source{
            vertices.data(),
            static_cast<uint64_t>(input.vertices.size()),
            triangles.data(),
            static_cast<uint64_t>(triangles.size() / 3u)
        };
        CoACD_MeshArray generated = module.Run()(
            source,
            (std::clamp)(settings.threshold, 0.001, 1.0),
            settings.maximumConvexHulls,
            0,
            50,
            2000,
            20,
            150,
            3,
            false,
            settings.mergeParts,
            true,
            (std::clamp)(settings.maximumHullVertices, 16, 256),
            false,
            0.01,
            0,
            settings.seed,
            true);
        if (generated.meshes_ptr == nullptr || generated.meshes_count == 0u) {
            if (generated.meshes_ptr != nullptr) {
                module.Free()(generated);
            }
            outMessage = "CoACD did not produce any convex parts";
            return false;
        }

        outParts.reserve(static_cast<size_t>(generated.meshes_count));
        bool valid = true;
        for (uint64_t partIndex = 0u;
            partIndex < generated.meshes_count;
            ++partIndex) {
            const CoACD_Mesh& sourcePart = generated.meshes_ptr[partIndex];
            if (sourcePart.vertices_ptr == nullptr ||
                sourcePart.triangles_ptr == nullptr ||
                sourcePart.vertices_count < 4u ||
                sourcePart.triangles_count == 0u ||
                sourcePart.vertices_count >
                    (std::numeric_limits<uint32_t>::max)()) {
                valid = false;
                break;
            }
            ModelCollisionMeshData part{};
            part.sourceNodeIndices = input.sourceNodeIndices;
            part.vertices.reserve(static_cast<size_t>(
                sourcePart.vertices_count));
            for (uint64_t vertexIndex = 0u;
                vertexIndex < sourcePart.vertices_count;
                ++vertexIndex) {
                const size_t offset = static_cast<size_t>(vertexIndex * 3u);
                part.vertices.push_back({
                    static_cast<float>(sourcePart.vertices_ptr[offset]),
                    static_cast<float>(sourcePart.vertices_ptr[offset + 1u]),
                    static_cast<float>(sourcePart.vertices_ptr[offset + 2u])
                });
            }
            part.indices.reserve(static_cast<size_t>(
                sourcePart.triangles_count * 3u));
            for (uint64_t triangleIndex = 0u;
                triangleIndex < sourcePart.triangles_count;
                ++triangleIndex) {
                const size_t offset = static_cast<size_t>(triangleIndex * 3u);
                for (size_t corner = 0u; corner < 3u; ++corner) {
                    const int index = sourcePart.triangles_ptr[offset + corner];
                    if (index < 0 ||
                        static_cast<uint64_t>(index) >=
                            sourcePart.vertices_count) {
                        valid = false;
                        break;
                    }
                    part.indices.push_back(static_cast<uint32_t>(index));
                }
                if (!valid) {
                    break;
                }
            }
            if (!valid) {
                break;
            }
            ComputeBounds(part);
            outParts.push_back(std::move(part));
        }
        module.Free()(generated);
        if (!valid || outParts.empty()) {
            outParts.clear();
            outMessage = "CoACD returned invalid convex geometry";
            return false;
        }
        outMessage.clear();
        return true;
    }

} // namespace HIKARI::ASSETS::COLLISION
