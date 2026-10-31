#pragma once
#include "Render3D/Material/Program/Internal/HIKARI_MaterialExpressionRegisterAllocator.h"

inline void ValidateMaterialRegisterContracts() {
    using namespace HIKARI;
    using namespace HIKARI::RENDER3D::MATERIAL;
    const auto require = [](bool value, const std::string& message) {
        if (!value)
            throw std::runtime_error(message);
    };
    MaterialProgramIR program;
    MaterialProgramInstruction initial;
    initial.outputRegisters = {0};
    program.instructions.push_back(initial);
    uint32_t previous = 0, next = 1;
    for (uint32_t iteration = 0; iteration < 33; ++iteration) {
        MaterialProgramInstruction split;
        split.opcode = MaterialProgramOpcode::Split;
        split.inputRegisters = {previous};
        split.outputRegisters = {next, next + 1, next + 2, next + 3};
        next += 4;
        program.instructions.push_back(split);
        MaterialProgramInstruction combine;
        combine.opcode = MaterialProgramOpcode::MakeFloat4;
        combine.inputRegisters = split.outputRegisters;
        combine.outputRegisters = {next++};
        program.instructions.push_back(combine);
        previous = combine.outputRegisters.front();
    }
    std::unordered_set<size_t> required;
    for (size_t i = 0; i < program.instructions.size(); ++i)
        required.insert(i);
    const std::array<uint32_t, 2> roots{0, previous};
    std::unordered_map<uint32_t, uint8_t> allocation;
    uint32_t peak = 0;
    std::string error;
    require(INTERNAL::AllocateExpressionRegisters(program, required, roots, allocation, peak, error), error);
    require(next > kGpuMaterialExpressionRegisterCount && peak < 16,
            "Dead temporaries were not reused within the existing shader register layout");
    std::array<uint32_t, kGpuMaterialExpressionRegisterCount> symbols;
    symbols.fill(UINT32_MAX);
    for (const auto& instruction : program.instructions) {
        for (auto input : instruction.inputRegisters)
            require(symbols[allocation.at(input)] == input, "A live input was overwritten by register reuse");
        const auto begin = allocation.at(instruction.outputRegisters.front());
        for (size_t i = 0; i < instruction.outputRegisters.size(); ++i) {
            const auto output = instruction.outputRegisters[i];
            require(allocation.at(output) == begin + i, "Split/texture outputs must remain contiguous");
            symbols[allocation.at(output)] = output;
        }
    }
    for (auto root : roots)
        require(symbols[allocation.at(root)] == root, "Final material output was recycled");
    auto invalid = program;
    invalid.instructions.front().inputRegisters = {previous};
    require(!INTERNAL::AllocateExpressionRegisters(invalid, required, roots, allocation, peak, error),
            "Forward reference accepted by the pixel expression allocator");
    invalid = program;
    invalid.instructions[1].outputRegisters[0] = 0;
    require(!INTERNAL::AllocateExpressionRegisters(invalid, required, roots, allocation, peak, error),
            "Repeated SSA definition accepted");
    std::vector<uint32_t> allOutputs;
    for (const auto& instruction : program.instructions)
        allOutputs.insert(allOutputs.end(), instruction.outputRegisters.begin(), instruction.outputRegisters.end());
    require(!INTERNAL::AllocateExpressionRegisters(program, required, allOutputs, allocation, peak, error),
            "True live-register overflow accepted");
}
