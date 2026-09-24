#pragma once

#include <cstdint>
#include <unordered_map>

#include "HIKARI/Project/RuntimeActivityTypes.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {

// project が指定する runtime activity を world 単位で保持する。
// Active は既定値なので sparse map には保存しない。
class RuntimeObjectActivityService final {
public:
    bool Set(RuntimeObjectHandle object, GameProjectRuntimeActivity activity) noexcept {
        if (!object.IsValid() || !IsValid(activity)) {
            return false;
        }
        const uint64_t key = object.ToValue();
        if (activity == GameProjectRuntimeActivity::Active) {
            activities_.erase(key);
        } else {
            activities_.insert_or_assign(key, activity);
        }
        return true;
    }

    GameProjectRuntimeActivity Get(RuntimeObjectHandle object) const noexcept {
        if (!object.IsValid()) {
            return GameProjectRuntimeActivity::Active;
        }
        const auto found = activities_.find(object.ToValue());
        return found != activities_.end() ? found->second : GameProjectRuntimeActivity::Active;
    }

    bool IsActive(RuntimeObjectHandle object) const noexcept {
        return Get(object) == GameProjectRuntimeActivity::Active;
    }

    void Remove(RuntimeObjectHandle object) noexcept {
        if (object.IsValid()) {
            activities_.erase(object.ToValue());
        }
    }

    void Clear() noexcept { activities_.clear(); }

private:
    static bool IsValid(GameProjectRuntimeActivity activity) noexcept {
        switch (activity) {
        case GameProjectRuntimeActivity::Active:
        case GameProjectRuntimeActivity::FrozenVisible:
        case GameProjectRuntimeActivity::SuspendedHidden: return true;
        default: return false;
        }
    }

    std::unordered_map<uint64_t, GameProjectRuntimeActivity> activities_{};
};

} // namespace HIKARI
