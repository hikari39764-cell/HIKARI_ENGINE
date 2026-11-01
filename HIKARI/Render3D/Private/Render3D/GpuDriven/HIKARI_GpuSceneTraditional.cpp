#include "Render3D/GpuDriven/HIKARI_GpuSceneRegistry.h"
#include "Render3D/GpuDriven/CommandStream/HIKARI_GpuTraditionalCommandStreamBuffer.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"
#include "Core/Numeric/HIKARI_IntegerConversion.h"

#include <algorithm>
#include <limits>

namespace HIKARI::RENDER3D::GPUDRIVEN {
namespace {
RUNTIME::SurfaceDrawIndexedArgs BuildDrawIndexedArgs(const GpuSceneSurfaceRecord& record) {
    RUNTIME::SurfaceDrawIndexedArgs args{};
    if (record.model == nullptr || record.meshIndex >= record.model->meshes.size()) {
        return args;
    }
    const MeshAsset& mesh = record.model->meshes[record.meshIndex];
    if (record.primitiveIndex >= mesh.primitives.size()) {
        return args;
    }
    args.indexCountPerInstance = NUMERIC::SaturateToUint32(mesh.primitives[record.primitiveIndex].indices.size());
    args.instanceCount = 1u;
    return args;
}

std::string ResolveTraditionalMaterialFxPixelShaderId(std::string pixelShaderId) {
    if (pixelShaderId == "Render3D_StaticFxPS" || pixelShaderId == "StaticFx" || pixelShaderId == "MaterialFx") {
        return "Render3D_StaticFxPS";
    }
    return pixelShaderId;
}

VFX::VariantKey BuildTraditionalVariantKey(const GpuSceneSurfaceRecord& record, RUNTIME::SurfaceDrawCommandPass pass) {
    VFX::VariantKey key{};
    key.shaderId = "StaticLit";
    key.pixelShaderId = "StaticLit";
    key.composite = VFX::CompositeMode::Replace;
    key.depthTest = true;
    key.depthWrite = pass != RUNTIME::SurfaceDrawCommandPass::DepthAware && !record.key.transparent;
    key.doubleSided = record.key.doubleSided;

    if (record.key.materialFx) {
        MaterialFxProfile profile{};
        if (MaterialFxProfile::LoadById(record.materialFxProfileId, profile)) {
            key.shaderId = profile.shaderProfileId.empty() ? "StaticFx" : profile.shaderProfileId;
            key.vertexShaderId = profile.vertexShaderId;
            const std::string pixelShaderId = !profile.pixelShaderId.empty() ? profile.pixelShaderId : key.shaderId;
            key.pixelShaderId = ResolveTraditionalMaterialFxPixelShaderId(pixelShaderId);
            key.featureBits = profile.featureBits;
            key.composite = profile.composite;
            key.depthTest = profile.depthTest;
            key.depthWrite = profile.depthWrite;
            key.doubleSided = record.key.doubleSided || profile.doubleSided;
        } else {
            key.shaderId = "StaticFx";
            key.pixelShaderId = "Render3D_StaticFxPS";
        }
    }

    if (record.key.transparent || pass == RUNTIME::SurfaceDrawCommandPass::DepthAware) {
        key.depthWrite = false;
    }
    return key;
}

bool AssignTraditionalBucket(GpuSceneRegistry::TraditionalIndirectStream& stream, const VFX::VariantKey& variant,
                             RUNTIME::SurfaceDrawCommand& command) {
    for (size_t i = 0; i < stream.bucketVariants.size(); ++i) {
        if (stream.bucketVariants[i] == variant) {
            command.traditionalVariant = variant;
            command.traditionalBucketIndex = NUMERIC::SaturateToUint32(i);
            return true;
        }
    }

    if (stream.bucketVariants.size() >= kGpuTraditionalCommandBucketCount) {
        return false;
    }

    command.traditionalVariant = variant;
    command.traditionalBucketIndex = NUMERIC::SaturateToUint32(stream.bucketVariants.size());
    stream.bucketVariants.push_back(variant);
    return true;
}

bool IsValidDrawArgs(const RUNTIME::SurfaceDrawIndexedArgs& args) {
    return args.indexCountPerInstance > 0u && args.instanceCount > 0u;
}

void FillTraditionalDrawCommand(const GpuSceneSurfaceRecord& record, uint32_t recordIndexInStream,
                                uint32_t executableIndex, uint32_t gpuSceneInstanceIndex,
                                RUNTIME::SurfaceDrawCommandPass pass, bool clusterMainlineEligible,
                                RUNTIME::SurfaceDrawCommand& command) {
    command = {};
    command.pass = pass;
    command.firstExecutableIndex = executableIndex;
    command.recordCount = 1u;
    command.firstRecordIndex = recordIndexInStream;
    command.firstGpuSceneInstanceIndex = gpuSceneInstanceIndex;
    command.gpuSceneInstanceCount = 1u;
    command.batchKey = RUNTIME::BuildSurfaceDrawBatchKey(pass, record.key);
    command.resources = record.key.resources;
    command.psoKey = record.key.psoKey;
    command.geometryKey = record.key.geometryKey;
    command.geometryBackend = record.key.geometryBackend;
    command.backendRoute = record.key.backendRoute;
    command.materialKey = record.key.materialKey;
    command.textureSetKey = record.key.textureSetKey;
    command.modelKey = record.key.modelKey;
    command.singleRecord = true;
    command.transparent = record.key.transparent;
    command.alphaMasked = record.key.alphaMasked;
    command.doubleSided = record.key.doubleSided;
    command.materialFx = record.key.materialFx;
    command.waterMaterialFx = record.key.waterMaterialFx;
    command.materialFxUsesCustomVertexShader = record.key.materialFxUsesCustomVertexShader;
    command.traditionalVariant = BuildTraditionalVariantKey(record, pass);
    command.clusterMainlineEligible = clusterMainlineEligible;
    command.drawArgs = BuildDrawIndexedArgs(record);
    command.drawArgsValid = IsValidDrawArgs(command.drawArgs);
}

bool AppendTraditionalRecordInstance(const std::vector<GpuSceneSurfaceRecord>& records, uint32_t sourceRecordIndex,
                                     GpuSceneRegistry::TraditionalIndirectStream& stream) {
    std::vector<uint32_t> singleRecordIndex{sourceRecordIndex};
    std::vector<RUNTIME::SurfaceGpuSceneInstance> singleInstance{};
    std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> singleMaterial{};
    (void)BuildGpuSceneInstanceList(records, singleRecordIndex, singleInstance, singleMaterial);
    if (singleInstance.empty() || singleMaterial.empty()) {
        return false;
    }

    const uint32_t streamInstanceIndex = NUMERIC::SaturateToUint32(stream.instances.size());
    singleMaterial.front().localGpuSceneInstanceIndex = streamInstanceIndex;
    // Material residency is indexed by the global scene record table.
    // The command stream has its own local record index; mixing the two
    // makes pass-local records overwrite unrelated material bindings.
    singleInstance.front().sourceRecordIndex = sourceRecordIndex;
    singleMaterial.front().sourceRecordIndex = sourceRecordIndex;
    singleMaterial.front().sourceSurfaceInstanceIndex = singleInstance.front().sourceSurfaceInstanceIndex;
    stream.instances.push_back(singleInstance.front());
    stream.materialSources.push_back(singleMaterial.front());
    return true;
}

} // namespace

void GpuSceneRegistry::TraditionalIndirectStream::Append(const std::vector<GpuSceneSurfaceRecord>& sourceRecords,
                                                         uint32_t recordIndex, RUNTIME::SurfaceDrawCommandPass pass) {
    auto& stream = *this;
    if (recordIndex >= sourceRecords.size())
        return;
    const auto& record = sourceRecords[recordIndex];
    if (!HasValidGpuSceneSubmitPrimitiveTarget(record))
        return;
    const uint32_t recordIndexInStream = NUMERIC::SaturateToUint32(stream.records.size());
    const uint32_t executableIndex = NUMERIC::SaturateToUint32(stream.executableRecordIndices.size());
    const uint32_t instanceIndex = NUMERIC::SaturateToUint32(stream.instances.size());
    if (!AppendTraditionalRecordInstance(sourceRecords, recordIndex, stream))
        return;
    RUNTIME::SurfaceDrawCommand command{};
    FillTraditionalDrawCommand(record, recordIndexInStream, executableIndex, instanceIndex, pass,
                               record.key.clusterMainlineEligible, command);
    if (!AssignTraditionalBucket(stream, command.traditionalVariant, command)) {
        stream.instances.pop_back();
        stream.materialSources.pop_back();
        return;
    }
    // Shadow の静的/動的分割は primary と同じ pass 資格を参照する。
    if (pass == RUNTIME::SurfaceDrawCommandPass::Shadow) {
        stream.instances[instanceIndex].flags |=
            static_cast<uint32_t>(RUNTIME::SurfaceGpuSceneInstanceFlags::PassShadow);
    }
    stream.records.push_back(record);
    stream.executableRecordIndices.push_back(recordIndexInStream);
    stream.jointPalettes.emplace_back();
    stream.commands.push_back(command);
    ++stream.staticCommandCount;
}

void GpuSceneRegistry::TraditionalIndirectStream::Attach(GpuDrivenPassSource& pass, uint32_t gpuSceneBaseIndex) const {
    const auto& stream = *this;

    pass.traditionalIndirect.Reset();
    if (!stream.HasCommands()) {
        return;
    }
    pass.traditionalIndirect.records = &stream.records;
    pass.traditionalIndirect.executableRecordIndices = &stream.executableRecordIndices;
    pass.traditionalIndirect.commands = &stream.commands;
    pass.traditionalIndirect.instances = &stream.instances;
    pass.traditionalIndirect.materialSources = &stream.materialSources;
    pass.traditionalIndirect.jointPalettes = &stream.jointPalettes;
    pass.traditionalIndirect.bucketVariants = &stream.bucketVariants;
    pass.traditionalIndirect.gpuSceneBaseIndex = gpuSceneBaseIndex;
    pass.traditionalIndirect.gpuSceneInstanceCount = NUMERIC::SaturateToUint32(stream.instances.size());
    pass.traditionalIndirect.staticCommandCount = stream.staticCommandCount;
}

void GpuSceneRegistry::TraditionalIndirectStream::Clear() {
    records.clear();
    executableRecordIndices.clear();
    commands.clear();
    instances.clear();
    materialSources.clear();
    jointPalettes.clear();
    bucketVariants.clear();
    staticCommandCount = 0;
}

bool GpuSceneRegistry::TraditionalIndirectStream::HasCommands() const {
    return !records.empty() && !commands.empty() && !executableRecordIndices.empty() && !bucketVariants.empty() &&
           records.size() == instances.size() && records.size() == materialSources.size() &&
           records.size() == jointPalettes.size();
}

} // namespace HIKARI::RENDER3D::GPUDRIVEN
