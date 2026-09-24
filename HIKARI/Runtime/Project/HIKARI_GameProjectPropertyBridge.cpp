#include "HIKARI/Project/PropertyArchive.h"

#include <algorithm>
#include <cstring>

#include <json.hpp>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectFixedText.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI {
using namespace PROJECT::DETAIL;
namespace {

const nlohmann::json* FindProperty(const void* properties, std::string_view name) noexcept {
    const auto* object = static_cast<const nlohmann::json*>(properties);
    if (object == nullptr || !object->is_object()) {
        return nullptr;
    }
    const auto found = object->find(std::string(name));
    return found != object->end() ? &*found : nullptr;
}

nlohmann::json* GetPropertyObject(void* properties) {
    auto* object = static_cast<nlohmann::json*>(properties);
    if (object != nullptr && !object->is_object()) {
        *object = nlohmann::json::object();
    }
    return object;
}

template <size_t Capacity>
bool CopyFixedString(const nlohmann::json* value, char (&destination)[Capacity]) noexcept {
    std::memset(destination, 0, Capacity);
    if (value == nullptr || !value->is_string()) {
        return false;
    }
    const std::string& source = value->get_ref<const std::string&>();
    if (source.size() >= Capacity) {
        return false;
    }
    const size_t count = source.size();
    std::memcpy(destination, source.data(), count);
    destination[count] = '\0';
    return true;
}

const nlohmann::json* FindObjectMember(const nlohmann::json& object, std::string_view primary,
                                       std::string_view legacy) noexcept {
    const auto found = object.find(std::string(primary));
    if (found != object.end()) {
        return &*found;
    }
    const auto legacyFound = object.find(std::string(legacy));
    return legacyFound != object.end() ? &*legacyFound : nullptr;
}

} // namespace

PropertyReader GameProjectSdkAccess::MakePropertyReader(const void* properties) noexcept {
    return PropertyReader(properties);
}

PropertyWriter GameProjectSdkAccess::MakePropertyWriter(void* properties) noexcept {
    return PropertyWriter(properties);
}

bool PropertyReader::Contains(std::string_view name) const noexcept {
    return FindProperty(properties_, name) != nullptr;
}

uint64_t PropertyReader::ReadSceneObjectId(std::string_view name, uint64_t fallback) const noexcept {
    const auto* value = FindProperty(properties_, name);
    uint64_t id = fallback;
    return value != nullptr && SCENE::SERIALIZATION::ReadSceneObjectReference(*value, id) ? id : fallback;
}

void PropertyWriter::WriteSceneObjectId(std::string_view name, uint64_t value) {
    if (auto* object = GetPropertyObject(properties_)) {
        (*object)[std::string(name)] = SCENE::SERIALIZATION::WriteSceneObjectReference(value);
    }
}

bool PropertyReader::ReadBool(std::string_view name, bool fallback) const noexcept {
    const nlohmann::json* value = FindProperty(properties_, name);
    return value != nullptr && value->is_boolean() ? value->get<bool>() : fallback;
}

int64_t PropertyReader::ReadInt(std::string_view name, int64_t fallback) const noexcept {
    const nlohmann::json* value = FindProperty(properties_, name);
    if (value == nullptr || !value->is_number_integer()) {
        return fallback;
    }
    return value->get<int64_t>();
}

float PropertyReader::ReadFloat(std::string_view name, float fallback) const noexcept {
    const nlohmann::json* value = FindProperty(properties_, name);
    return value != nullptr && value->is_number() ? value->get<float>() : fallback;
}

GameProjectFloat3 PropertyReader::ReadFloat3(std::string_view name, GameProjectFloat3 fallback) const noexcept {
    const nlohmann::json* value = FindProperty(properties_, name);
    if (value == nullptr || !value->is_array() || value->size() < 3u || !(*value)[0].is_number() ||
        !(*value)[1].is_number() || !(*value)[2].is_number()) {
        return fallback;
    }
    return {(*value)[0].get<float>(), (*value)[1].get<float>(), (*value)[2].get<float>()};
}

bool PropertyReader::ReadVfxAssetReference(std::string_view name,
                                           GameProjectVfxAssetReference& outReference) const noexcept {
    outReference = {};
    try {
        const nlohmann::json* value = FindProperty(properties_, name);
        if (value == nullptr) {
            return false;
        }
        // Older project components may have stored only the GUID string.
        if (value->is_string()) {
            return CopyFixedString(value, outReference.assetGuid);
        }
        if (!value->is_object()) {
            return false;
        }
        const nlohmann::json* guid = FindObjectMember(*value, "assetGuid", "guid");
        const nlohmann::json* path = FindObjectMember(*value, "assetPath", "sourcePath");
        const bool guidFits = CopyFixedString(guid, outReference.assetGuid);
        const bool pathFits = CopyFixedString(path, outReference.assetPath);
        if ((guid != nullptr && !guidFits) || (path != nullptr && !pathFits)) {
            outReference = {};
            return false;
        }
        return true;
    } catch (...) {
        outReference = {};
        return false;
    }
}

bool PropertyReader::ReadTextureAssetReference(std::string_view name,
                                               GameProjectUiTextureReference& outReference) const noexcept {
    outReference = {};
    try {
        const nlohmann::json* value = FindProperty(properties_, name);
        if (value == nullptr) {
            return false;
        }
        if (value->is_string()) {
            return CopyFixedString(value, outReference.assetGuid);
        }
        if (!value->is_object()) {
            return false;
        }
        const nlohmann::json* guid = FindObjectMember(*value, "assetGuid", "guid");
        const nlohmann::json* path = FindObjectMember(*value, "assetPath", "sourcePath");
        const bool guidFits = CopyFixedString(guid, outReference.assetGuid);
        const bool pathFits = CopyFixedString(path, outReference.assetPath);
        if ((guid != nullptr && !guidFits) || (path != nullptr && !pathFits)) {
            outReference = {};
            return false;
        }
        return true;
    } catch (...) {
        outReference = {};
        return false;
    }
}

size_t PropertyReader::GetStringSize(std::string_view name) const noexcept {
    const nlohmann::json* value = FindProperty(properties_, name);
    return value != nullptr && value->is_string() ? value->get_ref<const std::string&>().size() : 0u;
}

bool PropertyReader::CopyString(std::string_view name, char* destination, size_t capacity) const noexcept {
    if (destination == nullptr || capacity == 0u) {
        return false;
    }
    const nlohmann::json* value = FindProperty(properties_, name);
    if (value == nullptr || !value->is_string()) {
        destination[0] = '\0';
        return false;
    }
    const std::string& source = value->get_ref<const std::string&>();
    const size_t count = (std::min)(source.size(), capacity - 1u);
    std::memcpy(destination, source.data(), count);
    destination[count] = '\0';
    return count == source.size();
}

void PropertyWriter::WriteBool(std::string_view name, bool value) {
    if (nlohmann::json* object = GetPropertyObject(properties_)) {
        (*object)[std::string(name)] = value;
    }
}

void PropertyWriter::WriteInt(std::string_view name, int64_t value) {
    if (nlohmann::json* object = GetPropertyObject(properties_)) {
        (*object)[std::string(name)] = value;
    }
}

void PropertyWriter::WriteFloat(std::string_view name, float value) {
    if (nlohmann::json* object = GetPropertyObject(properties_)) {
        (*object)[std::string(name)] = value;
    }
}

void PropertyWriter::WriteString(std::string_view name, std::string_view value) {
    if (nlohmann::json* object = GetPropertyObject(properties_)) {
        (*object)[std::string(name)] = value;
    }
}

void PropertyWriter::WriteFloat3(std::string_view name, GameProjectFloat3 value) {
    if (nlohmann::json* object = GetPropertyObject(properties_)) {
        (*object)[std::string(name)] = {value.x, value.y, value.z};
    }
}

void PropertyWriter::WriteVfxAssetReference(std::string_view name, const GameProjectVfxAssetReference& reference) {
    const std::string guid = ReadFixedString(reference.assetGuid);
    const std::string path = ReadFixedString(reference.assetPath);
    if ((reference.assetGuid[0] != '\0' && guid.empty()) || (reference.assetPath[0] != '\0' && path.empty())) {
        return;
    }
    if (nlohmann::json* object = GetPropertyObject(properties_)) {
        (*object)[std::string(name)] = {{"assetGuid", guid}, {"assetPath", path}};
    }
}

void PropertyWriter::WriteTextureAssetReference(std::string_view name, const GameProjectUiTextureReference& reference) {
    const std::string guid = ReadFixedString(reference.assetGuid);
    const std::string path = ReadFixedString(reference.assetPath);
    if ((reference.assetGuid[0] != '\0' && guid.empty()) || (reference.assetPath[0] != '\0' && path.empty())) {
        return;
    }
    if (nlohmann::json* object = GetPropertyObject(properties_)) {
        (*object)[std::string(name)] = {{"assetGuid", guid}, {"assetPath", path}};
    }
}

} // namespace HIKARI
