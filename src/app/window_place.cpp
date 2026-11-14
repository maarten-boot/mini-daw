#include "window_place.hpp"

#include <imgui_internal.h>  // FindWindowByName, the open windows and their places

#include <algorithm>
#include <cmath>

namespace minidaw::app {

ImVec2 cascadePlace(ImVec2 base, float step, ImVec2 areaMin, ImVec2 areaMax, const std::vector<ImVec2>& taken)
{
    if (step <= 0.0f) return base;
    const float near = step / 2.0f;
    const float limitX = areaMax.x - 8.0f * step;
    const float limitY = areaMax.y - 8.0f * step;
    ImVec2 at = base;
    float column = base.x;
    for (int i = 0; i < 64; ++i) {
        bool free = true;
        for (const ImVec2& t : taken) {
            if (std::fabs(t.x - at.x) < near && std::fabs(t.y - at.y) < near) {
                free = false;
                break;
            }
        }
        if (free) return at;
        at = ImVec2(at.x + step, at.y + step);
        if (at.y > limitY || at.x > limitX) {  // past the bottom or right: a new column at the top
            column += step;
            if (column > limitX) return base;
            at = ImVec2(column, std::max(areaMin.y, base.y));
        }
    }
    return base;
}

void placeNewWindow(const char* title)
{
    if (ImGui::FindWindowByName(title) != nullptr) return;  // known: ImGui keeps its place
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    std::vector<ImVec2> taken;
    for (const ImGuiWindow* w : g.Windows) {
        if (!(w->Active || w->WasActive) || w->IsFallbackWindow) continue;  // (a new window is Hidden in its first frame: still counted)
        if (w->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_Popup)) continue;
        if (w->Flags & ImGuiWindowFlags_NoTitleBar) continue;  // the main window filling the screen
        taken.push_back(w->Pos);
    }
    const float step = 2.0f * ImGui::GetFontSize();
    const ImVec2 base(vp->WorkPos.x + 60.0f, vp->WorkPos.y + 60.0f);  // ImGui's own default place
    const ImVec2 areaMax(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
    ImGui::SetNextWindowPos(cascadePlace(base, step, vp->WorkPos, areaMax, taken), ImGuiCond_Appearing);
}

} // namespace minidaw::app
