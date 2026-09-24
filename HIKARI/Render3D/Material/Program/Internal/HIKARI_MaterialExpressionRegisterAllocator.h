#pragma once
#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"
#include <algorithm>
#include <array>
#include <span>
#include <unordered_map>
#include <unordered_set>

namespace HIKARI::RENDER3D::MATERIAL::INTERNAL {
// IR は SSA、GPU は固定長レジスタ配列。最終参照後に領域を再利用する。
// TextureSample / Split の複数出力は連続領域という既存の GPU ABI を守る。
inline bool AllocateExpressionRegisters(const MaterialProgramIR& program, const std::unordered_set<size_t>& required,
                                        std::span<const uint32_t> roots,
                                        std::unordered_map<uint32_t, uint8_t>& registers, uint32_t& peak,
                                        std::string& error) {
    registers.clear();
    peak = 0;
    std::unordered_map<uint32_t, size_t> lastUse;
    for (size_t index = 0; index < program.instructions.size(); ++index) {
        if (!required.contains(index))
            continue;
        const auto& instruction = program.instructions[index];
        for (auto input : instruction.inputRegisters) {
            if (input == kInvalidMaterialProgramRegister)
                continue;
            const auto found = lastUse.find(input);
            if (found == lastUse.end()) {
                error = "Material expression reads a value before its definition";
                return false;
            }
            found->second = index;
        }
        for (auto output : instruction.outputRegisters) {
            if (output == kInvalidMaterialProgramRegister || !lastUse.emplace(output, index).second) {
                error = "Material expression contains an invalid or repeated definition";
                return false;
            }
        }
    }
    for (auto root : roots) {
        const auto found = lastUse.find(root);
        if (found == lastUse.end()) {
            error = "Material expression output has no definition";
            return false;
        }
        found->second = program.instructions.size();
    }
    std::vector<std::vector<uint32_t>> retire(program.instructions.size());
    for (const auto& [reg, last] : lastUse)
        if (last < retire.size())
            retire[last].push_back(reg);
    std::array<bool, kGpuMaterialExpressionRegisterCount> live{};
    for (size_t index = 0; index < program.instructions.size(); ++index) {
        if (!required.contains(index))
            continue;
        const auto& outputs = program.instructions[index].outputRegisters;
        const auto count = static_cast<uint32_t>(outputs.size());
        if (!count || count > live.size()) {
            error = "Invalid material expression output count";
            return false;
        }
        uint32_t begin = 0;
        for (; begin + count <= live.size(); ++begin) {
            if (std::none_of(live.begin() + begin, live.begin() + begin + count, [](bool value) { return value; }))
                break;
        }
        if (begin + count > live.size()) {
            error = "Material expression exceeds " + std::to_string(live.size()) + " simultaneously live registers";
            return false;
        }
        for (uint32_t offset = 0; offset < count; ++offset) {
            registers.emplace(outputs[offset], static_cast<uint8_t>(begin + offset));
            live[begin + offset] = true;
        }
        peak = (std::max)(peak, begin + count);
        // 入力を読んだ後に解放する。出力先と生存入力の重なりを作らない。
        for (auto reg : retire[index])
            live[registers.at(reg)] = false;
    }
    return true;
}
} // namespace HIKARI::RENDER3D::MATERIAL::INTERNAL
