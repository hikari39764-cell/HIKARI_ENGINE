#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"

namespace HIKARI::RENDER3D::MATERIAL {

    inline constexpr uint32_t kGpuMaterialExpressionHeaderSlotCount = 4u;
    inline constexpr uint32_t kGpuMaterialExpressionValueSlotCount = 48u;
    inline constexpr uint32_t kGpuMaterialExpressionInstructionCount = 96u;
    inline constexpr uint32_t kGpuMaterialExpressionRegisterCount = 128u;
    inline constexpr uint32_t kGpuMaterialExpressionResourceBase = 10u;
    inline constexpr uint32_t kGpuMaterialExpressionMagic = 0x484D4558u;
    inline constexpr uint32_t kGpuMaterialExpressionVersion = 1u;
    inline constexpr uint32_t kGpuMaterialExpressionHeaderBase =
        kGpuMaterialProgramResponseSlotCount;
    inline constexpr uint32_t kGpuMaterialExpressionValueBase =
        kGpuMaterialExpressionHeaderBase +
        kGpuMaterialExpressionHeaderSlotCount;
    inline constexpr uint32_t kGpuMaterialExpressionInstructionBase =
        kGpuMaterialExpressionValueBase +
        kGpuMaterialExpressionValueSlotCount;

    enum class MaterialExpressionRuntimeOpcode : uint8_t {
        Constant,
        Parameter,
        TextureSample,
        Add,
        Subtract,
        Multiply,
        Divide,
        Lerp,
        Minimum,
        Maximum,
        Clamp,
        Saturate,
        OneMinus,
        Power,
        Absolute,
        Negate,
        Floor,
        Ceil,
        Fraction,
        Modulo,
        SquareRoot,
        Sine,
        Cosine,
        Step,
        SmoothStep,
        Remap,
        Dot,
        Cross,
        Length,
        Normalize,
        Distance,
        Reflect,
        MakeFloat2,
        MakeFloat3,
        MakeFloat4,
        Split,
        Select,
        NormalUnpack,
        TextureCoordinate,
        Time,
        WorldPosition,
        WorldNormal,
        WorldTangent,
        ViewDirection,
        CameraPosition,
        ScreenPosition,
        DerivativeX,
        DerivativeY,
        FilterWidth,
    };

    struct MaterialExpressionRuntimeProgram {
        bool active = false;
        uint32_t instructionCount = 0u;
        uint32_t registerCount = 0u;
        uint32_t valueCount = 0u;
        std::vector<MATH::Vec4> parameters{};
        std::vector<uint32_t> resourceBindingIndices{};
        std::vector<std::pair<std::string, uint32_t>> nodeInstructionCounts{};
    };

    struct MaterialExpressionBudget {
        bool active = false;
        uint32_t instructionCount = 0u;
        uint32_t registerCount = 0u;
        uint32_t valueCount = 0u;
        uint32_t parameterSlotCount = 0u;
        uint32_t resourceCount = 0u;
        uint32_t maxInstructionCount = kGpuMaterialExpressionInstructionCount;
        uint32_t maxRegisterCount = kGpuMaterialExpressionRegisterCount;
        uint32_t maxValueCount = kGpuMaterialExpressionValueSlotCount;
        uint32_t maxParameterSlotCount =
            kGpuMaterialProgramParameterSlotCount;
        uint32_t maxResourceSlotCount =
            kGpuMaterialProgramResourceSlotCount;
        std::vector<std::pair<std::string, uint32_t>> nodeInstructionCounts{};
    };

    bool BuildMaterialExpressionRuntimeProgram(
        const MaterialProgramIR& program,
        MaterialExpressionRuntimeProgram& outProgram,
        std::string& outError);

    bool ResolveMaterialExpressionBudget(
        const MaterialProgramIR& program,
        MaterialExpressionBudget& outBudget,
        std::string& outError);

    bool MaterialProgramHasRuntimeExpression(
        const MaterialProgramIR& program);

} // namespace HIKARI::RENDER3D::MATERIAL
