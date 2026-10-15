#pragma once

#include <string_view>

#include "HIKARI/Project/RuntimeRegistration.h"

namespace HIKARI {

class ComponentRegistry;
class ComponentSystemPolicy;
class SystemTypeRegistry;
class WorldMassSchemaRegistry;

// 公開の RuntimeFeatureContext が指す登録表（いずれも Scene が所有する）を作り、取り出す。内蔵 Feature と
// Runtime の SDK 実装（GameProject の登録）が共有する。登録表の所有者である Scene に置き、内蔵 Feature が
// Runtime の内部ヘッダへ依存しないようにする。
struct RuntimeFeatureContextAccess {
    static RuntimeFeatureContext Create(ComponentRegistry& componentRegistry,
                                        WorldMassSchemaRegistry& massSchemaRegistry,
                                        SystemTypeRegistry& systemTypeRegistry,
                                        ComponentSystemPolicy& componentSystemPolicy) noexcept {
        return RuntimeFeatureContext(&componentRegistry, &massSchemaRegistry, &systemTypeRegistry,
                                     &componentSystemPolicy);
    }

    static ComponentRegistry& Components(RuntimeFeatureContext& context) noexcept {
        return *static_cast<ComponentRegistry*>(context.componentRegistry_);
    }
    static WorldMassSchemaRegistry& MassSchemas(RuntimeFeatureContext& context) noexcept {
        return *static_cast<WorldMassSchemaRegistry*>(context.massSchemaRegistry_);
    }
    static SystemTypeRegistry& Systems(RuntimeFeatureContext& context) noexcept {
        return *static_cast<SystemTypeRegistry*>(context.systemTypeRegistry_);
    }
    static ComponentSystemPolicy& Policy(RuntimeFeatureContext& context) noexcept {
        return *static_cast<ComponentSystemPolicy*>(context.componentSystemPolicy_);
    }
    // 登録取引が Register 呼び出しの間だけ設定する。記述子の所属 Feature になる。
    static void SetFeatureId(RuntimeFeatureContext& context, std::string_view featureId) noexcept {
        context.featureId_ = featureId;
    }
};

} // namespace HIKARI
