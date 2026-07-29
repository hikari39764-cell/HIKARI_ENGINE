#include "Render3D/Lighting/HIKARI_LightmapRuntime.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace HIKARI::RENDER3D::LIGHTMAP {

    namespace {

        struct LightmapRuntimeState {
            struct SurfaceKey {
                uint64_t objectId = 0u;
                uint32_t nodeIndex = 0u;
                uint32_t meshIndex = 0u;
                uint32_t primitiveIndex = 0u;

                bool operator==(
                    const SurfaceKey&) const = default;
            };

            struct SurfaceKeyHash {
                size_t operator()(
                    const SurfaceKey& key) const noexcept {
                    size_t hash =
                        std::hash<uint64_t>{}(key.objectId);
                    const auto combine =
                        [&hash](uint32_t value) {
                            hash ^=
                                std::hash<uint32_t>{}(value) +
                                0x9e3779b9u +
                                (hash << 6u) +
                                (hash >> 2u);
                        };
                    combine(key.nodeIndex);
                    combine(key.meshIndex);
                    combine(key.primitiveIndex);
                    return hash;
                }
            };

            std::vector<LightmapRuntimeBinding> bindings{};
            std::unordered_map<
                SurfaceKey,
                size_t,
                SurfaceKeyHash> indexBySurface{};
        };

        LightmapRuntimeState& State() {
            static LightmapRuntimeState state{};
            return state;
        }

        void RebuildIndex(LightmapRuntimeState& state) {
            state.indexBySurface.clear();
            for (size_t i = 0; i < state.bindings.size(); ++i) {
                const LightmapRuntimeBinding& binding =
                    state.bindings[i];
                state.indexBySurface[{
                    binding.objectId,
                    binding.surface.nodeIndex,
                    binding.surface.meshIndex,
                    binding.surface.primitiveIndex
                }] = i;
            }
        }

    } // namespace

    void Reset() {
        State() = {};
    }

    bool Register(LightmapRuntimeBinding binding) {
        if (binding.objectId == 0u ||
            !binding.surface.IsValid() ||
            !binding.texture ||
            binding.textureDescriptorIndex ==
                kInvalidLightmapTextureDescriptorIndex ||
            binding.uvSet !=
                ASSETS::LIGHTING::kSurfaceLightmapUvSet) {
            return false;
        }

        LightmapRuntimeState& state = State();
        const auto found =
            state.indexBySurface.find({
                binding.objectId,
                binding.surface.nodeIndex,
                binding.surface.meshIndex,
                binding.surface.primitiveIndex
            });
        if (found != state.indexBySurface.end()) {
            state.bindings[found->second] = std::move(binding);
            RebuildIndex(state);
            return true;
        }

        state.bindings.push_back(std::move(binding));
        RebuildIndex(state);
        return true;
    }

    const LightmapRuntimeBinding* Find(
        uint64_t objectId,
        uint32_t nodeIndex,
        uint32_t meshIndex,
        uint32_t primitiveIndex) {
        const LightmapRuntimeState& state = State();
        const auto found = state.indexBySurface.find({
            objectId,
            nodeIndex,
            meshIndex,
            primitiveIndex
        });
        return found == state.indexBySurface.end()
            ? nullptr
            : &state.bindings[found->second];
    }

    const std::vector<LightmapRuntimeBinding>& GetBindings() {
        return State().bindings;
    }

} // namespace HIKARI::RENDER3D::LIGHTMAP
