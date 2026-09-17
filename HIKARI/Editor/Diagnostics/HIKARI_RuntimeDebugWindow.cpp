#include "Editor/Diagnostics/HIKARI_RuntimeDebugWindow.h"
#include "Runtime/Diagnostics/HIKARI_RuntimeDebugService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "imgui.h"

namespace HIKARI::RUNTIME {
    void RuntimeDebugWindow::Reset() {
        if (open_) SERVICES::GetInputService().SetDeveloperInputCaptured(false);
        open_=false;focus_=false;command_={};
    }
    void RuntimeDebugWindow::Draw(World& world,RuntimeDebugService& service) {
        if (!service.IsEnabled() || !ImGui::GetCurrentContext()) {Reset();return;}
        if (ImGui::IsKeyPressed(ImGuiKey_F10,false)) {open_=!open_;focus_=open_;}
        if (open_ && ImGui::IsKeyPressed(ImGuiKey_Escape,false)) open_=false;
        if (open_) {
            ImGui::SetNextWindowSize(ImVec2(700,380),ImGuiCond_FirstUseEver);
            if (ImGui::Begin("HIKARI Developer Console (F10)",&open_)) {
                ImGui::TextUnformatted("help | world | object <id> | systems | perf | events | trace <id> | emit <id>");
                ImGui::TextDisabled("Development session only. Gameplay/UI input is captured while open.");
                if (ImGui::BeginChild("Output",ImVec2(0,-ImGui::GetFrameHeightWithSpacing()),ImGuiChildFlags_Border)) {
                    const bool bottom=ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-2;
                    for (const auto& line:service.History()) ImGui::TextUnformatted(line.c_str());
                    if (bottom) ImGui::SetScrollHereY(1);
                }
                ImGui::EndChild();
                ImGui::SetNextItemWidth(-1);
                if (focus_) {ImGui::SetKeyboardFocusHere();focus_=false;}
                if (ImGui::InputText("##Command",command_.data(),command_.size(),ImGuiInputTextFlags_EnterReturnsTrue)) {
                    (void)service.Execute(world,command_.data());command_={};focus_=true;
                }
            }
            ImGui::End();
        }
        SERVICES::GetInputService().SetDeveloperInputCaptured(open_);
    }
}
