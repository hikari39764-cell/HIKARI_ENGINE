#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "RenderContracts/Material/HIKARI_MaterialNodeDefinition.h"

namespace HIKARI::EDITOR {

// Normalizes AI-supplied node spellings (exact stable name, "node." prefix,
// case-insensitive match, "additive" alias) to a stable node kind.
std::optional<MaterialNodeKind> ResolveMaterialNodeKind(std::string_view input) noexcept;

// Returns the semantic contract for one node: summary, pin types, class,
// static evaluation and output route. Facts only; no usage advice.
bool DescribeAiMaterialNode(std::string_view nodeType, std::string& outMessage, std::vector<std::string>& outDetails,
                            std::string& outCode);

// Returns the discoverable node catalogue (palette-visible nodes only),
// optionally filtered by keyword and semantic class. Facts only.
bool ListAiMaterialNodes(std::string_view keyword, std::string_view nodeClass, std::string& outMessage,
                         std::vector<std::string>& outDetails, std::string& outCode);

} // namespace HIKARI::EDITOR
