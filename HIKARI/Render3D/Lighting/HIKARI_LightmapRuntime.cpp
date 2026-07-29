#include "Render3D/Lighting/HIKARI_LightmapRuntime.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace HIKARI::RENDER3D::LIGHTMAP {

    namespace {

        struct LightmapRuntimeState {
            std::vector<LightmapRuntimeBinding> bindings{};
            std::unordered_map<uint64_t, size_t> indexByObjectId{};
        };

        LightmapRuntimeState& State() {
            static LightmapRuntimeState state{};
            return state;
        }

        void RebuildIndex(LightmapRuntimeState& state) {
            state.indexByObjectId.clear();
            for (size_t i = 0; i < state.bindings.size(); ++i) {
                state.indexByObjectId[
                    state.bindings[i].objectId] = i;
            }
        }

    } // namespace

    void Reset() {
        State() = {};
    }

    bool Register(LightmapRuntimeBinding binding) {
        if (binding.objectId == 0u ||
            !binding.texture ||
            binding.textureDescriptorIndex ==
                kInvalidLightmapTextureDescriptorIndex ||
            binding.uvSet !=
                ASSETS::LIGHTING::kSurfaceLightmapUvSet) {
            return false;
        }

        LightmapRuntimeState& state = State();
        const auto found =
            state.indexByObjectId.find(binding.objectId);
        if (found != state.indexByObjectId.end()) {
            state.bindings[found->second] = std::move(binding);
            return true;
        }

        state.bindings.push_back(std::move(binding));
        RebuildIndex(state);
        return true;
    }

    const LightmapRuntimeBinding* Find(uint64_t objectId) {
        const LightmapRuntimeState& state = State();
        const auto found = state.indexByObjectId.find(objectId);
        return found == state.indexByObjectId.end()
            ? nullptr
            : &state.bindings[found->second];
    }

    const std::vector<LightmapRuntimeBinding>& GetBindings() {
        return State().bindings;
    }

} // namespace HIKARI::RENDER3D::LIGHTMAP
