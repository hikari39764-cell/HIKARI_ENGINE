#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponent.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace HIKARI {

void VfxPlayerComponent::OnAttach() { pendingCommands_.clear(); }

bool VfxPlayerComponent::IsEnabled() const noexcept { return enabled_; }

void VfxPlayerComponent::SetEnabled(bool enabled) noexcept { enabled_ = enabled; }

const std::vector<VfxPlayerSlot>& VfxPlayerComponent::GetSlots() const noexcept { return slots_; }

VfxPlayerSlot* VfxPlayerComponent::FindSlot(VfxSlotId id) noexcept {
    const auto found =
        std::find_if(slots_.begin(), slots_.end(), [id](const VfxPlayerSlot& slot) { return slot.id == id; });
    return found == slots_.end() ? nullptr : &*found;
}

const VfxPlayerSlot* VfxPlayerComponent::FindSlot(VfxSlotId id) const noexcept {
    return const_cast<VfxPlayerComponent*>(this)->FindSlot(id);
}

VfxPlayerSlot& VfxPlayerComponent::AddSlot(std::string name) {
    VfxPlayerSlot slot{};
    slot.name = std::move(name);
    return AppendSlot(std::move(slot));
}

VfxPlayerSlot& VfxPlayerComponent::AppendSlot(VfxPlayerSlot slot) {
    if (!slot.id.IsValid() || FindSlot(slot.id) != nullptr) {
        slot.id = AllocateSlotId();
    } else {
        nextSlotId_ = (std::max)(nextSlotId_, slot.id.value + 1u);
    }
    if (slot.name.empty()) {
        slot.name = "VFX Slot " + std::to_string(slot.id.value);
    }
    slots_.push_back(std::move(slot));
    return slots_.back();
}

VfxPlayerSlot* VfxPlayerComponent::DuplicateSlot(VfxSlotId id) {
    const VfxPlayerSlot* source = FindSlot(id);
    if (source == nullptr) {
        return nullptr;
    }
    VfxPlayerSlot duplicate = *source;
    duplicate.id = {};
    duplicate.name += " Copy";
    return &AppendSlot(std::move(duplicate));
}

bool VfxPlayerComponent::RemoveSlot(VfxSlotId id) {
    const size_t previousSize = slots_.size();
    std::erase_if(slots_, [id](const VfxPlayerSlot& slot) { return slot.id == id; });
    if (previousSize == slots_.size()) {
        return false;
    }
    std::erase_if(pendingCommands_, [id](const VfxSlotCommand& command) { return command.slotId == id; });
    return true;
}

bool VfxPlayerComponent::MoveSlot(VfxSlotId id, size_t targetIndex) {
    const auto found =
        std::find_if(slots_.begin(), slots_.end(), [id](const VfxPlayerSlot& slot) { return slot.id == id; });
    if (found == slots_.end()) {
        return false;
    }
    const size_t sourceIndex = static_cast<size_t>(found - slots_.begin());
    targetIndex = (std::min)(targetIndex, slots_.size() - 1u);
    if (sourceIndex == targetIndex) {
        return true;
    }
    VfxPlayerSlot slot = std::move(slots_[sourceIndex]);
    slots_.erase(slots_.begin() + sourceIndex);
    slots_.insert(slots_.begin() + targetIndex, std::move(slot));
    return true;
}

bool VfxPlayerComponent::RequestPlay(VfxSlotId id) {
    if (FindSlot(id) == nullptr) {
        return false;
    }
    pendingCommands_.push_back({VfxSlotCommandKind::Play, id});
    return true;
}

bool VfxPlayerComponent::RequestStop(VfxSlotId id) {
    if (FindSlot(id) == nullptr) {
        return false;
    }
    pendingCommands_.push_back({VfxSlotCommandKind::Stop, id});
    return true;
}

std::vector<VfxSlotCommand> VfxPlayerComponent::ConsumePendingCommands() {
    std::vector<VfxSlotCommand> commands{};
    commands.swap(pendingCommands_);
    return commands;
}

VfxSlotId VfxPlayerComponent::AllocateSlotId() noexcept {
    while (nextSlotId_ == 0u || FindSlot(VfxSlotId{nextSlotId_}) != nullptr) {
        ++nextSlotId_;
    }
    return VfxSlotId{nextSlotId_++};
}

void VfxPlayerComponent::NormalizeSlots() {
    std::unordered_set<uint64_t> ids{};
    uint64_t maximumId = 0u;
    for (VfxPlayerSlot& slot : slots_) {
        if (!slot.id.IsValid() || !ids.emplace(slot.id.value).second) {
            slot.id = {};
        } else {
            maximumId = (std::max)(maximumId, slot.id.value);
        }
    }
    nextSlotId_ = maximumId + 1u;
    for (VfxPlayerSlot& slot : slots_) {
        if (!slot.id.IsValid()) {
            slot.id = AllocateSlotId();
        }
        if (slot.name.empty()) {
            slot.name = "VFX Slot " + std::to_string(slot.id.value);
        }
        std::unordered_set<std::string> inputIds{};
        std::erase_if(slot.variableBindings, [&inputIds](const VfxVariableBinding& binding) {
            return binding.inputId.empty() || binding.variableId.empty() || !inputIds.emplace(binding.inputId).second;
        });
    }
}

} // namespace HIKARI
