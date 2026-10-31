#include "Editor/Tools/Console/HIKARI_ConsoleCommandLine.h"

#include <cmath>
#include <cstdint>
#include <utility>

#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_ParseNumber.h"

namespace HIKARI::EDITOR {
namespace {

bool IsSpace(char c) noexcept { return c == ' ' || c == '\t'; }
bool IsNameStart(char c) noexcept { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool IsNameChar(char c) noexcept { return IsNameStart(c) || (c >= '0' && c <= '9'); }

// pos から値を一つ読む。引用符と括弧で始まる値は閉じるところまで、それ以外は次の空白まで。
bool ReadValue(std::string_view line, size_t& pos, ConsoleToken& token, std::string& error) {
    const size_t begin = pos;
    const char first = pos < line.size() ? line[pos] : '\0';
    if (first == '"' || first == '\'') {
        bool closed = false;
        for (++pos; pos < line.size() && !closed; ++pos) {
            if (first == '"' && line[pos] == '\\' && pos + 1 < line.size() &&
                (line[pos + 1] == '"' || line[pos + 1] == '\\'))
                token.text += line[++pos];
            else if (line[pos] == first)
                closed = true;
            else
                token.text += line[pos];
        }
        if (!closed) {
            error = "Unclosed quote.";
            return false;
        }
        token.raw = line.substr(begin, pos - begin);
    } else if (first == '[' || first == '{') {
        int depth = 0;
        bool inString = false;
        do {
            const char c = line[pos];
            if (inString) {
                if (c == '\\')
                    ++pos;
                else if (c == '"')
                    inString = false;
            } else if (c == '"') {
                inString = true;
            } else if (c == '[' || c == '{') {
                ++depth;
            } else if (c == ']' || c == '}') {
                --depth;
            }
            ++pos;
        } while (depth > 0 && pos < line.size());
        if (depth > 0) {
            error = "Unclosed bracket.";
            return false;
        }
        token.raw = token.text = line.substr(begin, pos - begin);
    } else {
        while (pos < line.size() && !IsSpace(line[pos]))
            ++pos;
        token.raw = token.text = line.substr(begin, pos - begin);
    }
    if (pos < line.size() && !IsSpace(line[pos])) {
        error = "Expected a space after the closing quote or bracket.";
        return false;
    }
    return true;
}

// 宣言した型の JSON にする。Json 型でも JSON として読めない bare な語（列挙名など）は文字列で渡す。
bool ConvertArgument(const EditorOperationParameter& parameter, const ConsoleToken& argument, nlohmann::json& value) {
    switch (parameter.type) {
    case EditorOperationParameterType::String: value = argument.text; return true;
    case EditorOperationParameterType::Float: {
        double number = 0.0;
        if (!TEXT::ParseNumber(argument.text, number) || !std::isfinite(number))
            return false;
        value = number;
        return true;
    }
    case EditorOperationParameterType::Int: {
        int64_t integer = 0;
        uint64_t unsignedInteger = 0;
        if (TEXT::ParseNumber(argument.text, integer))
            value = integer;
        else if (TEXT::ParseNumber(argument.text, unsignedInteger))
            value = unsignedInteger;
        else
            return false;
        return true;
    }
    case EditorOperationParameterType::Bool: {
        const std::string lowered = TEXT::ToLowerAsciiCopy(argument.text);
        if (lowered != "true" && lowered != "false")
            return false;
        value = lowered == "true";
        return true;
    }
    case EditorOperationParameterType::Json: {
        value = nlohmann::json::parse(argument.raw, nullptr, false);
        if (!value.is_discarded())
            return true;
        const char lead = argument.raw.empty() ? '\0' : argument.raw.front();
        if (lead == '\0' || lead == '[' || lead == '{')
            return false;
        value = argument.text;
        return true;
    }
    }
    return false;
}

} // namespace

const EditorOperationDefinition* FindConsoleOperation(const EditorOperationRegistry& operations,
                                                      std::string_view name) {
    const EditorOperationDefinition* operation = operations.Find(std::string(name));
    if (operation == nullptr ||
        !HasEditorOperationExposure(operation->exposureMask, EditorOperationExposureMask::Human))
        return nullptr;
    return operation;
}

bool TokenizeConsoleLine(std::string_view line, std::vector<ConsoleToken>& tokens, std::string& error) {
    tokens.clear();
    size_t pos = 0;
    for (;;) {
        while (pos < line.size() && IsSpace(line[pos]))
            ++pos;
        if (pos == line.size())
            return true;
        ConsoleToken token{};
        if (!tokens.empty() && IsNameStart(line[pos])) {
            size_t end = pos;
            while (end < line.size() && IsNameChar(line[end]))
                ++end;
            if (end < line.size() && line[end] == '=') {
                token.name = line.substr(pos, end - pos);
                pos = end + 1;
            }
        }
        if (!ReadValue(line, pos, token, error))
            return false;
        tokens.push_back(std::move(token));
    }
}

std::vector<const EditorOperationParameter*> AssignArgumentParameters(const EditorOperationDefinition& definition,
                                                                      std::span<const ConsoleToken> arguments) {
    const size_t declared = definition.parameters.size();
    const auto indexOf = [&](const std::string& name) {
        size_t index = 0;
        while (index < declared && definition.parameters[index].name != name)
            ++index;
        return index;
    };
    std::vector<bool> named(declared, false);
    for (const ConsoleToken& argument : arguments) {
        if (const size_t index = indexOf(argument.name); index < declared)
            named[index] = true;
    }

    std::vector<const EditorOperationParameter*> assigned;
    assigned.reserve(arguments.size());
    size_t next = 0;
    for (const ConsoleToken& argument : arguments) {
        size_t index = declared;
        if (!argument.name.empty()) {
            index = indexOf(argument.name);
        } else {
            while (next < declared && named[next])
                ++next;
            if (next < declared)
                index = next++;
        }
        assigned.push_back(index < declared ? &definition.parameters[index] : nullptr);
    }
    return assigned;
}

bool BuildOperationParameters(const EditorOperationDefinition& definition, std::span<const ConsoleToken> arguments,
                              nlohmann::json& parameters, std::string& error) {
    parameters = nlohmann::json::object();
    const std::vector<const EditorOperationParameter*> assigned = AssignArgumentParameters(definition, arguments);
    for (size_t position = 0; position < arguments.size(); ++position) {
        const ConsoleToken& argument = arguments[position];
        const EditorOperationParameter* parameter = assigned[position];
        if (parameter == nullptr && argument.name.empty()) {
            error = definition.id + " takes " + std::to_string(definition.parameters.size()) + " parameters at most.";
            return false;
        }
        const std::string& name = parameter != nullptr ? parameter->name : argument.name;
        if (parameters.contains(name)) {
            error = "Parameter '" + name + "' was given twice.";
            return false;
        }
        if (parameter == nullptr) {
            parameters[name] = argument.text;
            continue;
        }
        if (!ConvertArgument(*parameter, argument, parameters[name])) {
            error = "Parameter '" + name + "' must be " +
                    TEXT::EnumToName(kEditorOperationParameterTypeNames, parameter->type) + ", got '" + argument.raw +
                    "'.";
            return false;
        }
    }
    return true;
}

} // namespace HIKARI::EDITOR
