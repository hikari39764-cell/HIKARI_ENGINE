#include "HIKARI/Project/WorldView.h"

#include <array>
#include <cstddef>
#include <iterator>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    bool GameProjectSdkAccess::VisitIndexedComponents(
        World& world,
        std::span<const ComponentTypeId> typeIds,
        WorldComponentQueryCardinality cardinality,
        WorldComponentQueryAnchor anchor,
        void* context,
        IndexedComponentVisitorFn visitor) noexcept {
        const WorldComponentQueryPlan plan{
            typeIds,
            WorldComponentQueryMatch::All,
            cardinality,
            anchor,
            WorldComponentQueryOrder::AnchorAttachment
        };
        return world.VisitIndexedComponents(
            plan,
            context,
            visitor);
    }

    bool GameProjectSdkAccess::IsWorldClosing(
        const World& world) noexcept {
        return world.worldClosing_;
    }

    GameProjectWorldQueryResult WorldView::QueryComponents(
        const GameProjectWorldQueryDesc& desc,
        void* context,
        GameProjectWorldQueryCallback callback) const noexcept {
        GameProjectWorldQueryResult result{};
        RuntimeWorldAccessResult access = RuntimeWorldAccessResult::Success;
        World* world = GameProjectSdkAccess::GetWorld(*this, &access);
        if (world == nullptr) {
            result.code = access == RuntimeWorldAccessResult::WrongThread
                ? GameProjectWorldQueryResultCode::WrongThread
                : GameProjectWorldQueryResultCode::InvalidWorld;
            return result;
        }
        if (GameProjectSdkAccess::IsWorldClosing(*world)) {
            result.code = GameProjectWorldQueryResultCode::WorldClosing;
            return result;
        }
        const auto hasReserved = [](const uint32_t* values,
            std::size_t count) noexcept {
            for (std::size_t index = 0u; index < count; ++index) {
                if (values[index] != 0u) {
                    return true;
                }
            }
            return false;
        };
        if (desc.version != kGameProjectWorldQueryVersion) {
            result.code = GameProjectWorldQueryResultCode::Unsupported;
            return result;
        }
        if (callback == nullptr ||
            desc.structSize < kGameProjectWorldQueryDescV1Size ||
            desc.terms == nullptr || desc.termCount == 0u ||
            desc.match != GameProjectWorldQueryMatch::AllRequired ||
            desc.cardinality >= GameProjectWorldQueryCardinality::Count ||
            desc.anchor >= GameProjectWorldQueryAnchor::Count ||
            desc.order != GameProjectWorldQueryOrder::AnchorAttachment ||
            (desc.termCount > 1u &&
             desc.cardinality !=
                GameProjectWorldQueryCardinality::FirstPerObject) ||
            hasReserved(desc.reserved, std::size(desc.reserved))) {
            result.code = GameProjectWorldQueryResultCode::InvalidRequest;
            return result;
        }
        if (desc.termCount > kGameProjectWorldQueryMaximumTermCount) {
            result.code = GameProjectWorldQueryResultCode::TooManyTerms;
            return result;
        }

        std::array<ComponentTypeId,
            kGameProjectWorldQueryMaximumTermCount> typeIds{};
        for (uint32_t termIndex = 0u;
            termIndex < desc.termCount;
            ++termIndex) {
            const GameProjectWorldQueryTerm& term = desc.terms[termIndex];
            if (term.version != kGameProjectWorldQueryVersion) {
                result.code = GameProjectWorldQueryResultCode::Unsupported;
                result.diagnosticTermIndex = termIndex;
                return result;
            }
            if (term.structSize != kGameProjectWorldQueryTermV1Size ||
                !term.typeId.IsValid() ||
                hasReserved(term.reserved, std::size(term.reserved))) {
                result.code = GameProjectWorldQueryResultCode::InvalidTerm;
                result.diagnosticTermIndex = termIndex;
                return result;
            }
            for (uint32_t prior = 0u; prior < termIndex; ++prior) {
                if (typeIds[prior] == term.typeId) {
                    result.code =
                        GameProjectWorldQueryResultCode::InvalidTerm;
                    result.diagnosticTermIndex = termIndex;
                    return result;
                }
            }
            typeIds[termIndex] = term.typeId;
        }

        const WorldComponentQueryCardinality runtimeCardinality =
            desc.cardinality == GameProjectWorldQueryCardinality::AllEntries
                ? WorldComponentQueryCardinality::AllEntries
                : WorldComponentQueryCardinality::FirstPerObject;
        const WorldComponentQueryAnchor runtimeAnchor =
            desc.anchor == GameProjectWorldQueryAnchor::FirstRequiredTerm
                ? WorldComponentQueryAnchor::FirstRequiredType
                : WorldComponentQueryAnchor::SmallestUniqueBucket;
        struct VisitContext {
            World* world = nullptr;
            void* clientContext = nullptr;
            GameProjectWorldQueryCallback clientCallback = nullptr;
            GameProjectWorldQueryResult* result = nullptr;
            uint32_t expectedComponentCount = 0u;
            bool unsupported = false;
            bool internalFailure = false;
        } visitContext{
            world,
            context,
            callback,
            &result,
            desc.termCount,
            false,
            false
        };

        bool completed = false;
        try {
            completed = GameProjectSdkAccess::VisitIndexedComponents(
                *world,
                std::span<const ComponentTypeId>(
                    typeIds.data(),
                    desc.termCount),
                runtimeCardinality,
                runtimeAnchor,
                &visitContext,
                [](void* opaque,
                    RuntimeObjectHandle objectHandle,
                    IComponent* const* components,
                    uint32_t componentCount) noexcept {
                    auto& visit = *static_cast<VisitContext*>(opaque);
                    if (components == nullptr ||
                        componentCount != visit.expectedComponentCount) {
                        visit.internalFailure = true;
                        return false;
                    }
                    std::array<IGameComponent*,
                        kGameProjectWorldQueryMaximumTermCount>
                        projectComponents{};
                    for (uint32_t termIndex = 0u;
                        termIndex < componentCount;
                        ++termIndex) {
                        auto* adapter = dynamic_cast<
                            GameProjectComponentAdapter*>(
                                components[termIndex]);
                        IGameComponent* gameComponent = adapter != nullptr
                            ? adapter->GetGameComponent()
                            : nullptr;
                        if (gameComponent == nullptr) {
                            visit.unsupported = true;
                            visit.result->diagnosticTermIndex = termIndex;
                            return false;
                        }
                        projectComponents[termIndex] = gameComponent;
                    }
                    GameObject* object = visit.world->FindObject(objectHandle);
                    if (object == nullptr) {
                        visit.internalFailure = true;
                        return false;
                    }
                    const GameProjectWorldQueryRow row{
                        kGameProjectWorldQueryRowV1Size,
                        kGameProjectWorldQueryVersion,
                        GameProjectSdkAccess::MakeObjectView(
                            *object).GetHandle(),
                        projectComponents.data(),
                        componentCount,
                        { 0u, 0u, 0u }
                    };
                    ++visit.result->rowsVisited;
                    const GameProjectWorldQueryVisitResult callbackResult =
                        visit.clientCallback(visit.clientContext, &row);
                    visit.result->callbackResult = callbackResult;
                    switch (callbackResult) {
                    case GameProjectWorldQueryVisitResult::Continue:
                        return true;
                    case GameProjectWorldQueryVisitResult::Stop:
                    case GameProjectWorldQueryVisitResult::Error:
                        return false;
                    case GameProjectWorldQueryVisitResult::Count:
                        visit.result->callbackResult =
                            GameProjectWorldQueryVisitResult::Error;
                        return false;
                    }
                    return false;
                });
            (void)world->FlushQueuedCommands(WorldCommandPhase::QueryEnd);
        }
        catch (...) {
            visitContext.internalFailure = true;
        }

        if (visitContext.unsupported) {
            result.code = GameProjectWorldQueryResultCode::Unsupported;
        }
        else if (visitContext.internalFailure) {
            result.code = GameProjectWorldQueryResultCode::InternalError;
        }
        else if (result.callbackResult ==
                GameProjectWorldQueryVisitResult::Stop) {
            result.code = GameProjectWorldQueryResultCode::Stopped;
        }
        else if (result.callbackResult ==
                GameProjectWorldQueryVisitResult::Error) {
            result.code = GameProjectWorldQueryResultCode::CallbackError;
        }
        else {
            result.code = completed
                ? GameProjectWorldQueryResultCode::Success
                : GameProjectWorldQueryResultCode::InternalError;
        }
        return result;
    }

    bool WorldView::VisitComponents(
        ComponentTypeId typeId,
        void* context,
        VisitorFn visitor) const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        if (world == nullptr ||
            !typeId.IsValid() ||
            visitor == nullptr) {
            return false;
        }
        struct VisitContext {
            World* world = nullptr;
            void* clientContext = nullptr;
            VisitorFn clientVisitor = nullptr;
        } visitContext{ world, context, visitor };
        const std::array typeIds{ typeId };
        const bool completed =
            GameProjectSdkAccess::VisitIndexedComponents(
                *world,
                typeIds,
                WorldComponentQueryCardinality::AllEntries,
                WorldComponentQueryAnchor::FirstRequiredType,
                &visitContext,
                [](void* opaque,
                    RuntimeObjectHandle objectHandle,
                    IComponent* const* components,
                    uint32_t componentCount) noexcept {
                    auto& visit = *static_cast<VisitContext*>(opaque);
                    if (components == nullptr || componentCount != 1u) {
                        return false;
                    }
                    GameObject* object =
                        visit.world->FindObject(objectHandle);
                    auto* adapter = dynamic_cast<
                        GameProjectComponentAdapter*>(
                            components[0]);
                    return object != nullptr &&
                        adapter != nullptr &&
                        visit.clientVisitor(
                            visit.clientContext,
                            GameProjectSdkAccess::MakeObjectView(
                                *object),
                            adapter->GetGameComponent());
                });
        (void)world->FlushQueuedCommands(WorldCommandPhase::QueryEnd);
        return completed;
    }

} // namespace HIKARI
