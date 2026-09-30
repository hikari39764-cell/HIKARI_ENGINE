#pragma once

namespace HIKARI {

class WorldView;

// Project-owned state for one Play or Standalone gameplay session.
// The module creates this object once and keeps it alive across World changes.
// Initial World construction and editor preview attachment happen before this
// session is created; gameplay side effects belong in these callbacks, not in
// component/system attach callbacks.
// Callbacks run serially on the World owner thread: game start, then the
// initial scene entry; each later scene exit happens before World detach and
// scene entry happens after the replacement World is attached. Game stop runs
// after the last World has detached and has no WorldView argument.
// A WorldView is a non-owning identity view: it may be retained, but does not
// extend its World lifetime or represent a scene snapshot. Prefer the view
// passed to each callback and resolve objects again after scene changes.
// WorldView::GetGameSession() returns a borrowed pointer that is valid on the
// owner thread for the active Play session, including between callbacks. It
// becomes invalid when that session is stopped and destroyed; it is null
// outside Play.
class IGameProjectSession {
public:
    virtual ~IGameProjectSession() = default;

    virtual void OnGameStarted(WorldView&) {}
    virtual void OnSceneExiting(WorldView&) {}
    virtual void OnSceneEntered(WorldView&) {}
    virtual void OnGameStopped() {}
};

} // namespace HIKARI
