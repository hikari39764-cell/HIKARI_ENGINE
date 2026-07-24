#include "Vfx/Post/HIKARI_PostProfile.h"

#include <algorithm>
#include <iterator>

#include <json.hpp>
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Vfx/Parameters/HIKARI_VfxParameterValues.h"
#include "Vfx/Serialization/HIKARI_VfxParameterJson.h"
#include "Vfx/Serialization/HIKARI_VfxProfileJson.h"

namespace HIKARI {

    bool PostProfile::LoadById(const std::string& profileId, PostProfile& out) {
        if (profileId.empty()) {
            return false;
        }
        const std::string path = std::string("Data/post_profiles/") + profileId + ".json";
        return out.LoadFromJson(path);
    }

    bool PostProfile::LoadFromJson(const std::string& path) {
        nlohmann::json root{};
        if (!SERIALIZATION::JSON::ReadJsonFile(
                std::filesystem::path(path),
                root) ||
            !root.is_object()) {
            return false;
        }

        id = root.value("id", id);
        displayName = root.value("displayName", displayName);
        domain = VFX::SERIALIZATION::ParseFxDomain(
            root.value("domain", nlohmann::json{}),
            domain);

        passes.clear();
        if (root.contains("passes") && root["passes"].is_array()) {
            for (const auto& passNode : root["passes"]) {
                if (!passNode.is_object()) {
                    continue;
                }
                VFX::PassDescriptor pass{};
                pass.passName = passNode.value("passName", pass.passName);
                pass.shaderId = passNode.value("shaderId", pass.shaderId);
                pass.composite = VFX::SERIALIZATION::ParseCompositeMode(
                    passNode.value("composite", nlohmann::json{}),
                    pass.composite);
                pass.depthTest = passNode.value("depthTest", pass.depthTest);
                pass.depthWrite = passNode.value("depthWrite", pass.depthWrite);
                pass.doubleSided = passNode.value("doubleSided", pass.doubleSided);
                passes.push_back(std::move(pass));
            }
        }

        VFX::SERIALIZATION::ReadParameterDescriptorsFromJson(
            root.value("params", nlohmann::json{}),
            params);

        ResetValuesFromDefaults();
        return true;
    }

    void PostProfile::ResetValuesFromDefaults() {
        VFX::ResetParameterValuesFromDefaults(
            params,
            values.data(),
            values.size());
    }

    void PostProfile::ApplyToCommonParams(POST::CommonParams& out) const {
        const size_t count = std::min(values.size(), std::size(out.user));
        for (size_t i = 0; i < count; ++i) {
            out.user[i] = values[i];
        }
    }

    void PostProfile::CopyValuesTo(DirectX::XMFLOAT4(&dst)[16]) const {
        const size_t count = std::min(values.size(), std::size(dst));
        for (size_t i = 0; i < count; ++i) {
            dst[i] = values[i];
        }
    }

} // namespace HIKARI
