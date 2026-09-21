#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Editor/Views/Director/HIKARI_DirectorViewInternal.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/HIKARI_TimeService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include <algorithm>
#include <cmath>
#include "imgui.h"

namespace HIKARI::EDITOR {
    Camera3D CinematicsWorkspaceController::ResolveShotCamera(DocumentSceneBase& scene,
        CinematicSequence& sequence,const SEQUENCER::CameraCutClip& shot,float localTime,bool includeMotion) const {
        if(!shot.camera.owned) {
            SceneObjectId id;
            Camera3D camera=scene.GetCamera();
            if(previewBindings_.Resolve(sequence.bindings,shot.cameraBindingId,id)) scene.TryResolveCameraObjectView(id,16.0f/9,camera);
            return camera;
        }
        const auto object=[&](SEQUENCER::SequenceBindingId binding) {
            SceneObjectId id;
            return previewBindings_.Resolve(sequence.bindings,binding,id)?scene.GetWorld().FindObject(id):nullptr;
        };
        const auto* anchor=object(shot.camera.anchor);
        const auto* target=object(shot.camera.lookAt);
        const auto matrix=anchor?anchor->GetTransform().GetWorldMatrix():MATH::Mat4::Identity();
        const auto position=target?DIRECTOR_VIEW::ExtractPosition(target->GetTransform().GetWorldMatrix()):MATH::Vec3{};
        if(includeMotion) return SEQUENCER::EvaluateShotCamera(shot.camera,localTime,16.0f/9,anchor?&matrix:nullptr,target?&position:nullptr);
        auto base=shot.camera;base.motion.clear();
        return SEQUENCER::EvaluateShotCamera(base,localTime,16.0f/9,anchor?&matrix:nullptr,target?&position:nullptr);
    }

    bool CinematicsWorkspaceController::DrawStage(DocumentSceneBase& scene,CinematicSequence& sequence,
        EditorContext& context,EditorWorkspaceHost& host,bool enabled) {
        auto& view=host.GetCinematicsDirectorView();
        if(!spaceView_) {RENDER3D::EDITORVIEW::ClearRequest(view.renderViewId);return false;}
        bool changed=false;
        if(ImGui::Begin(TEXT::LocalizeWindow("cine.stage","Cinematics/Space").c_str(),&spaceView_,ImGuiWindowFlags_NoScrollbar)) {
            const auto frameAll=[&] {
                std::vector<MATH::Vec3> positions;
                for(const auto& shot:sequence.cameraCutTrack.clips) {
                    for (int sample = 0; sample <= 12; ++sample) {
                        const auto camera=ResolveShotCamera(scene,sequence,shot,shot.durationSeconds * sample / 12.0f);
                        positions.push_back(camera.GetPosition());positions.push_back(camera.GetTarget());
                    }
                }
                for(const auto& object:scene.GetWorld().GetObjects()) if(object && object->GetComponent<CameraComponent>())
                    positions.push_back(DIRECTOR_VIEW::ExtractPosition(object->GetTransform().GetWorldMatrix()));
                if(positions.empty()) positions.push_back(scene.GetCamera().GetTarget());
                auto low=positions.front(),high=low;
                for(const auto& point:positions) {
                    low={(std::min)(low.x,point.x),(std::min)(low.y,point.y),(std::min)(low.z,point.z)};
                    high={(std::max)(high.x,point.x),(std::max)(high.y,point.y),(std::max)(high.z,point.z)};
                }
                const auto center=(low+high)*0.5f;
                const float radius=(std::max)(3.0f,MATH::Length(high-low)*0.5f);
                Camera3D camera=scene.GetCamera();
                const auto available=ImGui::GetContentRegionAvail();
                const float aspect=(std::max)(0.1f,available.x/(std::max)(1.0f,available.y-70));
                const float halfFov=std::atan(std::tan(camera.GetFovYRad()*0.5f)*(std::min)(1.0f,aspect));
                const float distance=radius*1.15f/(std::max)(0.05f,std::sin(halfFov));
                camera.SetLookAt(center+MATH::Normalize(MATH::Vec3{1,0.65f,-1})*distance,center,{0,1,0});
                directorViewPanel_.SetFreeCamera(camera);
            };
            if(!stageInitialized_ && view.extent.width && view.extent.height) {frameAll();stageInitialized_=true;}
            const auto icon=[&](EditorGlyph glyph,const char* key) {
                return IconButton(glyph,key,EditorButtonTone::Quiet,{26,26},TEXT::Localize(key).c_str());
            };
            if(icon(EditorGlyph::Focus,"cine.frameAllCameras") ||
                ConsumeEditorShortcut(EditorShortcutScope::Viewport, ImGuiKey_F,
                    ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))) frameAll();
            NextToolbarItem();
            if(IconToggleButton(EditorGlyph::Animation,"shotPaths",showShotPaths_,{26,26},TEXT::Localize("cine.showPaths").c_str())) showShotPaths_=!showShotPaths_;
            NextToolbarItem();ImGui::BeginDisabled(!enabled);
            if(icon(EditorGlyph::Add,"cine.shotFromView")) {CreateShot(sequence,directorViewPanel_.GetViewCamera());changed=true;}
            NextToolbarItem();
            if(icon(EditorGlyph::Keyframe,"cine.captureView")) {CapturePose(scene,sequence,directorViewPanel_.GetViewCamera());changed=true;}
            NextToolbarItem();
            if(IconToggleButton(EditorGlyph::Camera,"editCameraPosition",!stageEditTarget_,{26,26},TEXT::Localize("cine.editCamera").c_str())) stageEditTarget_=false;
            NextToolbarItem();
            if(IconToggleButton(EditorGlyph::Focus,"editCameraTarget",stageEditTarget_,{26,26},TEXT::Localize("cine.editTarget").c_str())) {stageEditTarget_=true;stageRotate_=false;}
            NextToolbarItem();
            const auto* selected = SelectedShot(sequence);
            const bool rotationLocked = selected && selected->camera.lookAt.IsValid();
            if (rotationLocked) stageRotate_ = false;
            ImGui::BeginDisabled(rotationLocked);
            if(IconToggleButton(EditorGlyph::Rotate,"rotateCamera",stageRotate_,{26,26},TEXT::Localize("cine.rotateCamera").c_str())) {stageRotate_=!stageRotate_;stageEditTarget_=false;}
            ImGui::EndDisabled();
            if (rotationLocked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", TEXT::LocalizeCStr("cine.boundRotation"));
            ImGui::EndDisabled();
            directorViewPanel_.SetTargetCamera({});
            const auto result=directorViewPanel_.DrawContents(scene,view,{},context.transformGizmo,
                TIME::GetFrameContext().unscaledDt,!enabled,false);
            changed|=DrawShotOverlays(scene,sequence,result.viewport,enabled);
        } else RENDER3D::EDITORVIEW::ClearRequest(view.renderViewId);
        ImGui::End();
        return changed;
    }

    bool CinematicsWorkspaceController::DrawShotOverlays(DocumentSceneBase& scene,CinematicSequence& sequence,
        const EditorViewportRect& viewport,bool enabled) {
        if(viewport.width<1 || viewport.height<1) return false;
        const auto& observer=directorViewPanel_.GetViewCamera();
        const ImVec2 origin{viewport.x,viewport.y},size{viewport.width,viewport.height};
        auto* draw=ImGui::GetWindowDrawList();
        draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
        const auto project=[&](MATH::Vec3 point,ImVec2& screen) {return DIRECTOR_VIEW::ProjectWorldToViewport(observer,point,origin,size,screen);};
        const auto line=[&](MATH::Vec3 a,MATH::Vec3 b,ImU32 color) {ImVec2 pa,pb;if(project(a,pa)&&project(b,pb)) draw->AddLine(pa,pb,color,1.4f);};
        uint64_t clicked=0;
        bool changed=false,interacting=false;
        if(auto* shot=SelectedShot(sequence);shot && shot->camera.owned && enabled) {
            const float local=std::clamp(cursor_.GetTimeSeconds()-shot->startTimeSeconds,0.0f,shot->durationSeconds);
            auto camera=ResolveShotCamera(scene,sequence,*shot,local,false);
            const auto basis=MATH::BuildViewBasisRH(camera.GetTarget()-camera.GetPosition(),camera.GetUp());
            auto matrix=MATH::Mat4::Identity();
            const MATH::Vec3 axes[]{basis.right*-1,basis.up,basis.forward};
            for(int i=0;i<3;++i) {matrix.m[i][0]=axes[i].x;matrix.m[i][1]=axes[i].y;matrix.m[i][2]=axes[i].z;}
            MATH::Vec3 translation,scale;MATH::Quat rotation;
            MATH::DecomposeTRS(matrix,translation,rotation,scale);
            TransformData transform;
            transform.position=stageEditTarget_?camera.GetTarget():camera.GetPosition();
            transform.rotationEulerDeg=MATH::EulerXYZDegreesFromQuat(rotation);
            transform.scale={1,1,1};
            EditorTransformGizmoState state;
            state.operation=stageRotate_ && !shot->camera.lookAt.IsValid()?EditorTransformGizmoOperation::Rotate:EditorTransformGizmoOperation::Translate;
            state.snapEnabled=ImGui::GetIO().KeyCtrl;
            const auto edit=stageGizmo_.DrawTransform(transform,observer,state,viewport);
            interacting=edit.interacting;
            if(edit.changed) {
                if(state.operation==EditorTransformGizmoOperation::Rotate) {
                    const auto rotationMatrix=MATH::Mat4::Rotate(edit.rotation);
                    const MATH::Vec3 forward{rotationMatrix.m[2][0],rotationMatrix.m[2][1],rotationMatrix.m[2][2]};
                    const MATH::Vec3 up{rotationMatrix.m[1][0],rotationMatrix.m[1][1],rotationMatrix.m[1][2]};
                    camera.SetLookAt(camera.GetPosition(),camera.GetPosition()+forward*MATH::Length(camera.GetTarget()-camera.GetPosition()),up);
                } else if(stageEditTarget_) {
                    if(shot->camera.lookAt.IsValid()) shot->camera.targetOffset=shot->camera.targetOffset+edit.transform.position-camera.GetTarget();
                    camera.SetLookAt(camera.GetPosition(),edit.transform.position,camera.GetUp());
                } else {
                    const auto delta=edit.transform.position-camera.GetPosition();
                    camera.SetLookAt(edit.transform.position,shot->camera.lookAt.IsValid()?camera.GetTarget():camera.GetTarget()+delta,camera.GetUp());
                }
                SceneObjectId anchorId;
                const auto* anchor=previewBindings_.Resolve(sequence.bindings,shot->camera.anchor,anchorId)?scene.GetWorld().FindObject(anchorId):nullptr;
                const auto anchorMatrix=anchor?anchor->GetTransform().GetWorldMatrix():MATH::Mat4::Identity();
                const auto pose=SEQUENCER::CaptureShotPose(camera,local,anchor?&anchorMatrix:nullptr);
                // 変えていないレンズや位置の通過点を自動で増やさない。
                const float values[]{pose.eye.x,pose.eye.y,pose.eye.z,pose.target.x,pose.target.y,pose.target.z,pose.fov,pose.roll};
                for(size_t i=0;i<8;++i) {
                    const bool rotationEdit=state.operation==EditorTransformGizmoOperation::Rotate;
                    const bool write=(i<3 && !stageEditTarget_ && !rotationEdit) || (i>=3 && i<6 && !shot->camera.lookAt.IsValid()) || (i==7 && rotationEdit);
                    if(!write) continue;
                    auto& parameter=shot->camera.channels[i];
                    if(autoKey_ || !parameter.keys.empty()) SEQUENCER::SetPresentationKey(parameter,pose.time,values[i]);
                    else parameter.value=values[i];
                }
                cursor_.Pause();previewEnabled_=true;changed=true;
            }
        }
        for(const auto& shot:sequence.cameraCutTrack.clips) {
            const bool selected=selectionKind_==SelectionKind::Shot && selectedId_==shot.id;
            const float local=std::clamp(cursor_.GetTimeSeconds()-shot.startTimeSeconds,0.0f,shot.durationSeconds);
            const auto camera=ResolveShotCamera(scene,sequence,shot,local);
            const auto basis=MATH::BuildViewBasisRH(camera.GetTarget()-camera.GetPosition(),camera.GetUp());
            const ImU32 color=selected?IM_COL32(255,204,104,255):IM_COL32(94,199,222,220);
            const auto position=camera.GetPosition(),front=position+basis.forward*2;
            const float h=std::tan(camera.GetFovYRad()*0.5f)*2,w=h*16/9;
            const MATH::Vec3 corners[]{front-basis.right*w-basis.up*h,front+basis.right*w-basis.up*h,front+basis.right*w+basis.up*h,front-basis.right*w+basis.up*h};
            for(int i=0;i<4;++i) {line(position,corners[i],color);line(corners[i],corners[(i+1)%4],color);}
            if(selected) line(position,camera.GetTarget(),IM_COL32(255,204,104,110));
            ImVec2 screen;
            if(project(position,screen)) {
                draw->AddCircleFilled(screen,selected?5.0f:4.0f,color);
                draw->AddText({screen.x+8,screen.y-6},color,shot.camera.name.c_str());
                const auto mouse=ImGui::GetIO().MousePos;
                if(enabled && !interacting && !ImGui::GetIO().KeyAlt && ImGui::IsWindowHovered() &&
                    std::abs(mouse.x-screen.x)<12 && std::abs(mouse.y-screen.y)<12 && ImGui::IsMouseClicked(0)) clicked=shot.id;
            }
            if(showShotPaths_ && shot.camera.owned) {
                auto last=ResolveShotCamera(scene,sequence,shot,0).GetPosition();
                for(int i=1;i<=48;++i) {
                    const auto next=ResolveShotCamera(scene,sequence,shot,shot.durationSeconds*i/48).GetPosition();
                    line(last,next,color);last=next;
                }
            }
        }
        draw->PopClipRect();
        if(clicked) {selectedId_=clicked;selectionKind_=SelectionKind::Shot;curveView_={};}
        return changed;
    }
}
