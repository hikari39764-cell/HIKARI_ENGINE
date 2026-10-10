#include "Editor/Commands/HIKARI_EditorOperationSearch.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::EDITOR {
namespace {

int MatchScore(const EditorOperationDefinition& definition, const std::string& keyword) {
    if (keyword.empty())
        return 1;
    const std::string id = TEXT::ToLowerAsciiCopy(definition.id);
    std::string haystack = id + " " + TEXT::ToLowerAsciiCopy(definition.summary) + " " + definition.workspace;
    for (const std::string& term : definition.searchTerms)
        haystack += " " + TEXT::ToLowerAsciiCopy(term);
    if (id == keyword)
        return 1000;
    int score = haystack.find(keyword) != std::string::npos ? 100 : 0;
    std::istringstream words(keyword);
    std::string word;
    while (words >> word) {
        if (id.find(word) != std::string::npos)
            score += 20;
        else if (haystack.find(word) != std::string::npos)
            score += 5;
    }
    return score;
}

} // namespace

nlohmann::json DescribeEditorOperationCompact(const EditorOperationDefinition& definition, bool includeParameters) {
    nlohmann::json item = DescribeEditorOperation(definition);
    item.erase("searchTerms");
    if (item.value("permission", std::string{}) == "unspecified")
        item.erase("permission");
    for (const char* flag : {"requiresDocumentSession", "requiresCleanDocument", "entryPoint", "approveEachTime"}) {
        if (!item.value(flag, false))
            item.erase(flag);
    }
    if (!includeParameters) {
        item.erase("parameters");
        return item;
    }
    for (nlohmann::json& parameter : item["parameters"]) {
        if (!parameter.value("required", false))
            parameter.erase("required");
        if (parameter.value("description", std::string{}).empty())
            parameter.erase("description");
        if (parameter.contains("schema") && parameter["schema"].empty())
            parameter.erase("schema");
    }
    return item;
}

nlohmann::json SearchEditorOperations(const EditorOperationRegistry& registry, EditorOperationExposureMask exposure,
                                      const EditorOperationSearchQuery& query) {
    const std::string keyword = TEXT::ToLowerAsciiCopy(query.keyword);
    std::map<std::string, int> workspaces;
    std::vector<std::pair<int, const EditorOperationDefinition*>> matches;
    for (const std::string& id : registry.RegisteredIds()) {
        const EditorOperationDefinition* definition = registry.Find(id);
        if (definition == nullptr || !HasEditorOperationExposure(definition->exposureMask, exposure))
            continue;
        ++workspaces[definition->workspace];
        if (!query.workspace.empty() && definition->workspace != query.workspace)
            continue;
        if (const int score = MatchScore(*definition, keyword); score > 0)
            matches.emplace_back(score, definition);
    }
    nlohmann::json data{{"workspaces", workspaces}, {"operations", nlohmann::json::array()}};
    if (query.workspace.empty() && keyword.empty())
        return data;
    std::sort(matches.begin(), matches.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first != rhs.first ? lhs.first > rhs.first : lhs.second->id < rhs.second->id;
    });
    const size_t limit = std::clamp<size_t>(query.limit, 1u, 32u);
    data["total"] = matches.size();
    for (size_t index = query.offset; index < matches.size() && index < query.offset + limit; ++index)
        data["operations"].push_back(DescribeEditorOperationCompact(*matches[index].second, query.includeParameters));
    if (query.offset + limit < matches.size())
        data["nextOffset"] = query.offset + limit;
    return data;
}

} // namespace HIKARI::EDITOR
