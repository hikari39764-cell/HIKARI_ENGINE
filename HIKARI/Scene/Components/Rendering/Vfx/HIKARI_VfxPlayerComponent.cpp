#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponent.h"

#include <algorithm>
#include <exception>
#include <unordered_set>
#include <utility>

#include "Authoring/HIKARI_FieldInspector.h"

namespace HIKARI {

void VfxPlayerComponent::OnAttach() { pendingCommands_.clear(); }

void VfxPlayerComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kVfxPlayerFields);
}

void VfxPlayerComponent::Deserialize(const nlohmann::json& in) {
    std::string ignoredDiagnostic{};
    (void)TryDeserialize(in, ignoredDiagnostic);
}

// 厳密に読む。壊れた文書は拒み（診断は項目のパスから始まる）、この組件は変えない。
bool VfxPlayerComponent::TryDeserialize(const nlohmann::json& in, std::string& outDiagnostic) {
    VfxPlayerComponent decoded{};
    try {
        JSONFIELDS::Read(in, decoded.settings_, kVfxPlayerFields);
    } catch (const std::exception& error) {
        outDiagnostic = std::string("VfxPlayerComponent: ") + error.what();
        return false;
    }
    decoded.NormalizeSlots();
    settings_ = std::move(decoded.settings_);
    nextSlotId_ = decoded.nextSlotId_;
    pendingCommands_.clear();
    outDiagnostic.clear();
    return true;
}

// 追加した要素の ID と既定名はここで付ける。
void VfxPlayerComponent::BuildInspector(IInspectorBuilder& builder) {
    if (InspectFields(builder, settings_, kVfxPlayerFields)) {
        NormalizeSlots();
    }
}

bool VfxPlayerComponent::IsEnabled() const noexcept { return settings_.enabled; }

void VfxPlayerComponent::SetEnabled(bool enabled) noexcept { settings_.enabled = enabled; }

const std::vector<VfxPlayerSlot>& VfxPlayerComponent::GetSlots() const noexcept { return settings_.slots; }

VfxPlayerSlot* VfxPlayerComponent::FindSlot(VfxSlotId id) noexcept {
    const auto found = std::find_if(settings_.slots.begin(), settings_.slots.end(),
                                    [id](const VfxPlayerSlot& slot) { return slot.id == id; });
    return found == settings_.slots.end() ? nullptr : &*found;
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
    settings_.slots.push_back(std::move(slot));
    return settings_.slots.back();
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
    const size_t previousSize = settings_.slots.size();
    std::erase_if(settings_.slots, [id](const VfxPlayerSlot& slot) { return slot.id == id; });
    if (previousSize == settings_.slots.size()) {
        return false;
    }
    std::erase_if(pendingCommands_, [id](const VfxSlotCommand& command) { return command.slotId == id; });
    return true;
}

bool VfxPlayerComponent::MoveSlot(VfxSlotId id, size_t targetIndex) {
    const auto found = std::find_if(settings_.slots.begin(), settings_.slots.end(),
                                    [id](const VfxPlayerSlot& slot) { return slot.id == id; });
    if (found == settings_.slots.end()) {
        return false;
    }
    const size_t sourceIndex = static_cast<size_t>(found - settings_.slots.begin());
    targetIndex = (std::min)(targetIndex, settings_.slots.size() - 1u);
    if (sourceIndex == targetIndex) {
        return true;
    }
    VfxPlayerSlot slot = std::move(settings_.slots[sourceIndex]);
    settings_.slots.erase(settings_.slots.begin() + sourceIndex);
    settings_.slots.insert(settings_.slots.begin() + targetIndex, std::move(slot));
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
    for (VfxPlayerSlot& slot : settings_.slots) {
        if (!slot.id.IsValid() || !ids.emplace(slot.id.value).second) {
            slot.id = {};
        } else {
            maximumId = (std::max)(maximumId, slot.id.value);
        }
    }
    nextSlotId_ = maximumId + 1u;
    for (VfxPlayerSlot& slot : settings_.slots) {
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
