#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsPresentation.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include <cmath>
#include "imgui.h"

namespace HIKARI::EDITOR {
    std::vector<CinematicsWorkspaceController::Channel>
    CinematicsWorkspaceController::CollectChannels(CinematicSequence& sequence, bool visibleOnly) {
        std::vector<Channel> result;
        const uint64_t owner=(selectedId_<<2)|static_cast<uint64_t>(selectionKind_);
        if(channelOwner_!=owner) {channelOwner_=owner;curveView_={};}
        const auto effects=[&](auto& items,float start) {
            for(auto& effect:items) {
                const auto name=effect.name.empty()?TEXT::Localize(kCinematicEffectKeys[static_cast<int>(effect.kind)]):effect.name;
                const auto prefix="effect/"+std::to_string(effect.id);
                result.push_back({prefix+"/amount",name+" · "+TEXT::Localize("cine.amount"),&effect.amount,start+effect.start,effect.duration});
                result.push_back({prefix+"/weight",name+" · "+TEXT::Localize("cine.weight"),&effect.weight,start+effect.start,effect.duration});
            }
        };
        if(auto* shot=SelectedShot(sequence);shot && shot->camera.owned) {
            for(size_t i=0;i<shot->camera.channels.size();++i)
                result.push_back({SEQUENCER::kShotChannelNames[i],TEXT::Localize(kCinematicChannelKeys[i]),&shot->camera.channels[i],shot->startTimeSeconds,shot->durationSeconds});
            for(auto& motion:shot->camera.motion) {
                const auto name=TEXT::Localize(SEQUENCER::kCameraMotionDescriptors[static_cast<size_t>(motion.kind)].key);
                const auto prefix="motion/"+std::to_string(motion.id);
                result.push_back({prefix+"/amount",name+" · "+TEXT::Localize("cine.amount"),&motion.amount,shot->startTimeSeconds+motion.start,motion.duration>0?motion.duration:std::max(0.0f,shot->durationSeconds-motion.start)});
                result.push_back({prefix+"/weight",name+" · "+TEXT::Localize("cine.weight"),&motion.weight,shot->startTimeSeconds+motion.start,motion.duration>0?motion.duration:std::max(0.0f,shot->durationSeconds-motion.start)});
            }
            effects(shot->effects,shot->startTimeSeconds);
        } else if(selectionKind_==SelectionKind::Effect) {
            for(auto& effect:sequence.screenEffects) if(effect.id==selectedId_) {
                const auto prefix="effect/"+std::to_string(effect.id);
                result.push_back({prefix+"/amount",TEXT::Localize("cine.amount"),&effect.amount,effect.start,effect.duration});
                result.push_back({prefix+"/weight",TEXT::Localize("cine.weight"),&effect.weight,effect.start,effect.duration});
                break;
            }
        }
        if (visibleOnly && animatedChannelsOnly_)
            std::erase_if(result, [&](const auto& channel) { return channel.parameter->keys.empty() && channel.id != selectedChannel_; });
        return result;
    }

    bool CinematicsWorkspaceController::DrawChannelLanes(CinematicSequence& sequence,
        float x,float y,float width,float labelWidth,float rowHeight) {
        bool changed=false;
        auto channels=CollectChannels(sequence, true);
        auto* draw=ImGui::GetWindowDrawList();
        int row=0;
        for(auto& channel:channels) {
            ImGui::PushID(channel.id.c_str());
            const float top=y+row++*rowHeight, middle=top+rowHeight*0.5f;
            const bool selected=channel.id==selectedChannel_;
            if(selected) draw->AddRectFilled({x,top},{x+width,top+rowHeight},IM_COL32(38,63,75,125));
            draw->AddLine({x,top+rowHeight},{x+width,top+rowHeight},IM_COL32(31,41,51,255));
            draw->PushClipRect({x+5,top},{x+labelWidth-4,top+rowHeight},true);
            draw->AddText({x+12,top+6},selected?IM_COL32(136,231,235,255):IM_COL32(151,170,188,255),channel.label.c_str());
            draw->PopClipRect();
            ImGui::SetCursorScreenPos({x,top});
            ImGui::InvisibleButton("channel",{labelWidth,rowHeight});
            if(ImGui::IsItemClicked()) {selectedChannel_=channel.id;curveView_={};}
            if(ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s",channel.label.c_str());
                if(ImGui::IsMouseDoubleClicked(0)) curveMode_=true;
            }
            auto& keys=channel.parameter->keys;
            int remove=-1;
            for(size_t i=0;i<keys.size();++i) {
                auto& key=keys[i];
                const float px=x+labelWidth+(channel.start+key.time)*zoom_;
                const bool keySelected=selected && curveView_.selectedKey==static_cast<int>(i);
                const ImVec2 diamond[]{{px,middle-5},{px+5,middle},{px,middle+5},{px-5,middle}};
                draw->AddConvexPolyFilled(diamond,4,keySelected?IM_COL32(255,207,108,255):IM_COL32(123,211,221,255));
                ImGui::PushID(static_cast<int>(i));
                ImGui::SetCursorScreenPos({px-7,middle-8});
                ImGui::InvisibleButton("key",{14,16});
                if(ImGui::IsItemActivated()) {
                    selectedChannel_=channel.id;curveView_.selectedKey=static_cast<int>(i);curveView_.fitted=false;
                    cursor_.Pause();cursor_.Seek(channel.start+key.time);previewEnabled_=true;
                }
                if(ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                    float next=(ImGui::GetIO().MousePos.x-x-labelWidth)/zoom_-channel.start;
                    if(!ImGui::GetIO().KeyAlt) next=std::round(next*60)/60;
                    const float lo=i?keys[i-1].time+0.0001f:0;
                    const float hi=i+1<keys.size()?keys[i+1].time-0.0001f:(std::max)(key.time,channel.duration);
                    key.time=std::clamp(next,lo,(std::max)(lo,hi));
                    cursor_.Seek(channel.start+key.time);changed=true;
                }
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%.3f s · %.3f",channel.label.c_str(),key.time,key.value);
                if(ImGui::BeginPopupContextItem("keyMenu")) {
                    if(ImGui::MenuItem(TEXT::Localize("ui.materialGraph.copy").c_str())) keyClipboard_={key};
                    if(ImGui::MenuItem(TEXT::Localize("cine.deleteKey").c_str())) remove=static_cast<int>(i);
                    if(ImGui::MenuItem(TEXT::Localize("cine.editCurve").c_str())) {selectedChannel_=channel.id;curveView_.selectedKey=static_cast<int>(i);curveMode_=true;}
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            ImGui::SetCursorScreenPos({x+labelWidth,top});
            ImGui::InvisibleButton("channelBackground",{width-labelWidth,rowHeight});
            if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                float time=std::clamp((ImGui::GetIO().MousePos.x-x-labelWidth)/zoom_-channel.start,0.0f,channel.duration);
                if(!ImGui::GetIO().KeyAlt) time=std::round(time*60)/60;
                SEQUENCER::SetPresentationKey(*channel.parameter,time,channel.parameter->Evaluate(time));
                selectedChannel_=channel.id;curveView_={};changed=true;
            }
            if(selected && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
                const int selection=curveView_.selectedKey;
                if(selection >= 0 && selection < static_cast<int>(keys.size()) && ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), false)) remove=selection;
                if(ImGui::GetIO().KeyCtrl && ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_C, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), false)) {
                    if(selection>=0 && selection<static_cast<int>(keys.size())) keyClipboard_={keys[selection]};
                    else keyClipboard_=keys;
                }
                if(ImGui::GetIO().KeyCtrl && ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_V, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), false) && !keyClipboard_.empty()) {
                    const float start=std::clamp(cursor_.GetTimeSeconds()-channel.start,0.0f,channel.duration);
                    for(auto key:keyClipboard_) {
                        key.time=start+key.time-keyClipboard_.front().time;
                        if(key.time>channel.duration) continue;
                        SEQUENCER::SetPresentationKey(*channel.parameter,key.time,key.value);
                        for(auto& pasted:keys) if(std::abs(pasted.time-key.time)<0.00001f) pasted=key;
                    }
                    changed=true;curveView_={};
                }
            }
            if(remove>=0 && remove<static_cast<int>(keys.size())) {
                channel.parameter->value=keys[remove].value;
                keys.erase(keys.begin()+remove);curveView_.selectedKey=-1;changed=true;
            }
            ImGui::PopID();
        }
        return changed;
    }

    bool CinematicsWorkspaceController::DrawChannelCurve(CinematicSequence& sequence) {
        auto channels=CollectChannels(sequence);
        if(channels.empty()) {EmptyState(TEXT::Localize("cine.selectAnimated").c_str());return false;}
        auto found=std::find_if(channels.begin(),channels.end(),[&](const auto& c){return c.id==selectedChannel_;});
        if(found==channels.end()) {selectedChannel_=channels.front().id;found=channels.begin();curveView_={};}
        ImGui::SetNextItemWidth((std::min)(420.0f,ImGui::GetContentRegionAvail().x));
        if(ImGui::BeginCombo("##animatedChannel",found->label.c_str())) {
            for(const auto& channel:channels) if(ImGui::Selectable(channel.label.c_str(),channel.id==selectedChannel_)) {
                selectedChannel_=channel.id;curveView_={};
            }
            ImGui::EndCombo();
        }
        found=std::find_if(channels.begin(),channels.end(),[&](const auto& c){return c.id==selectedChannel_;});
        const float height=(std::max)(100.0f,ImGui::GetContentRegionAvail().y-85);
        ImGui::PushID(found->id.c_str());
        const bool changed=DrawPresentationCurve(*found->parameter,found->duration,
            std::clamp(cursor_.GetTimeSeconds()-found->start,0.0f,found->duration),curveView_,height);
        ImGui::PopID();
        return changed;
    }
}
