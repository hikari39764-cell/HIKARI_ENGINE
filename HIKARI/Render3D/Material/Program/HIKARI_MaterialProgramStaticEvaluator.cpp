#include "Render3D/Material/Program/HIKARI_MaterialProgramStaticEvaluator.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Core/Text/HIKARI_AsciiId.h"

namespace HIKARI::RENDER3D::MATERIAL {
namespace {

std::string NormalizeId(std::string_view id) {
  return TEXT::NormalizeAsciiId(id);
}

void SetRgb(MATH::Vec4 &target, const MATH::Vec4 &value) {
  target.x = value.x;
  target.y = value.y;
  target.z = value.z;
}

void ApplyNamedParameter(
    std::span<MATH::Vec4> values,
    const MaterialProgramParameterBinding &parameter) {
  const std::string id = NormalizeId(parameter.parameterId);
  MATH::Vec4 value = parameter.defaultValue.numeric;
  if (parameter.type == MaterialValueType::Bool) {
    value = {parameter.defaultValue.boolean ? 1.0f : 0.0f,
             0.0f, 0.0f, 0.0f};
  }
  if (id == "parametershadowthreshold") values[0].x = value.x;
  else if (id == "parametershadowsoftness") values[0].y = value.x;
  else if (id == "parametermidthreshold") values[0].z = value.x;
  else if (id == "parameterramprow") values[16].x = value.x;
  else if (id == "parameterrampmaskinfluence") values[16].y = value.x;
  else if (id == "parameterrampblend") values[16].z = value.x;
  else if (id == "parameterramplayerweights") values[20] = value;
  else if (id == "parameterrampintensity") values[16].w = value.x;
  else if (id == "parametertoondirectgain") values[0].w = value.x;
  else if (id == "parametershadowtint") SetRgb(values[1], value);
  else if (id == "parametermidtint") SetRgb(values[2], value);
  else if (id == "parameterlittint") SetRgb(values[3], value);
  else if (id == "parameterspecularbandwidth") values[4].x = value.x;
  else if (id == "parameterspecularbandsoftness") values[4].y = value.x;
  else if (id == "parameterspecularintensity") values[4].z = value.x;
  else if (id == "parameterspecularpower") values[4].w = value.x;
  else if (id == "parameterspeculartint") SetRgb(values[5], value);
  else if (id == "parameterhairhighlighttint") SetRgb(values[5], value);
  else if (id == "parameterrimcolor") SetRgb(values[6], value);
  else if (id == "parameterrimintensity") values[6].w = value.x;
  else if (id == "parameterrimpower") values[7].x = value.x;
  else if (id == "parameterrimwidth") values[7].y = value.x;
  else if (id == "parameterrimlightalignment") values[7].z = value.x;
  else if (id == "parametermatcaptint") SetRgb(values[8], value);
  else if (id == "parametermatcapintensity") values[8].w = value.x;
  else if (id == "parameterhairshift") values[9].x = value.x;
  else if (id == "parameterhairwidth") values[9].y = value.x;
  else if (id == "parameterhairsecondaryshift") values[9].z = value.x;
  else if (id == "parameterhairintensity") values[9].w = value.x;
  else if (id == "parameterhairprimarypower") values[21].x = value.x;
  else if (id == "parameterhairsecondarypower") values[21].y = value.x;
  else if (id == "parameterhairsecondaryintensity") values[21].z = value.x;
  else if (id == "parameterhairflowstrength") values[21].w = value.x;
  else if (id == "parameterfaceshadowsoftness") values[10].x = value.x;
  else if (id == "parameterfaceshadowoffset") values[10].y = value.x;
  else if (id == "parameterhairshadowstrength") values[10].z = value.x;
  else if (id == "parameterfaceright") SetRgb(values[17], value);
  else if (id == "parameterfaceforward") SetRgb(values[18], value);
  else if (id == "parameteririsdepth") values[11].x = value.x;
  else if (id == "parametereyesparkle") values[11].y = value.x;
  else if (id == "parametereyesparklepower") values[11].z = value.x;
  else if (id == "parameteririscenter") {
    values[22].x = value.x;
    values[22].y = value.y;
  }
  else if (id == "parameteririsradius") values[22].z = value.x;
  else if (id == "parameterirismasksoftness") values[22].w = value.x;
  else if (id == "parametereyelayermode") values[23].x = value.x;
  else if (id == "parametereyehighlightopacity") values[23].y = value.x;
  else if (id == "parametereyehighlightthreshold") values[23].z = value.x;
  else if (id == "parametereyehighlightsoftness") values[23].w = value.x;
  else if (id == "parametereyeparallaxscale") values[24].x = value.x;
  else if (id == "parametereyehighlightintensity") values[24].y = value.x;
  else if (id == "parametereyebaselift") values[24].z = value.x;
  else if (id == "parametereyecausticintensity") values[24].w = value.x;
  else if (id == "parametermatcapviewcorrection") values[25].x = value.x;
  else if (id == "parametermatcapsphericalnormalblend") values[25].y = value.x;
  else if (id == "parametereyecaustictint") SetRgb(values[26], value);
  else if (id == "parametereyecausticwidth") values[26].w = value.x;
  else if (id == "parametercorneahighlightmode") values[27].x = value.x;
  else if (id == "parametercorneahighlighttravel") values[27].y = value.x;
  else if (id == "parametercorneahighlightviewinfluence") values[27].z = value.x;
  else if (id == "parametercorneahighlightlightinfluence") values[27].w = value.x;
  else if (id == "parametercorneahighlightcurvature") values[28].x = value.x;
  else if (id == "parametercorneahighlightminbrightness") values[28].y = value.x;
  else if (id == "parametercorneahighlightlightresponse") values[28].z = value.x;
  else if (id == "parametercorneahighlightedgefade") values[28].w = value.x;
  else if (id == "parametercorneahighlightsharpness") values[29].x = value.x;
  else if (id == "parametercorneahighlightlightcolor") values[29].y = value.x;
  else if (id == "parameterpbrnprblend") {
    values[12].x = value.x;
    values[12].y = value.x;
    values[12].z = value.x;
  } else if (id == "parameterdirectdiffusepbrblend") values[12].x = value.x;
  else if (id == "parameterdirectspecularpbrblend") values[12].y = value.x;
  else if (id == "parameterindirectpbrblend") values[12].z = value.x;
  else if (id == "parameterfabricsheen") values[13].x = value.x;
  else if (id == "parameterfiberroughness") values[13].y = value.x;
  else if (id == "parameterfabricsheenpower") values[13].z = value.x;
  else if (id == "parameterfabricsheenintensity") values[13].w = value.x;
  else if (id == "parameterskinwrap") values[14].x = value.x;
  else if (id == "parameterskinsubsurface") values[14].y = value.x;
  else if (id == "parameterskintint") SetRgb(values[15], value);
  else if (id == "parameteroutlinecolor") SetRgb(values[19], value);
  else if (id == "parameteroutlinethickness") values[19].w = value.x;
}

float ReadComponent(const MaterialValueData& value,
                    uint32_t component) noexcept {
  if (value.type == MaterialValueType::Bool) {
    return value.boolean ? 1.0f : 0.0f;
  }
  const float values[]{value.numeric.x, value.numeric.y, value.numeric.z,
                       value.numeric.w};
  const uint32_t count = GetMaterialValueComponentCount(value.type);
  return values[count <= 1u ? 0u : (std::min)(component, count - 1u)];
}

using StaticRegisterFile =
    std::vector<std::optional<MaterialValueData>>;

const MaterialValueData* ReadStaticInput(
    const MaterialProgramInstruction& instruction, std::string_view pin,
    const StaticRegisterFile& registers) noexcept {
  const auto found = std::find(instruction.inputPins.begin(),
                               instruction.inputPins.end(), pin);
  if (found == instruction.inputPins.end()) {
    return nullptr;
  }
  const size_t index = static_cast<size_t>(
      std::distance(instruction.inputPins.begin(), found));
  const uint32_t registerIndex = instruction.inputRegisters[index];
  if (registerIndex == kInvalidMaterialProgramRegister ||
      registerIndex >= registers.size() ||
      !registers[registerIndex].has_value()) {
    return nullptr;
  }
  return &*registers[registerIndex];
}

StaticRegisterFile EvaluateStaticValues(
    const MaterialProgramIR& program,
    std::span<const MaterialProgramParameterValueOverride> overrides) {
  StaticRegisterFile registers(program.registerCount);
  for (const MaterialProgramInstruction& instruction : program.instructions) {
    std::optional<MaterialValueData> result{};
    if (instruction.opcode == MaterialProgramOpcode::Constant) {
      result = instruction.immediate;
    } else if (instruction.opcode == MaterialProgramOpcode::Parameter &&
               instruction.bindingIndex < program.parameters.size()) {
      const MaterialValueData* overrideValue =
          FindMaterialParameterOverride(
              overrides, instruction.bindingIndex);
      result = overrideValue != nullptr
          ? *overrideValue
          : program.parameters[instruction.bindingIndex].defaultValue;
    } else {
      const MaterialValueData* a = ReadStaticInput(
          instruction,
          instruction.opcode == MaterialProgramOpcode::Power
              ? "Base"
          : instruction.opcode == MaterialProgramOpcode::Clamp ||
                  instruction.opcode == MaterialProgramOpcode::Saturate ||
                  instruction.opcode == MaterialProgramOpcode::OneMinus
              ? "Value"
              : "A",
          registers);
      const MaterialValueData* b = ReadStaticInput(
          instruction,
          instruction.opcode == MaterialProgramOpcode::Power
              ? "Exponent"
          : instruction.opcode == MaterialProgramOpcode::Clamp ? "Min" : "B",
          registers);
      const MaterialValueData* c = ReadStaticInput(
          instruction,
          instruction.opcode == MaterialProgramOpcode::Lerp ? "Alpha" : "Max",
          registers);
      const bool unary = instruction.opcode == MaterialProgramOpcode::Saturate ||
                         instruction.opcode == MaterialProgramOpcode::OneMinus;
      const bool ternary = instruction.opcode == MaterialProgramOpcode::Lerp ||
                           instruction.opcode == MaterialProgramOpcode::Clamp;
      const bool numericOpcode =
          instruction.opcode >= MaterialProgramOpcode::Add &&
          instruction.opcode <= MaterialProgramOpcode::Dot;
      if (numericOpcode && a != nullptr && (unary || b != nullptr) &&
          (!ternary || c != nullptr)) {
        MaterialValueData value{};
        value.type = instruction.opcode == MaterialProgramOpcode::Dot
                         ? MaterialValueType::Float1
                         : instruction.valueType;
        float* output = &value.numeric.x;
        const uint32_t componentCount =
            GetMaterialValueComponentCount(value.type);
        for (uint32_t component = 0u; component < componentCount; ++component) {
          const float left = ReadComponent(*a, component);
          const float right =
              b != nullptr ? ReadComponent(*b, component) : 0.0f;
          if (instruction.opcode == MaterialProgramOpcode::Add) {
            output[component] = left + right;
          } else if (instruction.opcode == MaterialProgramOpcode::Multiply) {
            output[component] = left * right;
          } else if (instruction.opcode == MaterialProgramOpcode::Lerp) {
            const float alpha = ReadComponent(*c, component);
            output[component] = left + (right - left) * alpha;
          } else if (instruction.opcode == MaterialProgramOpcode::Clamp) {
            output[component] =
                std::clamp(left, right, ReadComponent(*c, component));
          } else if (instruction.opcode == MaterialProgramOpcode::Saturate) {
            output[component] = std::clamp(left, 0.0f, 1.0f);
          } else if (instruction.opcode == MaterialProgramOpcode::OneMinus) {
            output[component] = 1.0f - left;
          } else if (instruction.opcode == MaterialProgramOpcode::Power) {
            output[component] = std::pow((std::max)(left, 0.0f), right);
          } else if (instruction.opcode == MaterialProgramOpcode::Dot) {
            output[0] = ReadComponent(*a, 0u) * ReadComponent(*b, 0u) +
                        ReadComponent(*a, 1u) * ReadComponent(*b, 1u) +
                        ReadComponent(*a, 2u) * ReadComponent(*b, 2u);
            break;
          }
        }
        result = value;
      } else if (instruction.opcode == MaterialProgramOpcode::Atan2 &&
                 a != nullptr && b != nullptr) {
        MaterialValueData value{};
        value.type = MaterialValueType::Float1;
        value.numeric.x = std::atan2(a->numeric.x, b->numeric.x);
        result = value;
      } else if (instruction.opcode == MaterialProgramOpcode::Exp &&
                 a != nullptr) {
        MaterialValueData value{};
        value.type = MaterialValueType::Float1;
        value.numeric.x = std::exp(a->numeric.x);
        result = value;
      } else if (instruction.opcode == MaterialProgramOpcode::Log2 &&
                 a != nullptr) {
        MaterialValueData value{};
        value.type = MaterialValueType::Float1;
        value.numeric.x =
            std::log2((std::max)(a->numeric.x, 1.0e-6f));
        result = value;
      } else if (instruction.opcode == MaterialProgramOpcode::Sign &&
                 a != nullptr) {
        MaterialValueData value{};
        value.type = MaterialValueType::Float1;
        value.numeric.x =
            a->numeric.x > 0.0f
                ? 1.0f
                : (a->numeric.x < 0.0f ? -1.0f : 0.0f);
        result = value;
      } else if (instruction.opcode == MaterialProgramOpcode::Round &&
                 a != nullptr) {
        MaterialValueData value{};
        value.type = MaterialValueType::Float1;
        value.numeric.x = std::round(a->numeric.x);
        result = value;
      } else if (instruction.opcode == MaterialProgramOpcode::Trunc &&
                 a != nullptr) {
        MaterialValueData value{};
        value.type = MaterialValueType::Float1;
        value.numeric.x = std::trunc(a->numeric.x);
        result = value;
      }
    }
    if (!result.has_value() || instruction.outputRegisters.empty()) {
      continue;
    }
    const uint32_t outputRegister = instruction.outputRegisters.front();
    if (outputRegister < registers.size()) {
      registers[outputRegister] = *result;
    }
  }
  return registers;
}

} // namespace

void ApplyConnectedMaterialResponseParameters(
    const MaterialProgramIR& program,
    std::span<MATH::Vec4> parameters,
    std::span<const MaterialProgramParameterValueOverride> overrides) {
  // The response head is a fixed 30-slot block and the only region this
  // evaluator writes; expression payloads follow after it. Guarding against
  // the total 178-slot count silently disabled every authored NPR parameter
  // when the runtime builder passed the 30-slot response block.
  if (parameters.size() < kGpuMaterialProgramResponseSlotCount) {
    return;
  }
  for (uint32_t index = 0u; index < program.parameters.size(); ++index) {
    MaterialProgramParameterBinding parameter = program.parameters[index];
    if (const MaterialValueData* overrideValue =
            FindMaterialParameterOverride(overrides, index)) {
      parameter.defaultValue = *overrideValue;
    }
    ApplyNamedParameter(parameters, parameter);
  }
  const StaticRegisterFile registers =
      EvaluateStaticValues(program, overrides);
  for (const MaterialProgramInstruction& instruction : program.instructions) {
    const auto scalar = [&](std::string_view pin, float& target) {
      if (const MaterialValueData* value =
              ReadStaticInput(instruction, pin, registers)) {
        target = ReadComponent(*value, 0u);
      }
    };
    const auto rgb = [&](std::string_view pin, MATH::Vec4& target) {
      if (const MaterialValueData* value =
              ReadStaticInput(instruction, pin, registers)) {
        target.x = ReadComponent(*value, 0u);
        target.y = ReadComponent(*value, 1u);
        target.z = ReadComponent(*value, 2u);
      }
    };
    if (instruction.opcode == MaterialProgramOpcode::RampDiffuse ||
        instruction.opcode == MaterialProgramOpcode::MultiRampDiffuse) {
      scalar("ShadowThreshold", parameters[0].x);
      scalar("ShadowSoftness", parameters[0].y);
      scalar("MidThreshold", parameters[0].z);
      rgb("ShadowTint", parameters[1]);
      rgb("MidTint", parameters[2]);
      rgb("LitTint", parameters[3]);
      scalar("RampRow", parameters[16].x);
      scalar("MaskInfluence", parameters[16].y);
      scalar("RampBlend", parameters[16].z);
      if (const MaterialValueData* value =
              ReadStaticInput(instruction, "LayerWeights", registers)) {
        parameters[20].x = ReadComponent(*value, 0u);
        parameters[20].y = ReadComponent(*value, 1u);
        parameters[20].z = ReadComponent(*value, 2u);
        parameters[20].w = ReadComponent(*value, 3u);
      }
      scalar("Intensity", parameters[16].w);
      scalar("DirectGain", parameters[0].w);
    } else if (instruction.opcode == MaterialProgramOpcode::SpecularBand) {
      scalar("Width", parameters[4].x);
      scalar("Softness", parameters[4].y);
      scalar("Intensity", parameters[4].z);
      scalar("Power", parameters[4].w);
      rgb("Tint", parameters[5]);
    } else if (instruction.opcode == MaterialProgramOpcode::FresnelRim) {
      scalar("Power", parameters[7].x);
      scalar("Width", parameters[7].y);
      rgb("Color", parameters[6]);
      scalar("Intensity", parameters[6].w);
      scalar("LightAlignment", parameters[7].z);
    } else if (instruction.opcode == MaterialProgramOpcode::AnisotropicHair) {
      scalar("Shift", parameters[9].x);
      scalar("Width", parameters[9].y);
      scalar("PrimaryPower", parameters[21].x);
      scalar("SecondaryPower", parameters[21].y);
      scalar("SecondaryIntensity", parameters[21].z);
      scalar("FlowStrength", parameters[21].w);
    } else if (instruction.opcode == MaterialProgramOpcode::FaceSdf) {
      scalar("Softness", parameters[10].x);
      scalar("DirectGain", parameters[0].w);
    } else if (instruction.opcode == MaterialProgramOpcode::SkinLighting) {
      scalar("Wrap", parameters[14].x);
      scalar("Subsurface", parameters[14].y);
      rgb("Tint", parameters[15]);
    } else if (instruction.opcode == MaterialProgramOpcode::FabricSheen) {
      scalar("Sheen", parameters[13].x);
      scalar("FiberRoughness", parameters[13].y);
      scalar("Power", parameters[13].z);
      scalar("Intensity", parameters[13].w);
    } else if (instruction.opcode == MaterialProgramOpcode::HybridLighting) {
      scalar("DirectDiffuseBlend", parameters[12].x);
      scalar("DirectSpecularBlend", parameters[12].y);
      scalar("IndirectBlend", parameters[12].z);
    } else if (instruction.opcode == MaterialProgramOpcode::Outline) {
      rgb("Color", parameters[19]);
      scalar("Thickness", parameters[19].w);
    } else if (instruction.opcode == MaterialProgramOpcode::HairShadow) {
      scalar("Strength", parameters[10].z);
    }
  }
}

} // namespace HIKARI::RENDER3D::MATERIAL
