#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Runtime/Binding/HIKARI_RuntimeBindingTypes.h"

namespace HIKARI::BINDING {

    inline constexpr uint32_t kCatalogVersion = 1u;

    enum class CatalogDiagnosticSeverity : uint8_t {
        Warning,
        Error,
    };

    struct CatalogDiagnostic {
        CatalogDiagnosticSeverity severity =
            CatalogDiagnosticSeverity::Error;
        std::string code{};
        std::string message{};
        std::string subjectId{};
    };

    struct Catalog {
        uint32_t version = kCatalogVersion;
        std::vector<VariableDefinition> variables{};
        std::vector<EventDefinition> events{};

        const VariableDefinition* FindVariable(
            std::string_view id) const noexcept;
        VariableDefinition* FindVariable(
            std::string_view id) noexcept;
        const EventDefinition* FindEvent(
            std::string_view id) const noexcept;
        EventDefinition* FindEvent(
            std::string_view id) noexcept;
    };

    std::vector<CatalogDiagnostic> ValidateCatalog(
        const Catalog& catalog);
    bool HasCatalogErrors(
        const std::vector<CatalogDiagnostic>& diagnostics) noexcept;

} // namespace HIKARI::BINDING
