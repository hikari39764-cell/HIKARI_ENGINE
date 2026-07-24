#include "Scene/Sequencer/HIKARI_SequenceBinding.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

namespace HIKARI::SEQUENCER {

    void NormalizeSequenceBindings(SequenceBindingTable& bindings) {
        std::unordered_set<uint64_t> usedIds{};
        SequenceBindingId nextId = AllocateSequenceBindingId(bindings);
        for (SequenceBinding& binding : bindings) {
            if (!binding.id.IsValid() ||
                !usedIds.insert(binding.id.value).second) {
                while (!usedIds.insert(nextId.value).second) {
                    ++nextId.value;
                    if (nextId.value == 0) {
                        nextId.value = 1;
                    }
                }
                binding.id = nextId;
                ++nextId.value;
                if (nextId.value == 0) {
                    nextId.value = 1;
                }
            }
            if (binding.name.empty()) {
                binding.name = binding.targetKind ==
                        SequenceBindingTargetKind::Slot &&
                        !binding.slotName.empty()
                    ? binding.slotName
                    : "Binding " + std::to_string(binding.id.value);
            }
            if (binding.targetKind == SequenceBindingTargetKind::Slot &&
                binding.slotName.empty()) {
                binding.slotName = binding.name;
            }
        }
    }

    SequenceBindingId AllocateSequenceBindingId(
        const SequenceBindingTable& bindings) {

        SequenceBindingId nextId{ 1 };
        for (const SequenceBinding& binding : bindings) {
            if (binding.id.value < nextId.value) {
                continue;
            }
            if (binding.id.value ==
                (std::numeric_limits<uint64_t>::max)()) {
                nextId.value = 1;
                break;
            }
            nextId.value = binding.id.value + 1;
        }
        while (FindSequenceBinding(bindings, nextId) != nullptr) {
            ++nextId.value;
            if (nextId.value == 0) {
                nextId.value = 1;
            }
        }
        return nextId;
    }

    SequenceBindingId FindOrCreateSceneObjectBinding(
        SequenceBindingTable& bindings,
        SceneObjectId sceneObjectId,
        const std::string& name) {

        if (sceneObjectId.value == 0) {
            return {};
        }
        const auto found = std::find_if(
            bindings.begin(),
            bindings.end(),
            [sceneObjectId](const SequenceBinding& binding) {
                return binding.targetKind ==
                        SequenceBindingTargetKind::SceneObject &&
                    binding.sceneObjectId == sceneObjectId;
            });
        if (found != bindings.end()) {
            return found->id;
        }

        SequenceBinding binding{};
        binding.id = AllocateSequenceBindingId(bindings);
        binding.name = name;
        binding.sceneObjectId = sceneObjectId;
        bindings.push_back(binding);
        NormalizeSequenceBindings(bindings);
        return binding.id;
    }

    SequenceBindingId FindOrCreateSlotBinding(
        SequenceBindingTable& bindings,
        const std::string& slotName,
        const std::string& displayName) {

        if (slotName.empty()) {
            return {};
        }
        const auto found = std::find_if(
            bindings.begin(),
            bindings.end(),
            [&slotName](const SequenceBinding& binding) {
                return binding.targetKind ==
                        SequenceBindingTargetKind::Slot &&
                    binding.slotName == slotName;
            });
        if (found != bindings.end()) {
            return found->id;
        }

        SequenceBinding binding{};
        binding.id = AllocateSequenceBindingId(bindings);
        binding.name = displayName.empty() ? slotName : displayName;
        binding.targetKind = SequenceBindingTargetKind::Slot;
        binding.slotName = slotName;
        bindings.push_back(std::move(binding));
        NormalizeSequenceBindings(bindings);
        return binding.id;
    }

    SequenceBinding* FindSequenceBinding(
        SequenceBindingTable& bindings,
        SequenceBindingId bindingId) noexcept {

        const auto found = std::find_if(
            bindings.begin(),
            bindings.end(),
            [bindingId](const SequenceBinding& binding) {
                return binding.id == bindingId;
            });
        return found != bindings.end() ? &*found : nullptr;
    }

    const SequenceBinding* FindSequenceBinding(
        const SequenceBindingTable& bindings,
        SequenceBindingId bindingId) noexcept {

        const auto found = std::find_if(
            bindings.begin(),
            bindings.end(),
            [bindingId](const SequenceBinding& binding) {
                return binding.id == bindingId;
            });
        return found != bindings.end() ? &*found : nullptr;
    }

    bool ResolveSceneObjectBinding(
        const SequenceBindingTable& bindings,
        SequenceBindingId bindingId,
        SceneObjectId& outSceneObjectId) noexcept {

        const SequenceBinding* binding =
            FindSequenceBinding(bindings, bindingId);
        if (binding == nullptr ||
            binding->targetKind != SequenceBindingTargetKind::SceneObject ||
            binding->sceneObjectId.value == 0) {
            outSceneObjectId = {};
            return false;
        }
        outSceneObjectId = binding->sceneObjectId;
        return true;
    }

    bool ResolveSceneObjectBinding(
        const SequenceBindingTable& bindings,
        SequenceBindingId bindingId,
        const SequenceBindingContext& context,
        SceneObjectId& outSceneObjectId) noexcept {

        return context.Resolve(bindings, bindingId, outSceneObjectId);
    }

    bool SequenceBindingContext::Bind(
        SequenceBindingId bindingId,
        SceneObjectId sceneObjectId) {

        if (!bindingId.IsValid() || sceneObjectId.value == 0) {
            return false;
        }
        const auto found = std::find_if(
            overrides_.begin(),
            overrides_.end(),
            [bindingId](const SequenceBindingOverride& overrideValue) {
                return overrideValue.bindingId == bindingId;
            });
        if (found != overrides_.end()) {
            found->sceneObjectId = sceneObjectId;
            return true;
        }
        overrides_.push_back({ bindingId, {}, sceneObjectId });
        return true;
    }

    bool SequenceBindingContext::BindSlot(
        std::string slotName,
        SceneObjectId sceneObjectId) {

        if (slotName.empty() || sceneObjectId.value == 0) {
            return false;
        }
        const auto found = std::find_if(
            overrides_.begin(),
            overrides_.end(),
            [&slotName](const SequenceBindingOverride& overrideValue) {
                return overrideValue.slotName == slotName;
            });
        if (found != overrides_.end()) {
            found->sceneObjectId = sceneObjectId;
            return true;
        }
        overrides_.push_back({ {}, std::move(slotName), sceneObjectId });
        return true;
    }

    bool SequenceBindingContext::Unbind(
        SequenceBindingId bindingId) noexcept {

        const size_t oldSize = overrides_.size();
        overrides_.erase(
            std::remove_if(
                overrides_.begin(),
                overrides_.end(),
                [bindingId](const SequenceBindingOverride& overrideValue) {
                    return overrideValue.bindingId == bindingId;
                }),
            overrides_.end());
        return overrides_.size() != oldSize;
    }

    bool SequenceBindingContext::UnbindSlot(
        std::string_view slotName) noexcept {

        const size_t oldSize = overrides_.size();
        overrides_.erase(
            std::remove_if(
                overrides_.begin(),
                overrides_.end(),
                [slotName](const SequenceBindingOverride& overrideValue) {
                    return overrideValue.slotName == slotName;
                }),
            overrides_.end());
        return overrides_.size() != oldSize;
    }

    bool SequenceBindingContext::Resolve(
        const SequenceBindingTable& bindings,
        SequenceBindingId bindingId,
        SceneObjectId& outSceneObjectId) const noexcept {

        outSceneObjectId = {};
        if (!bindingId.IsValid()) {
            return false;
        }
        const auto directOverride = std::find_if(
            overrides_.begin(),
            overrides_.end(),
            [bindingId](const SequenceBindingOverride& overrideValue) {
                return overrideValue.bindingId == bindingId &&
                    overrideValue.sceneObjectId.value != 0;
            });
        if (directOverride != overrides_.end()) {
            outSceneObjectId = directOverride->sceneObjectId;
            return true;
        }

        const SequenceBinding* binding =
            FindSequenceBinding(bindings, bindingId);
        if (binding == nullptr) {
            return false;
        }
        if (binding->targetKind == SequenceBindingTargetKind::Slot) {
            const auto slotOverride = std::find_if(
                overrides_.begin(),
                overrides_.end(),
                [binding](const SequenceBindingOverride& overrideValue) {
                    return overrideValue.slotName == binding->slotName &&
                        overrideValue.sceneObjectId.value != 0;
                });
            if (slotOverride == overrides_.end()) {
                return false;
            }
            outSceneObjectId = slotOverride->sceneObjectId;
            return true;
        }
        if (binding->sceneObjectId.value == 0) {
            return false;
        }
        outSceneObjectId = binding->sceneObjectId;
        return true;
    }

    bool SequenceBindingContext::Empty() const noexcept {
        return overrides_.empty();
    }

    void SequenceBindingContext::Clear() noexcept {
        overrides_.clear();
    }

    const std::vector<SequenceBindingOverride>&
        SequenceBindingContext::GetOverrides() const noexcept {

        return overrides_;
    }

} // namespace HIKARI::SEQUENCER
