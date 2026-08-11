#include "Runtime/Binding/HIKARI_RuntimeBindingCatalog.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace HIKARI::BINDING {
    namespace {

        bool IsValidId(std::string_view id) noexcept {
            if (id.empty() || id.size() > 128u) {
                return false;
            }
            return std::all_of(id.begin(), id.end(), [](char value) {
                return (value >= 'a' && value <= 'z') ||
                    (value >= 'A' && value <= 'Z') ||
                    (value >= '0' && value <= '9') ||
                    value == '-' || value == '_' || value == '.';
            });
        }

    } // namespace

    const VariableDefinition* Catalog::FindVariable(
        std::string_view id) const noexcept {

        const auto found = std::find_if(
            variables.begin(), variables.end(),
            [id](const VariableDefinition& candidate) {
                return candidate.id == id;
            });
        return found == variables.end() ? nullptr : &*found;
    }

    VariableDefinition* Catalog::FindVariable(
        std::string_view id) noexcept {

        return const_cast<VariableDefinition*>(
            std::as_const(*this).FindVariable(id));
    }

    const EventDefinition* Catalog::FindEvent(
        std::string_view id) const noexcept {

        const auto found = std::find_if(
            events.begin(), events.end(),
            [id](const EventDefinition& candidate) {
                return candidate.id == id;
            });
        return found == events.end() ? nullptr : &*found;
    }

    EventDefinition* Catalog::FindEvent(
        std::string_view id) noexcept {

        return const_cast<EventDefinition*>(
            std::as_const(*this).FindEvent(id));
    }

    std::vector<CatalogDiagnostic> ValidateCatalog(
        const Catalog& catalog) {

        std::vector<CatalogDiagnostic> diagnostics{};
        if (catalog.version != kCatalogVersion) {
            diagnostics.push_back({
                CatalogDiagnosticSeverity::Error,
                "Binding.CatalogVersionUnsupported",
                "Runtime binding catalog version is not supported.",
                {}
            });
        }

        std::unordered_set<std::string> ids{};
        for (const VariableDefinition& variable : catalog.variables) {
            if (!IsValidId(variable.id)) {
                diagnostics.push_back({
                    CatalogDiagnosticSeverity::Error,
                    "Binding.VariableIdInvalid",
                    "Variable id must be a stable non-empty ASCII id.",
                    variable.id
                });
            } else if (!ids.emplace(variable.id).second) {
                diagnostics.push_back({
                    CatalogDiagnosticSeverity::Error,
                    "Binding.IdDuplicate",
                    "Variable and event ids must be globally unique.",
                    variable.id
                });
            }
            if (variable.name.empty()) {
                diagnostics.push_back({
                    CatalogDiagnosticSeverity::Error,
                    "Binding.VariableNameEmpty",
                    "Variable display name cannot be empty.",
                    variable.id
                });
            }
            if (!IsValueFinite(variable.defaultValue)) {
                diagnostics.push_back({
                    CatalogDiagnosticSeverity::Error,
                    "Binding.VariableDefaultInvalid",
                    "Variable default value is not finite or type-compatible.",
                    variable.id
                });
            }
            if (variable.hasRange &&
                (!IsNumeric(variable.defaultValue.type) ||
                 variable.minimum > variable.maximum ||
                 variable.step <= 0.0f ||
                 !std::isfinite(variable.minimum) ||
                 !std::isfinite(variable.maximum) ||
                 !std::isfinite(variable.step))) {
                diagnostics.push_back({
                    CatalogDiagnosticSeverity::Error,
                    "Binding.VariableRangeInvalid",
                    "Numeric range requires finite min/max and a positive step.",
                    variable.id
                });
            } else if (variable.hasRange &&
                !IsValueWithinRange(
                    variable.defaultValue,
                    variable.minimum,
                    variable.maximum)) {
                diagnostics.push_back({
                    CatalogDiagnosticSeverity::Error,
                    "Binding.VariableDefaultOutOfRange",
                    "Variable default value must be inside its numeric range.",
                    variable.id
                });
            }
        }

        for (const EventDefinition& event : catalog.events) {
            if (!IsValidId(event.id)) {
                diagnostics.push_back({
                    CatalogDiagnosticSeverity::Error,
                    "Binding.EventIdInvalid",
                    "Event id must be a stable non-empty ASCII id.",
                    event.id
                });
            } else if (!ids.emplace(event.id).second) {
                diagnostics.push_back({
                    CatalogDiagnosticSeverity::Error,
                    "Binding.IdDuplicate",
                    "Variable and event ids must be globally unique.",
                    event.id
                });
            }
            if (event.name.empty()) {
                diagnostics.push_back({
                    CatalogDiagnosticSeverity::Error,
                    "Binding.EventNameEmpty",
                    "Event display name cannot be empty.",
                    event.id
                });
            }
        }
        return diagnostics;
    }

    bool HasCatalogErrors(
        const std::vector<CatalogDiagnostic>& diagnostics) noexcept {

        return std::any_of(
            diagnostics.begin(), diagnostics.end(),
            [](const CatalogDiagnostic& diagnostic) {
                return diagnostic.severity ==
                    CatalogDiagnosticSeverity::Error;
            });
    }

} // namespace HIKARI::BINDING
