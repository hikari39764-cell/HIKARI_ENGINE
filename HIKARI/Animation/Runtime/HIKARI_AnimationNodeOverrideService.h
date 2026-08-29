#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Animation/Runtime/HIKARI_AnimationPose.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {

    class ModelAsset;

namespace ANIMATION {

    class AnimationNodeOverrideService {
    public:
        bool SetLocalOffset(
            RuntimeObjectHandle object,
            std::string_view nodeName,
            const Transform3D& offset);
        bool SetParentLocalOffset(
            RuntimeObjectHandle object,
            std::string_view nodeName,
            const Transform3D& offset);
        bool ClearLocalOffset(
            RuntimeObjectHandle object,
            std::string_view nodeName) noexcept;
        void Remove(RuntimeObjectHandle object) noexcept;
        void Clear() noexcept;

        bool Apply(
            const ModelAsset& model,
            RuntimeObjectHandle object,
            AnimationLocalPose& inOutPose) const;

    private:
        enum class OffsetSpace : uint8_t {
            NodeLocal = 0u,
            ParentLocal,
        };

        struct Entry {
            std::string nodeName{};
            Transform3D offset{};
            OffsetSpace space = OffsetSpace::NodeLocal;
        };

        bool SetOffset(
            RuntimeObjectHandle object,
            std::string_view nodeName,
            const Transform3D& offset,
            OffsetSpace space);

        std::unordered_map<uint64_t, std::vector<Entry>> entries_{};
    };

} // namespace ANIMATION
} // namespace HIKARI
