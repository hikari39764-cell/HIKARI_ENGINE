#include "HIKARI_ComponentRegistry.h"

namespace HIKARI {

bool ComponentRegistry::Register(ComponentTypeInfo info) {
    if (info.typeName.empty() || !info.factory) {
        return false;
    }
    const ComponentTypeId computedTypeId = MakeComponentTypeId(info.typeName);
    if (!computedTypeId.IsValid() || (info.typeId.IsValid() && info.typeId != computedTypeId)) {
        return false;
    }
    info.typeId = computedTypeId;
    if (byId_.find(info.typeId.value) != byId_.end()) {
        return false;
    }
    if (info.presentation.displayName.empty()) {
        info.presentation.displayName = info.typeName;
    }
    const std::string typeName = info.typeName;
    try {
        const auto inserted = byName_.emplace(typeName, std::move(info));
        if (!inserted.second) {
            return false;
        }
        try {
            const auto idInserted = byId_.emplace(inserted.first->second.typeId.value, inserted.first->first);
            if (!idInserted.second) {
                byName_.erase(inserted.first);
                return false;
            }
        } catch (...) {
            byName_.erase(inserted.first);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

void ComponentRegistry::Clear() noexcept {
    byId_.clear();
    byName_.clear();
}

const ComponentTypeInfo* ComponentRegistry::Find(std::string_view typeName) const {
    const auto it = byName_.find(std::string(typeName));
    if (it == byName_.end()) {
        return nullptr;
    }
    return &it->second;
}

const ComponentTypeInfo* ComponentRegistry::Find(ComponentTypeId typeId) const noexcept {
    if (!typeId.IsValid()) {
        return nullptr;
    }
    const auto found = byId_.find(typeId.value);
    if (found == byId_.end()) {
        return nullptr;
    }
    const auto type = byName_.find(found->second);
    return type != byName_.end() ? &type->second : nullptr;
}

std::vector<std::string> ComponentRegistry::GetTypeNames() const {
    std::vector<std::string> names;
    names.reserve(byName_.size());
    for (const auto& [name, _] : byName_) {
        names.push_back(name);
    }
    return names;
}

std::vector<const ComponentTypeInfo*> ComponentRegistry::GetTypeInfos() const {
    std::vector<const ComponentTypeInfo*> infos;
    infos.reserve(byName_.size());
    for (const auto& [_, info] : byName_) {
        infos.push_back(&info);
    }
    return infos;
}

} // namespace HIKARI
