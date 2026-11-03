#include "Render3D/Material/HIKARI_GpuMaterialSnapshot.h"

#include <cstring>
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Material/Program/HIKARI_GpuMaterialProgramBinding.h"

namespace HIKARI::RENDER3D::MATERIAL {

GpuMaterialSnapshot::~GpuMaterialSnapshot() { Reset(); }

void GpuMaterialSnapshot::Reset() {
    const auto context = GFX::GetCurrentContext();
    upload_.Finalize(&context);
    materialAddress_ = parameterAddress_ = resourceAddress_ = 0;
}

bool GpuMaterialSnapshot::Initialize(ID3D12Device* device, std::span<const MaterialSnapshot> materials,
                                     const TextureResourceSnapshot& textures, std::string& error) {
    Reset();
    error.clear();
    const auto fail = [&](std::string message) {
        error = std::move(message);
        Reset();
        return false;
    };
    if (!device || materials.empty() || materials.size() > kDefaultGpuMaterialCapacity || !textures.GetHeap())
        return fail("Invalid material snapshot size, device or texture snapshot.");
    for (const auto& material : materials) {
        if (!material.error.empty())
            return fail(material.error);
        if (material.programParameters.size() > kGpuMaterialProgramParameterSlotCount ||
            material.programTextures.size() > kGpuMaterialProgramResourceSlotCount)
            return fail("Material program exceeds the shared parameter or resource budget.");
    }
    const auto resolve = [&](const TextureSnapshotSource& source, uint32_t& index) {
        index = textures.GetDescriptorIndex(source);
        if (!source.enabled)
            return true;
        if (source.dimension == TextureResourceDimension::Texture2D && index != kInvalidTextureDescriptorIndex)
            return true;
        error = "Material requires a captured Texture2D view: " + source.load.path;
        return false;
    };

    std::vector<GpuMaterialData> table;
    std::vector<MATH::Vec4> parameters;
    std::vector<uint32_t> resources;
    for (const auto& material : materials) {
        auto projection = material.projection;
        for (size_t i = 0; i < material.textures.size(); ++i) {
            auto& texture = projection.textures[i];
            texture.backendHandle = -1;
            texture.enabled = material.textures[i].enabled;
            if (!resolve(material.textures[i], texture.descriptorIndex))
                return fail(error);
        }
        auto data = PackGpuMaterialData(projection);
        if (material.hasProgram) {
            GpuMaterialProgramResourceBlock resolvedResources;
            resolvedResources.fill(kInvalidTextureDescriptorIndex);
            for (size_t i = 0; i < material.programTextures.size(); ++i)
                if (!resolve(material.programTextures[i], resolvedResources[i]))
                    return fail(error);
            if (!AppendGpuMaterialProgramBlock(data, material.programBinding, material.programParameters,
                                               resolvedResources, parameters, resources))
                return fail("Failed to pack material program snapshot.");
        }
        table.push_back(data);
    }
    if (parameters.empty())
        parameters.emplace_back();
    if (resources.empty())
        resources.push_back(kInvalidTextureDescriptorIndex);
    const size_t totalBytes = table.size() * sizeof(GpuMaterialData) + parameters.size() * sizeof(MATH::Vec4) +
                              resources.size() * sizeof(uint32_t) + 3u * 256u;
    if (!upload_.Init(device, totalBytes, GFX::UPLOAD_TRAFFIC::Owner::Materials))
        return fail("Failed to allocate material snapshot upload.");
    const auto upload = [&](const auto& values, D3D12_GPU_VIRTUAL_ADDRESS& address) {
        const size_t bytes = values.size() * sizeof(values[0]);
        void* destination = upload_.Allocate(bytes, address);
        if (!destination)
            return false;
        std::memcpy(destination, values.data(), bytes);
        return true;
    };
    if (!upload(table, materialAddress_) || !upload(parameters, parameterAddress_) ||
        !upload(resources, resourceAddress_))
        return fail("Failed to upload material snapshot.");
    return true;
}

} // namespace HIKARI::RENDER3D::MATERIAL
