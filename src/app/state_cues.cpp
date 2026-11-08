#include "state_cues.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <string>
#include <vector>

namespace minidaw::app {
namespace {

struct Cue {
    ImGuiWindow* window = nullptr;
    ImGuiID id = 0;
    ImRect rect;
    ImRect clip;
    bool disabled = false;   // disabled inside beginDisabled(): dashed frame
    bool checkable = false;  // a checkbox: the dashed frame goes round its box only
};

std::vector<Cue> g_cues;
std::vector<bool> g_disabledStack;  // what each beginDisabled() pushed
int g_flatDepth = 0;                // open beginDisabled(true) blocks

ImU32 toU32(const Rgb& c)
{
    return ImGui::GetColorU32(ImVec4(static_cast<float>(c.r), static_cast<float>(c.g), static_cast<float>(c.b), 1.0f));
}

// A dashed rectangle of 1 px lines (ImGui has none).
void dashedRect(ImDrawList* dl, const ImRect& r, ImU32 color, float dash)
{
    auto line = [&](ImVec2 a, ImVec2 b) {
        const float len = std::hypot(b.x - a.x, b.y - a.y);
        if (len <= 0.0f) return;
        const ImVec2 step((b.x - a.x) / len, (b.y - a.y) / len);
        for (float s = 0.0f; s < len; s += 2.0f * dash) {
            const float e = std::min(len, s + dash);
            dl->AddLine(ImVec2(a.x + step.x * s, a.y + step.y * s), ImVec2(a.x + step.x * e, a.y + step.y * e), color,
                        1.0f);
        }
    };
    const ImVec2 a(r.Min.x + 0.5f, r.Min.y + 0.5f);
    const ImVec2 b(r.Max.x - 0.5f, r.Max.y - 0.5f);
    line(a, ImVec2(b.x, a.y));
    line(ImVec2(b.x, a.y), b);
    line(b, ImVec2(a.x, b.y));
    line(ImVec2(a.x, b.y), a);
}

} // namespace

float cueThickness()
{
    return std::max(2.0f, std::round(ImGui::GetFontSize() / 8.0f));
}

void enableStateCues()
{
    ImGui::GetCurrentContext()->TestEngineHookItems = true;
}

void beginStateCuesFrame()
{
    g_cues.clear();
    // The selected tab's line in the Accent role, thick enough to see (also at Max).
    ImGui::GetStyle().TabBarOverlineSize = std::round(cueThickness() * 1.5f);
}

void drawStateCues(const Palette& palette)
{
    ImGuiContext& g = *ImGui::GetCurrentContext();
    const float th = cueThickness();
    const ImU32 accent = toU32(palette[Role::Accent]);
    const ImU32 border = toU32(palette[Role::Border]);
    for (const Cue& c : g_cues) {
        if (c.window == nullptr || !c.window->WasActive) continue;
        ImDrawList* dl = c.window->DrawList;
        dl->PushClipRect(c.clip.Min, c.clip.Max, false);
        if (c.disabled) {
            ImRect r = c.rect;
            if (c.checkable) r.Max.x = r.Min.x + r.GetHeight();  // the box, not its label
            dashedRect(dl, r, border, std::max(2.0f, th * 1.5f));
        } else if (c.id == g.ActiveId || c.id == g.HoveredId || c.id == g.HoveredIdPreviousFrame) {
            // Inside the control, so a frame in a narrow table cell is not cut off.
            ImRect r = c.rect;
            r.Expand(-th * 0.5f);
            dl->AddRect(r.Min, r.Max, accent, 0.0f, 0, th);
        }
        dl->PopClipRect();
    }
}

void beginDisabled(bool disabled)
{
    ImGui::BeginDisabled(disabled);
    g_disabledStack.push_back(disabled);
    if (!disabled) return;
    ++g_flatDepth;
    // Flat: no fill of its own, and no solid frame (the dashed one is drawn instead).
    const ImVec4 none(0, 0, 0, 0);
    for (ImGuiCol col : {ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive, ImGuiCol_FrameBg,
                         ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive}) {
        ImGui::PushStyleColor(col, none);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
}

void endDisabled()
{
    IM_ASSERT(!g_disabledStack.empty());
    const bool disabled = g_disabledStack.back();
    g_disabledStack.pop_back();
    if (disabled) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(6);
        --g_flatDepth;
    }
    ImGui::EndDisabled();
}

bool choice(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size)
{
    // "###" keeps the ID the same with or without the mark.
    const std::string text = std::string(selected ? "> " : "  ") + label + "###choice-" + label;
    const bool pressed = ImGui::Selectable(text.c_str(), selected, flags, size);
    if (selected) {
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + cueThickness(), b.y),
                                                  ImGui::GetColorU32(ImGuiCol_CheckMark));  // Accent role
    }
    return pressed;
}

} // namespace minidaw::app

// --- Dear ImGui item hooks (IMGUI_ENABLE_TEST_ENGINE) ------------------------
// Called for every item with an ID. Only the ones that need a cue are kept.

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* data)
{
    (void)bb;
    if (data == nullptr || id == 0) return;
    ImGuiContext& g = *ctx;
    ImGuiWindow* window = g.CurrentWindow;
    if (window == nullptr || id == window->ID) return;
    const bool disabled = (data->ItemFlags & ImGuiItemFlags_Disabled) != 0;
    const bool flat = disabled && minidaw::app::g_flatDepth > 0;
    const bool hot = !disabled && (id == g.ActiveId || id == g.HoveredId || id == g.HoveredIdPreviousFrame);
    if (!flat && !hot) return;
    // NavRect is the control itself (a slider's frame without its label); for most
    // items it is the same as Rect.
    const ImRect& rect = data->NavRect.GetWidth() > 0.0f ? data->NavRect : data->Rect;
    if (!rect.Overlaps(window->ClipRect)) return;
    minidaw::app::Cue cue;
    cue.window = window;
    cue.id = id;
    cue.rect = rect;
    cue.clip = window->ClipRect;
    cue.disabled = flat;
    minidaw::app::g_cues.push_back(cue);
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char*, ImGuiItemStatusFlags flags)
{
    auto& cues = minidaw::app::g_cues;
    if (!cues.empty() && cues.back().id == id && (flags & ImGuiItemStatusFlags_Checkable) != 0) {
        cues.back().checkable = true;
    }
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID)
{
    return nullptr;
}
