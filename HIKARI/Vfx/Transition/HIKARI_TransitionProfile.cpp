#include "Vfx/Transition/HIKARI_TransitionProfile.h"

#include <algorithm>
#include <filesystem>
#include <utility>

#include <json.hpp>
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Vfx/Parameters/HIKARI_VfxParameterValues.h"
#include "Vfx/Serialization/HIKARI_VfxParameterJson.h"
#undef min
#undef max

namespace HIKARI {

bool TransitionProfile::LoadById(const std::string& profileId, TransitionProfile& outProfile) {
    if (!VFX::IsStableFxProfileId(profileId)) {
        return false;
    }
    const std::filesystem::path engineProfile =
        CONTENT::GetEngineContentContext().Resolve(std::filesystem::path("Transitions") / (profileId + ".json"));
    TransitionProfile parsed{};
    if (engineProfile.empty() || !parsed.LoadFromJson(engineProfile.generic_string())) {
        return false;
    }
    outProfile = std::move(parsed);
    return true;
}

bool TransitionProfile::LoadFromJson(const std::string& path) {
    nlohmann::json root{};
    if (!SERIALIZATION::JSON::ReadJsonFile(std::filesystem::path(path), root) || !root.is_object()) {
        return false;
    }

    TransitionProfile parsed{};
    parsed.id = root.value("id", parsed.id);
    parsed.displayName = root.value("displayName", parsed.displayName);
    parsed.programId = root.value("programId", std::string{});
    parsed.entryId = root.value("entryId", std::string("Main"));
    if (!VFX::IsStableFxProfileId(parsed.id) || parsed.programId.empty() || parsed.entryId.empty()) {
        return false;
    }
    parsed.outDuration = root.value("outDuration", parsed.outDuration);
    parsed.inDuration = root.value("inDuration", parsed.inDuration);
    parsed.waitForCameraReady = root.value("waitForCameraReady", parsed.waitForCameraReady);
    parsed.cameraReadyTimeout = std::clamp(root.value("cameraReadyTimeout", parsed.cameraReadyTimeout), 0.0f, 5.0f);

    std::string parameterError;
    if (!VFX::SERIALIZATION::ReadParameterDescriptorsFromJson(root.value("params", nlohmann::json::array()),
                                                              parsed.values.size(), parsed.params, parameterError)) {
        return false;
    }

    parsed.ResetValuesFromDefaults();
    *this = std::move(parsed);
    return true;
}

void TransitionProfile::ResetValuesFromDefaults() {
    VFX::ResetParameterValuesFromDefaults(params, values.data(), values.size());
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
