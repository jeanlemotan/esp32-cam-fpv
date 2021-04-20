#include "HUD.h"
#include "IHAL.h"
#include "imgui.h"

static void AddShadowText(ImDrawList& drawList, const ImVec2& pos, ImU32 col, const char* text)
{
    drawList.AddText(ImVec2(pos.x + 1.f, pos.y + 1.f), ImColor(0.f, 0.f, 0.f, ImColor(col).Value.w), text);
    drawList.AddText(pos, col, text);
}

HUD::HUD(IHAL& hal)
    : m_hal(hal)
{

}

///////////////////////////////////////////////////////////////////////////////////////////////////

void HUD::draw()
{
    ImVec2 display_size(m_hal.get_display_size());
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(display_size.x, display_size.y));
    ImGui::SetNextWindowBgAlpha(0);
    ImGui::Begin("HUD", nullptr, ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoInputs);

    ImGui::End();
}
