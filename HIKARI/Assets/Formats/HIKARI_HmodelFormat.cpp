#include "HIKARI_HmodelFormat.h"

#include <algorithm>
#include <fstream>
#include <utility>

#include "Core/Serialization/Binary/HIKARI_BinaryStream.h"

namespace HIKARI {

    namespace {
        constexpr uint32_t kHmodelMagic = 0x4C444D48u; // 'HMDL'
        constexpr uint32_t kHmodelVersion = 4;
        constexpr uint32_t kMaxStringBytes = 16u * 1024u * 1024u;
        constexpr uint32_t kMaxVectorCount = 16u * 1024u * 1024u;

        struct HmodelFileHeader {
            uint32_t magic = kHmodelMagic;
            uint32_t version = kHmodelVersion;
            uint32_t headerSize = sizeof(HmodelFileHeader);
            uint32_t reserved = 0;
        };

        using SERIALIZATION::BINARY::STREAM::ReadBoolean8;
        using SERIALIZATION::BINARY::STREAM::ReadLengthPrefixedString32;
        using SERIALIZATION::BINARY::STREAM::
            ReadLengthPrefixedTrivialVector32;
        using SERIALIZATION::BINARY::STREAM::ReadTrivial;
        using SERIALIZATION::BINARY::STREAM::WriteBoolean8;
        using SERIALIZATION::BINARY::STREAM::WriteLengthPrefixedString32;
        using SERIALIZATION::BINARY::STREAM::
            WriteLengthPrefixedTrivialVector32;
        using SERIALIZATION::BINARY::STREAM::WriteTrivial;

        template<class T>
        struct LegacyAnimationKeyframe {
            float timeSec = 0.0f;
            T value{};
        };

        template<class T>
        bool WriteAnimationKeyframes(
            std::ofstream& ofs,
            const std::vector<AnimationKeyframe<T>>& keys) {
            if (keys.size() > kMaxVectorCount) return false;
            const uint32_t count = static_cast<uint32_t>(keys.size());
            if (!WriteTrivial(ofs, count)) return false;
            for (const AnimationKeyframe<T>& key : keys) {
                if (!WriteTrivial(ofs, key.timeSec) ||
                    !WriteTrivial(ofs, key.value) ||
                    !WriteTrivial(ofs, key.inTangent) ||
                    !WriteTrivial(ofs, key.outTangent)) {
                    return false;
                }
            }
            return true;
        }

        template<class T>
        bool ReadAnimationKeyframes(
            std::ifstream& ifs,
            uint32_t version,
            std::vector<AnimationKeyframe<T>>& keys) {
            if (version < 4u) {
                std::vector<LegacyAnimationKeyframe<T>> legacy{};
                if (!ReadLengthPrefixedTrivialVector32<kMaxVectorCount>(ifs, legacy)) return false;
                keys.resize(legacy.size());
                for (size_t index = 0u; index < legacy.size(); ++index) {
                    keys[index].timeSec = legacy[index].timeSec;
                    keys[index].value = legacy[index].value;
                }
                return true;
            }

            uint32_t count = 0u;
            if (!ReadTrivial(ifs, count) || count > kMaxVectorCount) {
                return false;
            }
            keys.resize(count);
            for (AnimationKeyframe<T>& key : keys) {
                if (!ReadTrivial(ifs, key.timeSec) ||
                    !ReadTrivial(ifs, key.value) ||
                    !ReadTrivial(ifs, key.inTangent) ||
                    !ReadTrivial(ifs, key.outTangent)) {
                    return false;
                }
            }
            return true;
        }

        bool WriteTextureSlot(std::ofstream& ofs, const TextureSlot& slot) {
            return WriteTrivial(ofs, slot.textureIndex) &&
                WriteTrivial(ofs, slot.texCoord) &&
                WriteTrivial(ofs, slot.uvScale) &&
                WriteTrivial(ofs, slot.uvOffset) &&
                WriteTrivial(ofs, slot.uvRotation) &&
                WriteTrivial(ofs, slot.scale) &&
                WriteTrivial(ofs, slot.strength);
        }

        bool ReadTextureSlot(std::ifstream& ifs, TextureSlot& slot, uint32_t version) {
            if (!ReadTrivial(ifs, slot.textureIndex) ||
                !ReadTrivial(ifs, slot.texCoord)) {
                return false;
            }
            slot.texCoord = std::clamp(slot.texCoord, 0, 1);
            if (version >= 3u &&
                (!ReadTrivial(ifs, slot.uvScale) ||
                 !ReadTrivial(ifs, slot.uvOffset) ||
                 !ReadTrivial(ifs, slot.uvRotation))) {
                return false;
            }
            return ReadTrivial(ifs, slot.scale) &&
                ReadTrivial(ifs, slot.strength);
        }

        bool WriteMaterial(std::ofstream& ofs, const MaterialAsset& material) {
            return WriteLengthPrefixedString32<kMaxStringBytes>(ofs, material.name) &&
                WriteTrivial(ofs, material.baseColorFactor) &&
                WriteTrivial(ofs, material.metallicFactor) &&
                WriteTrivial(ofs, material.roughnessFactor) &&
                WriteTrivial(ofs, material.specularFactor) &&
                WriteTrivial(ofs, material.specularColorFactor) &&
                WriteTrivial(ofs, material.emissiveFactor) &&
                WriteTrivial(ofs, material.emissiveStrength) &&
                WriteTextureSlot(ofs, material.baseColorTexture) &&
                WriteTextureSlot(ofs, material.normalTexture) &&
                WriteTextureSlot(ofs, material.metallicRoughnessTexture) &&
                WriteTextureSlot(ofs, material.occlusionTexture) &&
                WriteTextureSlot(ofs, material.emissiveTexture) &&
                WriteTextureSlot(ofs, material.specularTexture) &&
                WriteTextureSlot(ofs, material.specularColorTexture) &&
                WriteTrivial(ofs, material.alphaMode) &&
                WriteTrivial(ofs, material.alphaCutoff) &&
                WriteBoolean8(ofs, material.doubleSided) &&
                WriteLengthPrefixedString32<kMaxStringBytes>(ofs, material.shaderProfileId) &&
                WriteLengthPrefixedString32<kMaxStringBytes>(ofs, material.defaultMaterialFxProfileId) &&
                WriteTrivial(ofs, material.featureBits);
        }

        bool ReadMaterial(std::ifstream& ifs, MaterialAsset& material, uint32_t version) {
            if (!ReadLengthPrefixedString32<kMaxStringBytes>(ifs, material.name) ||
                !ReadTrivial(ifs, material.baseColorFactor) ||
                !ReadTrivial(ifs, material.metallicFactor) ||
                !ReadTrivial(ifs, material.roughnessFactor)) {
                return false;
            }

            if (version >= 2u &&
                (!ReadTrivial(ifs, material.specularFactor) ||
                 !ReadTrivial(ifs, material.specularColorFactor))) {
                return false;
            }

            return ReadTrivial(ifs, material.emissiveFactor) &&
                ReadTrivial(ifs, material.emissiveStrength) &&
                ReadTextureSlot(ifs, material.baseColorTexture, version) &&
                ReadTextureSlot(ifs, material.normalTexture, version) &&
                ReadTextureSlot(ifs, material.metallicRoughnessTexture, version) &&
                ReadTextureSlot(ifs, material.occlusionTexture, version) &&
                ReadTextureSlot(ifs, material.emissiveTexture, version) &&
                (version < 2u || ReadTextureSlot(ifs, material.specularTexture, version)) &&
                (version < 2u || ReadTextureSlot(ifs, material.specularColorTexture, version)) &&
                ReadTrivial(ifs, material.alphaMode) &&
                ReadTrivial(ifs, material.alphaCutoff) &&
                ReadBoolean8(ifs, material.doubleSided) &&
                ReadLengthPrefixedString32<kMaxStringBytes>(ifs, material.shaderProfileId) &&
                ReadLengthPrefixedString32<kMaxStringBytes>(ifs, material.defaultMaterialFxProfileId) &&
                ReadTrivial(ifs, material.featureBits);
        }

        bool WritePrimitive(std::ofstream& ofs, const MeshPrimitive& primitive) {
            return WriteLengthPrefixedString32<kMaxStringBytes>(ofs, primitive.name) &&
                WriteTrivial(ofs, primitive.layout) &&
                WriteLengthPrefixedTrivialVector32<kMaxVectorCount>(ofs, primitive.staticVertices) &&
                WriteLengthPrefixedTrivialVector32<kMaxVectorCount>(ofs, primitive.skinnedVertices) &&
                WriteLengthPrefixedTrivialVector32<kMaxVectorCount>(ofs, primitive.indices) &&
                WriteTrivial(ofs, primitive.materialIndex) &&
                WriteTrivial(ofs, primitive.bounds);
        }

        bool ReadPrimitive(std::ifstream& ifs, MeshPrimitive& primitive) {
            return ReadLengthPrefixedString32<kMaxStringBytes>(ifs, primitive.name) &&
                ReadTrivial(ifs, primitive.layout) &&
                ReadLengthPrefixedTrivialVector32<kMaxVectorCount>(ifs, primitive.staticVertices) &&
                ReadLengthPrefixedTrivialVector32<kMaxVectorCount>(ifs, primitive.skinnedVertices) &&
                ReadLengthPrefixedTrivialVector32<kMaxVectorCount>(ifs, primitive.indices) &&
                ReadTrivial(ifs, primitive.materialIndex) &&
                ReadTrivial(ifs, primitive.bounds);
        }

        bool WriteMesh(std::ofstream& ofs, const MeshAsset& mesh) {
            if (mesh.primitives.size() > kMaxVectorCount) {
                return false;
            }
            if (!WriteLengthPrefixedString32<kMaxStringBytes>(ofs, mesh.name) || !WriteTrivial(ofs, mesh.bounds)) {
                return false;
            }
            const uint32_t primitiveCount = static_cast<uint32_t>(mesh.primitives.size());
            if (!WriteTrivial(ofs, primitiveCount)) {
                return false;
            }
            for (const MeshPrimitive& primitive : mesh.primitives) {
                if (!WritePrimitive(ofs, primitive)) {
                    return false;
                }
            }
            return true;
        }

        bool ReadMesh(std::ifstream& ifs, MeshAsset& mesh) {
            uint32_t primitiveCount = 0;
            if (!ReadLengthPrefixedString32<kMaxStringBytes>(ifs, mesh.name) ||
                !ReadTrivial(ifs, mesh.bounds) ||
                !ReadTrivial(ifs, primitiveCount) ||
                primitiveCount > kMaxVectorCount) {
                return false;
            }
            mesh.primitives.resize(primitiveCount);
            for (MeshPrimitive& primitive : mesh.primitives) {
                if (!ReadPrimitive(ifs, primitive)) {
                    return false;
                }
            }
            return true;
        }

        bool WriteNode(std::ofstream& ofs, const ModelNode& node) {
            return WriteLengthPrefixedString32<kMaxStringBytes>(ofs, node.name) &&
                WriteTrivial(ofs, node.parent) &&
                WriteLengthPrefixedTrivialVector32<kMaxVectorCount>(ofs, node.children) &&
                WriteTrivial(ofs, node.localTransform.position) &&
                WriteTrivial(ofs, node.localTransform.rotation) &&
                WriteTrivial(ofs, node.localTransform.scale) &&
                WriteBoolean8(ofs, node.hasLocalMatrix) &&
                WriteTrivial(ofs, node.localMatrix) &&
                WriteTrivial(ofs, node.globalBindMatrix) &&
                WriteTrivial(ofs, node.meshIndex) &&
                WriteTrivial(ofs, node.skinIndex);
        }

        bool ReadNode(std::ifstream& ifs, ModelNode& node) {
            return ReadLengthPrefixedString32<kMaxStringBytes>(ifs, node.name) &&
                ReadTrivial(ifs, node.parent) &&
                ReadLengthPrefixedTrivialVector32<kMaxVectorCount>(ifs, node.children) &&
                ReadTrivial(ifs, node.localTransform.position) &&
                ReadTrivial(ifs, node.localTransform.rotation) &&
                ReadTrivial(ifs, node.localTransform.scale) &&
                ReadBoolean8(ifs, node.hasLocalMatrix) &&
                ReadTrivial(ifs, node.localMatrix) &&
                ReadTrivial(ifs, node.globalBindMatrix) &&
                ReadTrivial(ifs, node.meshIndex) &&
                ReadTrivial(ifs, node.skinIndex);
        }

        bool WriteSkin(std::ofstream& ofs, const SkeletonAsset& skin) {
            if (skin.joints.size() > kMaxVectorCount) {
                return false;
            }
            if (!WriteLengthPrefixedString32<kMaxStringBytes>(ofs, skin.name) || !WriteTrivial(ofs, skin.skeletonRootNode)) {
                return false;
            }
            const uint32_t jointCount = static_cast<uint32_t>(skin.joints.size());
            if (!WriteTrivial(ofs, jointCount)) {
                return false;
            }
            for (const SkeletonJoint& joint : skin.joints) {
                if (!WriteLengthPrefixedString32<kMaxStringBytes>(ofs, joint.name) ||
                    !WriteTrivial(ofs, joint.nodeIndex) ||
                    !WriteTrivial(ofs, joint.parentJoint) ||
                    !WriteTrivial(ofs, joint.inverseBindMatrix)) {
                    return false;
                }
            }
            return true;
        }

        bool ReadSkin(std::ifstream& ifs, SkeletonAsset& skin) {
            uint32_t jointCount = 0;
            if (!ReadLengthPrefixedString32<kMaxStringBytes>(ifs, skin.name) ||
                !ReadTrivial(ifs, skin.skeletonRootNode) ||
                !ReadTrivial(ifs, jointCount) ||
                jointCount > kMaxVectorCount) {
                return false;
            }
            skin.joints.resize(jointCount);
            for (SkeletonJoint& joint : skin.joints) {
                if (!ReadLengthPrefixedString32<kMaxStringBytes>(ifs, joint.name) ||
                    !ReadTrivial(ifs, joint.nodeIndex) ||
                    !ReadTrivial(ifs, joint.parentJoint) ||
                    !ReadTrivial(ifs, joint.inverseBindMatrix)) {
                    return false;
                }
            }
            return true;
        }

        bool WriteAnimation(std::ofstream& ofs, const AnimationClip& clip) {
            if (clip.channels.size() > kMaxVectorCount) {
                return false;
            }
            if (!WriteLengthPrefixedString32<kMaxStringBytes>(ofs, clip.name) || !WriteTrivial(ofs, clip.durationSec)) {
                return false;
            }
            const uint32_t channelCount = static_cast<uint32_t>(clip.channels.size());
            if (!WriteTrivial(ofs, channelCount)) {
                return false;
            }
            for (const NodeAnimationChannel& channel : clip.channels) {
                if (!WriteTrivial(ofs, channel.targetNode) ||
                    !WriteTrivial(ofs, channel.path) ||
                    !WriteTrivial(ofs, channel.interpolation) ||
                    !WriteAnimationKeyframes(
                        ofs,
                        channel.vec3Keys) ||
                    !WriteAnimationKeyframes(
                        ofs,
                        channel.quatKeys)) {
                    return false;
                }
            }
            return true;
        }

        bool ReadAnimation(
            std::ifstream& ifs,
            AnimationClip& clip,
            uint32_t version) {
            uint32_t channelCount = 0;
            if (!ReadLengthPrefixedString32<kMaxStringBytes>(ifs, clip.name) ||
                !ReadTrivial(ifs, clip.durationSec) ||
                !ReadTrivial(ifs, channelCount) ||
                channelCount > kMaxVectorCount) {
                return false;
            }
            clip.channels.resize(channelCount);
            for (NodeAnimationChannel& channel : clip.channels) {
                if (!ReadTrivial(ifs, channel.targetNode) ||
                    !ReadTrivial(ifs, channel.path) ||
                    !ReadTrivial(ifs, channel.interpolation) ||
                    !ReadAnimationKeyframes(
                        ifs,
                        version,
                        channel.vec3Keys) ||
                    !ReadAnimationKeyframes(
                        ifs,
                        version,
                        channel.quatKeys)) {
                    return false;
                }
            }
            return true;
        }

        template<class T, class WriteFn>
        bool WriteObjectVector(std::ofstream& ofs, const std::vector<T>& values, WriteFn writeFn) {
            if (values.size() > kMaxVectorCount) {
                return false;
            }
            const uint32_t count = static_cast<uint32_t>(values.size());
            if (!WriteTrivial(ofs, count)) {
                return false;
            }
            for (const T& value : values) {
                if (!writeFn(ofs, value)) {
                    return false;
                }
            }
            return true;
        }

        template<class T, class ReadFn>
        bool ReadObjectVector(std::ifstream& ifs, std::vector<T>& values, ReadFn readFn) {
            uint32_t count = 0;
            if (!ReadTrivial(ifs, count) || count > kMaxVectorCount) {
                return false;
            }
            values.resize(count);
            for (T& value : values) {
                if (!readFn(ifs, value)) {
                    return false;
                }
            }
            return true;
        }
    }

    bool WriteHmodelFile(
        const std::filesystem::path& path,
        const ModelAsset& model,
        std::string& outMessage) {

        if (model.meshes.empty()) {
            outMessage = "[HMODEL] invalid model data";
            return false;
        }

        std::error_code ec{};
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            outMessage = "[HMODEL] failed to create output directory: " + ec.message();
            return false;
        }

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open()) {
            outMessage = "[HMODEL] failed to open for write: " + path.generic_string();
            return false;
        }

        const HmodelFileHeader header{};
        if (!WriteTrivial(ofs, header) ||
            !WriteLengthPrefixedString32<kMaxStringBytes>(ofs, model.id.value) ||
            !WriteLengthPrefixedString32<kMaxStringBytes>(ofs, model.sourcePath) ||
            !WriteTrivial(ofs, model.defaultSceneRootNode) ||
            !WriteTrivial(ofs, model.bounds)) {
            outMessage = "[HMODEL] failed while writing header payload: " + path.generic_string();
            return false;
        }

        // CPU 側のモデル構造をそのまま保存し、実行時に GPU リソースへ変換する。
        const bool ok =
            WriteObjectVector(ofs, model.nodes, WriteNode) &&
            WriteObjectVector(ofs, model.meshes, WriteMesh) &&
            WriteObjectVector(ofs, model.materials, WriteMaterial) &&
            WriteObjectVector(ofs, model.textures, [](std::ofstream& stream, const TextureAsset3D& texture) {
                return WriteLengthPrefixedString32<kMaxStringBytes>(stream, texture.name) && WriteLengthPrefixedString32<kMaxStringBytes>(stream, texture.sourcePath);
            }) &&
            WriteObjectVector(ofs, model.skins, WriteSkin) &&
            WriteObjectVector(ofs, model.animations, WriteAnimation);

        if (!ok || !ofs.good()) {
            outMessage = "[HMODEL] failed while writing: " + path.generic_string();
            return false;
        }

        outMessage = "[HMODEL] wrote " + path.generic_string();
        return true;
    }

    bool ReadHmodelFile(
        const std::filesystem::path& path,
        ModelAsset& outModel,
        std::string& outMessage) {

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            outMessage = "[HMODEL] failed to open: " + path.generic_string();
            return false;
        }

        HmodelFileHeader header{};
        if (!ReadTrivial(ifs, header) ||
            header.magic != kHmodelMagic ||
            header.version < 1u ||
            header.version > kHmodelVersion ||
            header.headerSize != sizeof(HmodelFileHeader)) {
            outMessage = "[HMODEL] invalid or unsupported file: " + path.generic_string();
            return false;
        }

        ModelAsset model{};
        if (!ReadLengthPrefixedString32<kMaxStringBytes>(ifs, model.id.value) ||
            !ReadLengthPrefixedString32<kMaxStringBytes>(ifs, model.sourcePath) ||
            !ReadTrivial(ifs, model.defaultSceneRootNode) ||
            !ReadTrivial(ifs, model.bounds)) {
            outMessage = "[HMODEL] failed to read header payload: " + path.generic_string();
            return false;
        }

        const bool ok =
            ReadObjectVector(ifs, model.nodes, ReadNode) &&
            ReadObjectVector(ifs, model.meshes, ReadMesh) &&
            ReadObjectVector(ifs, model.materials, [version = header.version](std::ifstream& stream, MaterialAsset& material) {
                return ReadMaterial(stream, material, version);
            }) &&
            ReadObjectVector(ifs, model.textures, [](std::ifstream& stream, TextureAsset3D& texture) {
                return ReadLengthPrefixedString32<kMaxStringBytes>(stream, texture.name) && ReadLengthPrefixedString32<kMaxStringBytes>(stream, texture.sourcePath);
            }) &&
            ReadObjectVector(ifs, model.skins, ReadSkin) &&
            ReadObjectVector(
                ifs,
                model.animations,
                [version = header.version](
                    std::ifstream& stream,
                    AnimationClip& animation) {
                    return ReadAnimation(stream, animation, version);
                });

        if (!ok || !ifs.good()) {
            outMessage = "[HMODEL] failed while reading: " + path.generic_string();
            return false;
        }

        outModel = std::move(model);
        outMessage = "[HMODEL] read " + path.generic_string();
        return true;
    }

} // namespace HIKARI
