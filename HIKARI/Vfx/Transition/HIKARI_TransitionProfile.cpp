#include "Vfx/Transition/HIKARI_TransitionProfile.h"

#include <algorithm>

#include <json.hpp>
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Vfx/Parameters/HIKARI_VfxParameterValues.h"
#include "Vfx/Serialization/HIKARI_VfxParameterJson.h"
#undef min
#undef max

namespace HIKARI {

bool TransitionProfile::LoadById(const std::string& profileId, TransitionProfile& outProfile) {
    if (profileId.empty()) {
        return false;
    }
    return outProfile.LoadFromJson(std::string("Data/transition_profiles/") + profileId + ".json");
}

bool TransitionProfile::LoadFromJson(const std::string& path) {
    nlohmann::json root{};
    if (!SERIALIZATION::JSON::ReadJsonFile(
            std::filesystem::path(path),
            root) ||
        !root.is_object()) {
        return false;
    }

    id = root.value("id", id);
    displayName = root.value("displayName", displayName);
    shaderId = root.value("shaderId", shaderId);
    outDuration = root.value("outDuration", outDuration);
    inDuration = root.value("inDuration", inDuration);

    VFX::SERIALIZATION::ReadParameterDescriptorsFromJson(
        root.value("params", nlohmann::json{}),
        params);

    ResetValuesFromDefaults();
    return true;
}

void TransitionProfile::ResetValuesFromDefaults() {
    VFX::ResetParameterValuesFromDefaults(
        params,
        values.data(),
        values.size());
}

void TransitionProfile::ApplyToCommonParams(POST::CommonParams& out) const {
    for (DirectX::XMFLOAT4& value : out.user) {
        value = DirectX::XMFLOAT4{};
    }

    const size_t count = (std::min)(values.size(), static_cast<size_t>(14));
    for (size_t i = 0; i < count; ++i) {
        out.user[i] = values[i];
    }
}

} // namespace HIKARI
