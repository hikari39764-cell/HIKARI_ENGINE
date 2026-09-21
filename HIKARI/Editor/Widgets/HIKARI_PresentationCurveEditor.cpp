#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Widgets/HIKARI_PresentationCurveEditor.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include <cmath>
#include "imgui.h"

namespace HIKARI::EDITOR {
    bool DrawPresentationCurve(SEQUENCER::PresentationParameter& parameter,
        float duration, float playhead, PresentationCurveView& view, float height) {
        bool changed = false;
        const auto tr = [](const char* key) { return TEXT::Localize(key); };
        const auto button = [&](EditorGlyph icon, const char* key) {
            return IconButton(icon, key, EditorButtonTone::Quiet, {24,24}, tr(key).c_str());
        };
        if (button(EditorGlyph::Focus, "cine.fitCurve")) view.fitted = false;
        ImGui::SameLine();
        if (button(EditorGlyph::Keyframe, "cine.addKey")) {
            SEQUENCER::SetPresentationKey(parameter, playhead, parameter.Evaluate(playhead));
            changed = true;
            for (size_t i=0; i<parameter.keys.size(); ++i)
                if (std::abs(parameter.keys[i].time-playhead)<0.0001f) view.selectedKey=static_cast<int>(i);
        }
        auto& keys = parameter.keys;
        if (view.selectedKey >= static_cast<int>(keys.size())) view.selectedKey = -1;
        ImGui::SameLine();
        ImGui::BeginDisabled(view.selectedKey < 0);
        bool remove = button(EditorGlyph::Delete, "cine.deleteKey");
        ImGui::SameLine();
        if (ActionButton(tr("cine.linear").c_str(), "linearKey", EditorButtonTone::Quiet) && view.selectedKey >= 0) {
            const auto i = static_cast<size_t>(view.selectedKey);
            auto& key = keys[i];
            if (i) key.inSlope = (key.value-keys[i-1].value)/(key.time-keys[i-1].time);
            if (i+1<keys.size()) key.outSlope=(keys[i+1].value-key.value)/(keys[i+1].time-key.time);
            changed=true;
        }
        ImGui::SameLine();
        if (ActionButton(tr("cine.smooth").c_str(), "smoothKey", EditorButtonTone::Quiet) && view.selectedKey >= 0) {
            keys[view.selectedKey].inSlope=keys[view.selectedKey].outSlope=0;
            changed=true;
        }
        ImGui::EndDisabled();

        if (!view.fitted) {
            view.duration=(std::max)(duration,1.0f);
            view.low=view.high=parameter.value;
            for (const auto& k:keys) {view.low=(std::min)(view.low,k.value);view.high=(std::max)(view.high,k.value);view.duration=(std::max)(view.duration,k.time);}
            // 接線のオーバーシュートも初期表示範囲へ含める。
            for (int i=0;i<=128;++i) {
                const float value=parameter.Evaluate(view.duration*i/128);
                view.low=(std::min)(view.low,value);view.high=(std::max)(view.high,value);
            }
            const float margin=(std::max)(0.25f,(view.high-view.low)*0.15f);
            view.low-=margin;view.high+=margin;view.fitted=true;
        }
        const auto origin=ImGui::GetCursorScreenPos();
        const ImVec2 size{(std::max)(100.0f,ImGui::GetContentRegionAvail().x),height};
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},IM_COL32(14,20,27,255),4);
        const float pad=12;
        const auto point=[&](float time,float value) {return ImVec2{origin.x+pad+time/view.duration*(size.x-2*pad),origin.y+pad+(view.high-value)/(view.high-view.low)*(size.y-2*pad)};};
        const auto timeAt=[&](float x) {return (x-origin.x-pad)/(size.x-2*pad)*view.duration;};
        const auto valueAt=[&](float y) {return view.high-(y-origin.y-pad)/(size.y-2*pad)*(view.high-view.low);};
        draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
        for(int i=1;i<4;++i) {
            float y=origin.y+size.y*i/4;
            draw->AddLine({origin.x,y},{origin.x+size.x,y},IM_COL32(40,49,60,255));
        }
        for(int i=1;i<=128;++i) {
            const float a=view.duration*(i-1)/128,b=view.duration*i/128;
            draw->AddLine(point(a,parameter.Evaluate(a)),point(b,parameter.Evaluate(b)),IM_COL32(85,206,217,255),2);
        }
        const auto cursor=point(playhead,view.low);
        draw->AddLine({cursor.x,origin.y},{cursor.x,origin.y+size.y},IM_COL32(225,232,245,110));
        // 小さなヒット領域を先に登録し、背景がキーの入力を奪わないようにする。
        for(size_t i=0;i<keys.size();++i) {
            auto& key=keys[i];
            const auto p=point(key.time,key.value);
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetCursorScreenPos({p.x-6,p.y-6});
            ImGui::InvisibleButton("key",{12,12});
            if(ImGui::IsItemActivated()) view.selectedKey=static_cast<int>(i);
            if(ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                float time=timeAt(ImGui::GetIO().MousePos.x);
                if(!ImGui::GetIO().KeyAlt) time=std::round(time*60)/60;
                const float begin=i ? keys[i-1].time+0.0001f : 0;
                const float end=i+1<keys.size() ? keys[i+1].time-0.0001f : (std::max)(duration,key.time);
                key.time=std::clamp(time,begin,(std::max)(begin,end));
                key.value=valueAt(ImGui::GetIO().MousePos.y);
                changed=true;
            }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("%.3f s  ·  %.3f",key.time,key.value);
            draw->AddCircleFilled(p,view.selectedKey==static_cast<int>(i)?5.0f:3.5f,view.selectedKey==static_cast<int>(i)?IM_COL32(255,202,96,255):IM_COL32(198,227,236,255));
            if(view.selectedKey==static_cast<int>(i)) for(int side : {-1,1}) {
                if((side<0 && !i)||(side>0 && i+1==keys.size())) continue;
                const float dt=(side<0 ? key.time-keys[i-1].time : keys[i+1].time-key.time)*0.3f;
                float& slope=side<0 ? key.inSlope:key.outSlope;
                const auto handle=point(key.time+side*dt,key.value+side*dt*slope);
                draw->AddLine(p,handle,IM_COL32(232,183,96,200));
                draw->AddCircle(handle,3,IM_COL32(255,206,107,255));
                ImGui::SetCursorScreenPos({handle.x-5,handle.y-5});
                ImGui::InvisibleButton(side<0 ? "inHandle":"outHandle",{10,10});
                if(ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                    slope=(valueAt(ImGui::GetIO().MousePos.y)-key.value)/(side*(std::max)(0.0001f,dt));
                    changed=true;
                }
            }
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(origin);
        ImGui::InvisibleButton("curveBackground",size);
        if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            float time=std::clamp(timeAt(ImGui::GetIO().MousePos.x),0.0f,(std::max)(0.01f,duration));
            if(!ImGui::GetIO().KeyAlt) time=std::round(time*60)/60;
            SEQUENCER::SetPresentationKey(parameter,time,valueAt(ImGui::GetIO().MousePos.y));changed=true;
            for (size_t i=0; i<keys.size(); ++i)
                if (std::abs(keys[i].time-time)<0.0001f) view.selectedKey=static_cast<int>(i);
        }
        draw->PopClipRect();
        if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
            ConsumeEditorShortcut(EditorShortcutScope::Editing, ImGuiKey_Delete, ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows), false)) remove=true;
        if(remove && view.selectedKey>=0) {parameter.value=keys[view.selectedKey].value;keys.erase(keys.begin()+view.selectedKey);view.selectedKey=-1;changed=true;}
        if(view.selectedKey>=0) {
            auto& key=keys[view.selectedKey];
            ImGui::SetNextItemWidth((std::max)(70.0f,ImGui::GetContentRegionAvail().x*0.4f));
            changed|=ImGui::DragFloat(TEXT::LocalizeWindow("cine.value","keyValue").c_str(),&key.value,0.01f);
        }
        return changed;
    }
}
