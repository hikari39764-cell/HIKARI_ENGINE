#include "Runtime/Services/HIKARI_ActionTimeDilationService.h"

#include <algorithm>
#include <cmath>

#include "Core/HIKARI_TimeService.h"

namespace HIKARI::SERVICES {

namespace {
constexpr uint64_t kTokenPartMask = 0xFFFFFFFFull;
constexpr uint64_t kTokenEpochShift = 32u;
} // namespace

bool ActionTimeDilationService::IsValidRequest(const RequestDesc& request) noexcept {
    return request.sourceId != 0u && std::isfinite(request.scale) && request.scale >= 0.0f && request.scale <= 1.0f &&
           std::isfinite(request.durationUnscaledSeconds) && request.durationUnscaledSeconds >= 0.0f;
}

uint64_t ActionTimeDilationService::EncodeToken(uint32_t epoch, uint32_t value) noexcept {
    if (epoch == 0u || value == 0u) {
        return 0u;
    }
    return (static_cast<uint64_t>(epoch) << kTokenEpochShift) | static_cast<uint64_t>(value);
}

uint32_t ActionTimeDilationService::DecodeEpoch(uint64_t token) noexcept {
    return static_cast<uint32_t>(token >> kTokenEpochShift);
}

uint32_t ActionTimeDilationService::DecodeValue(uint64_t token) noexcept {
    return static_cast<uint32_t>(token & kTokenPartMask);
}

uint64_t ActionTimeDilationService::AllocateToken() noexcept {
    const uint64_t token = EncodeToken(epoch_, nextTokenValue_);
    ++nextTokenValue_;
    if (nextTokenValue_ == 0u) {
        nextTokenValue_ = 1u;
    }
    return token;
}

uint64_t ActionTimeDilationService::Request(World* owner, const RequestDesc& request) noexcept {
    if (owner == nullptr || !IsValidRequest(request)) {
        return 0u;
    }

    try {
        for (Entry& entry : requests_) {
            if (entry.owner != owner || entry.request.sourceId != request.sourceId) {
                continue;
            }

            // Upsert keeps the token stable, so a caller holding the
            // previous token can still release the refreshed request.
            entry.request = request;
            entry.remainingUnscaledSeconds = request.durationUnscaledSeconds;
            return entry.token;
        }

        Entry entry{};
        entry.owner = owner;
        entry.request = request;
        entry.token = AllocateToken();
        entry.remainingUnscaledSeconds = request.durationUnscaledSeconds;
        entry.insertionOrder = nextInsertionOrder_++;
        if (nextInsertionOrder_ == 0u) {
            nextInsertionOrder_ = 1u;
        }
        requests_.push_back(entry);
        return entry.token;
    } catch (...) {
        return 0u;
    }
}

uint64_t ActionTimeDilationService::Request(World* owner, uint64_t sourceId, int32_t priority, float scale,
                                            float durationUnscaledSeconds) noexcept {
    return Request(owner, RequestDesc{sourceId, priority, scale, durationUnscaledSeconds});
}

ActionTimeDilationService::Entry* ActionTimeDilationService::FindToken(uint64_t token) noexcept {
    if (!IsCurrentToken(token)) {
        return nullptr;
    }
    for (Entry& entry : requests_) {
        if (entry.token == token) {
            return &entry;
        }
    }
    return nullptr;
}

bool ActionTimeDilationService::IsCurrentToken(uint64_t token) const noexcept {
    return token != 0u && DecodeEpoch(token) == epoch_ && DecodeValue(token) != 0u;
}

bool ActionTimeDilationService::Release(World* owner, uint64_t token) noexcept {
    if (owner == nullptr) {
        return false;
    }

    Entry* entry = FindToken(token);
    if (entry == nullptr || entry->owner != owner) {
        return false;
    }

    requests_.erase(std::remove_if(requests_.begin(), requests_.end(),
                                   [token](const Entry& candidate) { return candidate.token == token; }),
                    requests_.end());
    return true;
}

bool ActionTimeDilationService::Release(uint64_t token) noexcept {
    Entry* entry = FindToken(token);
    if (entry == nullptr) {
        return false;
    }

    requests_.erase(std::remove_if(requests_.begin(), requests_.end(),
                                   [token](const Entry& candidate) { return candidate.token == token; }),
                    requests_.end());
    return true;
}

void ActionTimeDilationService::ReleaseOwner(World* owner) noexcept {
    if (owner == nullptr) {
        return;
    }
    requests_.erase(std::remove_if(requests_.begin(), requests_.end(),
                                   [owner](const Entry& entry) { return entry.owner == owner; }),
                    requests_.end());
}

const ActionTimeDilationService::Entry* ActionTimeDilationService::FindWinner(const World* owner) const noexcept {
    const Entry* winner = nullptr;
    for (const Entry& entry : requests_) {
        if (owner != nullptr && entry.owner != owner) {
            continue;
        }
        if (winner == nullptr || entry.request.priority > winner->request.priority ||
            (entry.request.priority == winner->request.priority &&
             (entry.request.sourceId < winner->request.sourceId ||
              (entry.request.sourceId == winner->request.sourceId && entry.insertionOrder < winner->insertionOrder)))) {
            winner = &entry;
        }
    }
    return winner;
}

void ActionTimeDilationService::ApplyPending() noexcept {
    const Entry* winner = FindWinner(nullptr);
    const float currentScale = TIME::GetGameTimeScale();
    if (winner == nullptr) {
        if (ownsTimeScale_) {
            TIME::SetGameTimeScale(baselineScale_);
            effectiveScale_ = baselineScale_;
            ownsTimeScale_ = false;
        } else {
            // With no action request, the existing editor/game time
            // authority remains untouched.
            effectiveScale_ = currentScale;
        }
        return;
    }

    if (!ownsTimeScale_) {
        baselineScale_ = currentScale;
        ownsTimeScale_ = true;
    } else if (std::isfinite(currentScale) && std::abs(currentScale - effectiveScale_) > 1.0e-6f) {
        // An explicit editor/game control changed the base while hit-stop
        // was active. Preserve that newer intent for restoration.
        baselineScale_ = currentScale;
    }
    effectiveScale_ = winner->request.scale;
    // This is intentionally the only frame-boundary write.  Requests and
    // releases made after this call wait for the next host frame.
    TIME::SetGameTimeScale(effectiveScale_);
}

void ActionTimeDilationService::AdvanceEntry(Entry& entry, float unscaledDt) noexcept {
    if (entry.remainingUnscaledSeconds <= 0.0f || unscaledDt <= 0.0f) {
        return;
    }
    entry.remainingUnscaledSeconds = (std::max)(0.0f, entry.remainingUnscaledSeconds - unscaledDt);
}

void ActionTimeDilationService::Advance(float unscaledDt) noexcept {
    if (!std::isfinite(unscaledDt) || unscaledDt < 0.0f) {
        return;
    }

    for (Entry& entry : requests_) {
        AdvanceEntry(entry, unscaledDt);
    }

    requests_.erase(std::remove_if(requests_.begin(), requests_.end(),
                                   [](const Entry& entry) {
                                       // Zero is also the explicit "until released" value;
                                       // finite requests are removed as soon as they consume
                                       // their positive duration.  The latter is distinguished
                                       // by the original request descriptor.
                                       return entry.request.durationUnscaledSeconds > 0.0f &&
                                              entry.remainingUnscaledSeconds <= 0.0f;
                                   }),
                    requests_.end());
}

void ActionTimeDilationService::Reset() noexcept {
    if (ownsTimeScale_) {
        TIME::SetGameTimeScale(baselineScale_);
    }
    requests_.clear();
    ++epoch_;
    if (epoch_ == 0u) {
        epoch_ = 1u;
    }
    nextTokenValue_ = 1u;
    nextInsertionOrder_ = 1u;
    ownsTimeScale_ = false;
    baselineScale_ = TIME::GetGameTimeScale();
    effectiveScale_ = baselineScale_;
}

float ActionTimeDilationService::GetEffectiveScale(const World* owner) const noexcept {
    (void)owner;
    return effectiveScale_;
}

} // namespace HIKARI::SERVICES
