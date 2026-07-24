#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI::SEQUENCER {

    struct SequenceBindingId {
        uint64_t value = 0;

        bool IsValid() const noexcept {
            return value != 0;
        }

        bool operator==(const SequenceBindingId& rhs) const noexcept {
            return value == rhs.value;
        }
    };

    enum class SequenceBindingTargetKind : uint8_t {
        SceneObject,
        Slot,
    };

    struct SequenceBinding {
        SequenceBindingId id{};
        std::string name{};
        SequenceBindingTargetKind targetKind =
            SequenceBindingTargetKind::SceneObject;
        SceneObjectId sceneObjectId{};
        std::string slotName{};
        bool required = true;
    };

    struct SequenceBindingOverride {
        SequenceBindingId bindingId{};
        std::string slotName{};
        SceneObjectId sceneObjectId{};
    };

    using SequenceBindingTable = std::vector<SequenceBinding>;

    class SequenceBindingContext {
    public:
        bool Bind(
            SequenceBindingId bindingId,
            SceneObjectId sceneObjectId);
        bool BindSlot(
            std::string slotName,
            SceneObjectId sceneObjectId);
        bool Unbind(SequenceBindingId bindingId) noexcept;
        bool UnbindSlot(std::string_view slotName) noexcept;
        bool Resolve(
            const SequenceBindingTable& bindings,
            SequenceBindingId bindingId,
            SceneObjectId& outSceneObjectId) const noexcept;
        bool Empty() const noexcept;
        void Clear() noexcept;
        const std::vector<SequenceBindingOverride>& GetOverrides()
            const noexcept;

    private:
        std::vector<SequenceBindingOverride> overrides_{};
    };

    void NormalizeSequenceBindings(SequenceBindingTable& bindings);
    SequenceBindingId AllocateSequenceBindingId(
        const SequenceBindingTable& bindings);
    SequenceBindingId FindOrCreateSceneObjectBinding(
        SequenceBindingTable& bindings,
        SceneObjectId sceneObjectId,
        const std::string& name = {});
    SequenceBindingId FindOrCreateSlotBinding(
        SequenceBindingTable& bindings,
        const std::string& slotName,
        const std::string& displayName = {});
    SequenceBinding* FindSequenceBinding(
        SequenceBindingTable& bindings,
        SequenceBindingId bindingId) noexcept;
    const SequenceBinding* FindSequenceBinding(
        const SequenceBindingTable& bindings,
        SequenceBindingId bindingId) noexcept;
    bool ResolveSceneObjectBinding(
        const SequenceBindingTable& bindings,
        SequenceBindingId bindingId,
        SceneObjectId& outSceneObjectId) noexcept;
    bool ResolveSceneObjectBinding(
        const SequenceBindingTable& bindings,
        SequenceBindingId bindingId,
        const SequenceBindingContext& context,
        SceneObjectId& outSceneObjectId) noexcept;

} // namespace HIKARI::SEQUENCER
