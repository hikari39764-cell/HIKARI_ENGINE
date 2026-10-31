#include <array>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "HIKARI/Project/GameComponent.h"
#include "HIKARI/Project/WorldQuery.h"
#include "HIKARI/Project/WorldView.h"
#include "HIKARI/Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Tools/Tests/Internal/HIKARI_ContractAssert.h"

namespace {

constexpr std::string_view kProjectAName = "Test.Query.ProjectA";
constexpr std::string_view kProjectBName = "Test.Query.ProjectB";
constexpr HIKARI::ComponentTypeId kProjectAId = HIKARI::MakeComponentTypeId(kProjectAName);
constexpr HIKARI::ComponentTypeId kProjectBId = HIKARI::MakeComponentTypeId(kProjectBName);

class ProjectA final : public HIKARI::IGameComponent {};
class ProjectB final : public HIKARI::IGameComponent {};

class NativeOnly final : public HIKARI::IComponent {
public:
    std::string_view GetTypeName() const override { return "Test.Query.NativeOnly"; }
};

template <class Component>
void DestroyProjectComponent(HIKARI::IGameComponent* component) noexcept {
    delete static_cast<Component*>(component);
}

template <class Component>
void AddProjectComponent(HIKARI::GameObject& object, std::string_view typeName) {
    auto adapter = std::make_unique<HIKARI::GameProjectComponentAdapter>(std::string(typeName), new Component(),
                                                                         &DestroyProjectComponent<Component>);
    assert(object.AddComponentInstance(std::move(adapter)) != nullptr);
}

HIKARI::GameProjectWorldQueryDesc MakeQueryDesc(const HIKARI::GameProjectWorldQueryTerm* terms, uint32_t termCount,
                                                HIKARI::GameProjectWorldQueryCardinality cardinality,
                                                HIKARI::GameProjectWorldQueryAnchor anchor) noexcept {
    HIKARI::GameProjectWorldQueryDesc desc{};
    desc.terms = terms;
    desc.termCount = termCount;
    desc.cardinality = cardinality;
    desc.anchor = anchor;
    return desc;
}

struct CollectContext {
    HIKARI::WorldView world{};
    std::vector<HIKARI::GameProjectObjectHandle> objects{};
    HIKARI::GameProjectWorldResult queuedSpawn{};
    bool queueOneSpawn = false;
};

HIKARI::GameProjectWorldQueryVisitResult CollectRows(void* opaque,
                                                     const HIKARI::GameProjectWorldQueryRow* row) noexcept {
    auto& context = *static_cast<CollectContext*>(opaque);
    if (row == nullptr || row->components == nullptr || row->componentCount == 0u || !row->object.IsValid()) {
        return HIKARI::GameProjectWorldQueryVisitResult::Error;
    }
    context.objects.push_back(row->object);
    if (!context.queueOneSpawn || context.queuedSpawn.Accepted()) {
        return HIKARI::GameProjectWorldQueryVisitResult::Continue;
    }
    HIKARI::GameProjectSpawnObjectDesc spawn{};
    std::snprintf(spawn.name, HIKARI::kGameProjectObjectNameCapacity, "%s", "SpawnedAtQueryEnd");
    context.queuedSpawn = context.world.SpawnObject(spawn);
    return HIKARI::GameProjectWorldQueryVisitResult::Stop;
}

void TestPublicWorldQueries() {
    HIKARI::World world{};
    const HIKARI::WorldView view = HIKARI::GameProjectSdkAccess::MakeWorldView(world);
    HIKARI::GameObject* first = world.CreateObject("QueryFirst");
    HIKARI::GameObject* second = world.CreateObject("QuerySecond");
    HIKARI::GameObject* third = world.CreateObject("QueryThird");
    assert(first != nullptr && second != nullptr && third != nullptr);
    AddProjectComponent<ProjectA>(*first, kProjectAName);
    AddProjectComponent<ProjectA>(*second, kProjectAName);
    AddProjectComponent<ProjectA>(*third, kProjectAName);
    AddProjectComponent<ProjectA>(*first, kProjectAName);
    AddProjectComponent<ProjectB>(*third, kProjectBName);
    AddProjectComponent<ProjectB>(*first, kProjectBName);

    const HIKARI::GameProjectWorldQueryTerm termA{
        HIKARI::kGameProjectWorldQueryTermV1Size, HIKARI::kGameProjectWorldQueryVersion, kProjectAId, {0u, 0u}};
    const auto allA = MakeQueryDesc(&termA, 1u, HIKARI::GameProjectWorldQueryCardinality::AllEntries,
                                    HIKARI::GameProjectWorldQueryAnchor::FirstRequiredTerm);
    CollectContext allContext{view};
    const auto allResult = view.QueryComponents(allA, &allContext, &CollectRows);
    assert(allResult.Succeeded() && allResult.rowsVisited == 4u);
    assert((allContext.objects == std::vector<HIKARI::GameProjectObjectHandle>{
                                      HIKARI::GameProjectSdkAccess::MakeObjectView(*first).GetHandle(),
                                      HIKARI::GameProjectSdkAccess::MakeObjectView(*second).GetHandle(),
                                      HIKARI::GameProjectSdkAccess::MakeObjectView(*third).GetHandle(),
                                      HIKARI::GameProjectSdkAccess::MakeObjectView(*first).GetHandle()}));

    const HIKARI::ComponentTypeId typeIds[]{kProjectAId, kProjectBId};
    std::vector<HIKARI::GameProjectObjectHandle> joined{};
    const auto joinedResult =
        view.ForEachComponents<ProjectA, ProjectB>(typeIds, HIKARI::GameProjectWorldQueryAnchor::SmallestUniqueBucket,
                                                   [&](HIKARI::ObjectView object, ProjectA&, ProjectB&) {
                                                       joined.push_back(object.GetHandle());
                                                       return true;
                                                   });
    assert(joinedResult.Succeeded() && joinedResult.rowsVisited == 2u);
    assert((joined == std::vector<HIKARI::GameProjectObjectHandle>{
                          HIKARI::GameProjectSdkAccess::MakeObjectView(*third).GetHandle(),
                          HIKARI::GameProjectSdkAccess::MakeObjectView(*first).GetHandle()}));

    CollectContext queueContext{view};
    queueContext.queueOneSpawn = true;
    const auto stopped = view.QueryComponents(allA, &queueContext, &CollectRows);
    assert(stopped.code == HIKARI::GameProjectWorldQueryResultCode::Stopped);
    assert(stopped.rowsVisited == 1u);
    assert(queueContext.queuedSpawn.IsQueued());
    assert(view.FindObject(queueContext.queuedSpawn.object).IsValid());

    const auto callbackError = view.ForEachComponents<ProjectA, ProjectB>(
        typeIds, HIKARI::GameProjectWorldQueryAnchor::FirstRequiredTerm,
        [](HIKARI::ObjectView, ProjectA&, ProjectB&) -> HIKARI::GameProjectWorldQueryVisitResult { throw 1; });
    assert(callbackError.code == HIKARI::GameProjectWorldQueryResultCode::CallbackError);

    const HIKARI::GameProjectWorldQueryTerm duplicates[]{termA, termA};
    const auto duplicateDesc = MakeQueryDesc(duplicates, 2u, HIKARI::GameProjectWorldQueryCardinality::FirstPerObject,
                                             HIKARI::GameProjectWorldQueryAnchor::SmallestUniqueBucket);
    CollectContext invalidContext{view};
    const auto duplicateResult = view.QueryComponents(duplicateDesc, &invalidContext, &CollectRows);
    assert(duplicateResult.code == HIKARI::GameProjectWorldQueryResultCode::InvalidTerm);
    assert(duplicateResult.diagnosticTermIndex == 1u);
    assert(invalidContext.objects.empty());

    auto unsupportedDesc = allA;
    unsupportedDesc.version = HIKARI::kGameProjectWorldQueryVersion + 1u;
    assert(view.QueryComponents(unsupportedDesc, &invalidContext, &CollectRows).code ==
           HIKARI::GameProjectWorldQueryResultCode::Unsupported);
    auto reservedDesc = allA;
    reservedDesc.reserved[0] = 1u;
    assert(view.QueryComponents(reservedDesc, &invalidContext, &CollectRows).code ==
           HIKARI::GameProjectWorldQueryResultCode::InvalidRequest);
    auto invalidTerm = termA;
    invalidTerm.reserved[0] = 1u;
    const auto invalidTermDesc = MakeQueryDesc(&invalidTerm, 1u, HIKARI::GameProjectWorldQueryCardinality::AllEntries,
                                               HIKARI::GameProjectWorldQueryAnchor::FirstRequiredTerm);
    assert(view.QueryComponents(invalidTermDesc, &invalidContext, &CollectRows).code ==
           HIKARI::GameProjectWorldQueryResultCode::InvalidTerm);

    std::array<HIKARI::GameProjectWorldQueryTerm, 9u> tooMany{};
    for (auto& term : tooMany) {
        term.typeId = kProjectAId;
    }
    const auto tooManyDesc = MakeQueryDesc(tooMany.data(), static_cast<uint32_t>(tooMany.size()),
                                           HIKARI::GameProjectWorldQueryCardinality::FirstPerObject,
                                           HIKARI::GameProjectWorldQueryAnchor::SmallestUniqueBucket);
    const auto tooManyResult = view.QueryComponents(tooManyDesc, &invalidContext, &CollectRows);
    assert(tooManyResult.code == HIKARI::GameProjectWorldQueryResultCode::TooManyTerms);

    HIKARI::GameObject* wide = world.CreateObject("EightTermObject");
    assert(wide != nullptr);
    const std::array<std::string_view, 8u> wideNames{"Test.Query.Wide0", "Test.Query.Wide1", "Test.Query.Wide2",
                                                     "Test.Query.Wide3", "Test.Query.Wide4", "Test.Query.Wide5",
                                                     "Test.Query.Wide6", "Test.Query.Wide7"};
    std::array<HIKARI::GameProjectWorldQueryTerm, 8u> wideTerms{};
    for (size_t index = 0u; index < wideNames.size(); ++index) {
        AddProjectComponent<ProjectA>(*wide, wideNames[index]);
        wideTerms[index].typeId = HIKARI::MakeComponentTypeId(wideNames[index]);
    }
    const auto wideDesc = MakeQueryDesc(wideTerms.data(), static_cast<uint32_t>(wideTerms.size()),
                                        HIKARI::GameProjectWorldQueryCardinality::FirstPerObject,
                                        HIKARI::GameProjectWorldQueryAnchor::SmallestUniqueBucket);
    CollectContext wideContext{view};
    const auto wideResult = view.QueryComponents(wideDesc, &wideContext, &CollectRows);
    assert(wideResult.Succeeded() && wideResult.rowsVisited == 1u);

    HIKARI::GameObject* nativeObject = world.CreateObject("NativeOnly");
    assert(nativeObject != nullptr);
    assert(nativeObject->AddComponent<NativeOnly>() != nullptr);
    HIKARI::GameProjectWorldQueryTerm nativeTerm{};
    nativeTerm.typeId = HIKARI::MakeComponentTypeId("Test.Query.NativeOnly");
    const auto nativeDesc = MakeQueryDesc(&nativeTerm, 1u, HIKARI::GameProjectWorldQueryCardinality::AllEntries,
                                          HIKARI::GameProjectWorldQueryAnchor::FirstRequiredTerm);
    const auto unsupported = view.QueryComponents(nativeDesc, &invalidContext, &CollectRows);
    assert(unsupported.code == HIKARI::GameProjectWorldQueryResultCode::Unsupported);
    assert(unsupported.diagnosticTermIndex == 0u);
    assert(unsupported.rowsVisited == 0u);
    assert(invalidContext.objects.empty());

    HIKARI::GameProjectWorldQueryResult wrongThread{};
    std::thread worker([&]() { wrongThread = view.QueryComponents(allA, &invalidContext, &CollectRows); });
    worker.join();
    assert(wrongThread.code == HIKARI::GameProjectWorldQueryResultCode::WrongThread);
}

void TestExpiredWorldQuery() {
    HIKARI::WorldView expired{};
    HIKARI::GameProjectWorldQueryTerm term{};
    term.typeId = kProjectAId;
    const auto desc = MakeQueryDesc(&term, 1u, HIKARI::GameProjectWorldQueryCardinality::AllEntries,
                                    HIKARI::GameProjectWorldQueryAnchor::FirstRequiredTerm);
    {
        HIKARI::World world{};
        expired = HIKARI::GameProjectSdkAccess::MakeWorldView(world);
    }
    CollectContext context{expired};
    const auto result = expired.QueryComponents(desc, &context, &CollectRows);
    assert(result.code == HIKARI::GameProjectWorldQueryResultCode::InvalidWorld);
}

} // namespace

int main() {
    TestPublicWorldQueries();
    TestExpiredWorldQuery();
    return 0;
}
