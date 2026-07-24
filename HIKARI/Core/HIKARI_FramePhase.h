#pragma once

namespace HIKARI {

    enum class EngineFramePhase {
        None,
        Update,
        LateUpdate,
        PreRenderSync,
        Render,
        PostRender,
        EditorUpdate,
        ToolJob,
    };

    inline const char* ToString(EngineFramePhase phase) {
        switch (phase) {
        case EngineFramePhase::Update:
            return "Update";
        case EngineFramePhase::LateUpdate:
            return "LateUpdate";
        case EngineFramePhase::PreRenderSync:
            return "PreRenderSync";
        case EngineFramePhase::Render:
            return "Render";
        case EngineFramePhase::PostRender:
            return "PostRender";
        case EngineFramePhase::EditorUpdate:
            return "EditorUpdate";
        case EngineFramePhase::ToolJob:
            return "ToolJob";
        case EngineFramePhase::None:
        default:
            return "None";
        }
    }

}
