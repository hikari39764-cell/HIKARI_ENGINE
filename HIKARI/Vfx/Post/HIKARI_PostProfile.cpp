#include "Vfx/Post/HIKARI_PostProfile.h"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <utility>

#include <json.hpp>
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Vfx/Parameters/HIKARI_VfxParameterValues.h"
#include "Vfx/Serialization/HIKARI_VfxParameterJson.h"
#include "Vfx/Serialization/HIKARI_VfxProfileJson.h"

namespace HIKARI {
    namespace {
        bool ReadPostProgramReference(
            const nlohmann::json& node,
            std::string& outProgramId,
            std::string& outEntryId) {
            const std::string programId =
                node.value("programId", std::string{});
            outEntryId = node.value("entryId", std::string("Main"));
            const std::string legacyShaderId =
                node.value("shaderId", std::string{});
            return VFX::TryNormalizeBuiltinShaderProgramId(
                    programId,
                    legacyShaderId,
                    outProgramId) &&
                !outEntryId.empty();
        }
    }

    bool PostProfile::LoadById(const std::string& profileId, PostProfile& out) {
        if (!VFX::IsStableFxProfileId(profileId)) {
            return false;
        }
        const std::filesystem::path engineProfile =
            CONTENT::GetEngineContentContext().Resolve(
                std::filesystem::path("PostProfiles") /
                    (profileId + ".json"));
        if (!engineProfile.empty()) {
            std::error_code error{};
            const bool exists =
                std::filesystem::exists(engineProfile, error);
            if (error) {
                return false;
            }
            if (exists) {
                PostProfile parsed{};
                if (!parsed.LoadFromJson(
                        engineProfile.generic_string())) {
                    return false;
                }
                out = std::move(parsed);
                return true;
            }
        }
        PostProfile parsed{};
        if (!parsed.LoadFromJson(
                std::string("Data/post_profiles/") +
                    profileId + ".json")) {
            return false;
        }
        out = std::move(parsed);
        return true;
    }

    bool PostProfile::LoadFromJson(const std::string& path) {
        nlohmann::json root{};
        if (!SERIALIZATION::JSON::ReadJsonFile(
                std::filesystem::path(path),
                root) ||
            !root.is_object()) {
            return false;
        }

        PostProfile parsed{};
        parsed.id = root.value("id", parsed.id);
        parsed.displayName = root.value(
            "displayName", parsed.displayName);
        parsed.domain = VFX::SERIALIZATION::ParseFxDomain(
            root.value("domain", nlohmann::json{}),
            parsed.domain);
        if (!VFX::IsStableFxProfileId(parsed.id)) {
            return false;
        }

        if (root.contains("passes") && root["passes"].is_array()) {
            for (const auto& passNode : root["passes"]) {
                if (!passNode.is_object()) {
                    return false;
                }
                VFX::PassDescriptor pass{};
                pass.passName = passNode.value("passName", pass.passName);
                if (pass.passName.empty() || !ReadPostProgramReference(
                        passNode,
                        pass.programId,
                        pass.entryId)) {
                    return false;
                }
                pass.composite = VFX::SERIALIZATION::ParseCompositeMode(
                    passNode.value("composite", nlohmann::json{}),
                    pass.composite);
                pass.depthTest = passNode.value("depthTest", pass.depthTest);
                pass.depthWrite = passNode.value("depthWrite", pass.depthWrite);
                pass.doubleSided = passNode.value("doubleSided", pass.doubleSided);
                parsed.passes.push_back(std::move(pass));
            }
        }
        if (parsed.passes.empty()) {
            return false;
        }

        VFX::SERIALIZATION::ReadParameterDescriptorsFromJson(
            root.value("params", nlohmann::json{}),
            parsed.params);

        parsed.ResetValuesFromDefaults();
        *this = std::move(parsed);
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
