#include "Render3D/Material/Program/HIKARI_MaterialParameterProjection.h"

#include <algorithm>

#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramStaticEvaluator.h"

namespace HIKARI::RENDER3D::MATERIAL {
    namespace {

        std::vector<MATH::Vec4> BuildDefaultResponseParameters() {
            std::vector<MATH::Vec4> values(
                kGpuMaterialProgramResponseSlotCount,
                MATH::Vec4{ 0.0f, 0.0f, 0.0f, 0.0f });
            values[0] = { 0.50f, 0.08f, 0.72f, 1.0f };
            values[1] = { 0.56f, 0.62f, 0.78f, 1.0f };
            values[2] = { 0.82f, 0.86f, 0.94f, 1.0f };
            values[3] = { 1.0f, 1.0f, 1.0f, 1.0f };
            values[4] = { 0.10f, 0.025f, 1.0f, 64.0f };
            values[5] = { 1.0f, 1.0f, 1.0f, 1.0f };
            values[6] = { 0.55f, 0.68f, 1.0f, 1.0f };
            values[7] = { 4.0f, 0.22f, 1.0f, 0.0f };
            values[8] = { 1.0f, 1.0f, 1.0f, 1.0f };
            values[9] = { 0.08f, 0.18f, -0.10f, 1.0f };
            values[10] = { 0.04f, 0.0f, 0.65f, 0.0f };
            values[11] = { 0.12f, 0.35f, 64.0f, 1.0f };
            values[12] = { 0.25f, 0.35f, 0.65f, 0.25f };
            values[13] = { 0.28f, 0.72f, 4.0f, 1.0f };
            values[14] = { 0.18f, 0.0f, 0.0f, 0.0f };
            values[15] = { 1.0f, 0.42f, 0.32f, 1.0f };
            values[16] = { 0.5f, 0.0f, 0.5f, 1.0f };
            values[17] = { 1.0f, 0.0f, 0.0f, 0.0f };
            values[18] = { 0.0f, 0.0f, 1.0f, 0.0f };
            values[19] = { 0.025f, 0.030f, 0.045f, 2.0f };
            // Multi-ramp layer weights. Zero keeps the legacy two-ramp path.
            values[20] = { 0.0f, 0.0f, 0.0f, 0.0f };
            // Hair dual-lobe powers, secondary intensity, and flow strength.
            values[21] = { 96.0f, 48.0f, 0.45f, 1.0f };
            // Analytic eye mask center.xy, radius, and edge softness.
            values[22] = { 0.50f, 0.50f, 0.48f, 0.08f };
            // Eye layer mode, opacity, highlight threshold, and softness.
            values[23] = { 0.0f, 1.0f, 0.02f, 0.04f };
            // Eye parallax scale, HDR highlight, iris lift, and caustic gain.
            values[24] = { 0.10f, 2.0f, 0.18f, 0.25f };
            // MatCap view correction and eye spherical-normal blend.
            values[25] = { 1.0f, 0.0f, 0.0f, 0.0f };
            // Eye caustic tint and band width.
            values[26] = { 1.0f, 0.72f, 0.38f, 0.28f };
            // Cornea mode, travel, and weighted half-vector inputs.
            values[27] = { 2.0f, 0.10f, 0.35f, 0.65f };
            // Cornea curvature, retained brightness, response, and edge fade.
            values[28] = { 0.75f, 0.45f, 0.65f, 0.0f };
            // Cornea highlight sharpness and light-color influence.
            values[29] = { 12.0f, 0.25f, 0.0f, 0.0f };
            return values;
        }

    } // namespace

    std::shared_ptr<const MaterialParameterProjectionRecipe>
        BuildMaterialParameterProjectionRecipe(
            const MaterialProgramIR& program) {

        auto recipe = std::make_shared<MaterialParameterProjectionRecipe>();
        for (uint32_t index = 0u;
             index < program.parameters.size();
             ++index) {
            const MaterialProgramParameterBinding& parameter =
                program.parameters[index];
            if (parameter.externalBindingId.empty()) {
                continue;
            }
            recipe->bindings.push_back({
                index,
                parameter.externalBindingId,
                parameter.type
            });
        }
        if (recipe->bindings.empty()) {
            return {};
        }
        recipe->program = program;
        return recipe;
    }

    bool BuildMaterialProgramParameterBlock(
        const MaterialProgramIR& program,
        std::span<const MaterialProgramParameterValueOverride> overrides,
        std::vector<MATH::Vec4>& outParameters,
        std::string& outError) {

        outError.clear();
        std::vector<MATH::Vec4> responseParameters =
            BuildDefaultResponseParameters();
        ApplyConnectedMaterialResponseParameters(
            program, responseParameters, overrides);

        MaterialExpressionRuntimeProgram expression{};
        if (!BuildMaterialExpressionRuntimeProgram(
                program, expression, outError, overrides)) {
            outParameters = std::move(responseParameters);
            return false;
        }
        if (!expression.active) {
            outParameters = std::move(responseParameters);
            return true;
        }
        if (expression.parameters.size() < responseParameters.size()) {
            outError = "material response head exceeds the expression parameter block";
            outParameters = std::move(responseParameters);
            return false;
        }
        std::copy(
            responseParameters.begin(), responseParameters.end(),
            expression.parameters.begin());
        if (expression.parameters.size() >
            kGpuMaterialProgramParameterSlotCount) {
            outError = "material parameter block exceeds the GPU slot budget";
            outParameters = std::move(responseParameters);
            return false;
        }
        outParameters = std::move(expression.parameters);
        return true;
    }

    bool ApplyMaterialExternalValues(
        HIKARI::Material& material,
        const MaterialExternalValueSnapshot& snapshot,
        std::string& outError) {

        outError.clear();
        if (!material.NeedsMaterialParameterProjection(
                snapshot.sourceInstanceId,
                snapshot.revision)) {
            return false;
        }
        const auto& recipe = material.GetMaterialParameterProjection();
        if (recipe == nullptr) {
            return false;
        }

        std::vector<MaterialProgramParameterValueOverride> overrides{};
        overrides.reserve(recipe->bindings.size());
        for (const MaterialParameterProjectionBinding& binding :
             recipe->bindings) {
            const auto value = snapshot.values.find(
                binding.externalBindingId);
            if (value == snapshot.values.end() ||
                value->second.type != binding.type) {
                continue;
            }
            overrides.push_back({binding.bindingIndex, value->second});
        }

        std::vector<MATH::Vec4> parameters{};
        if (!BuildMaterialProgramParameterBlock(
                recipe->program,
                overrides,
                parameters,
                outError)) {
            return false;
        }
        material.ApplyMaterialParameterProjection(
            std::move(parameters),
            snapshot.sourceInstanceId,
            snapshot.revision);
        return true;
    }

} // namespace HIKARI::RENDER3D::MATERIAL
