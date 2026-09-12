#pragma once

namespace HIKARI { class IScene; }

namespace HIKARI::RUNTIME {

    class IHostApplication {
    public:
        virtual ~IHostApplication() = default;

        virtual bool Initialize() = 0;
        virtual void Update(float deltaTime) = 0;
        virtual void Render() = 0;
        virtual void RenderHostUi() {}
        virtual const IScene* GetCurrentScene() const { return nullptr; }
        virtual void UpdateHostSessions() {}
        virtual void RequestHostStop() {}
        virtual bool IsWindowedHostSessionRunning() const {
            return false;
        }
        virtual bool IsHostTransitioning() const {
            return false;
        }
        virtual bool ShouldExitHost() const {
            return false;
        }
        virtual void Shutdown() = 0;
    };

} // namespace HIKARI::RUNTIME
