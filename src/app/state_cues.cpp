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
    bool disabled = false;   // disabled (by beginDisabled or by ImGui itself): dashed frame
    bool flatBlock = false;  // inside a beginDisabled(true) block
    bool checkable = false;  // a checkbox: the dashed frame goes round its box only
};

std::vector<Cue> g_cues;            // every item with an ID this frame (crosscheck #15, W3)
std::vector<DrawnCue> g_drawn;      // what drawStateCues drew (for the tests)
std::vector<bool> g_disabledStack;  // what each beginDisabled() pushed
int g_flatDepth = 0;                // open beginDisabled(true) blocks
int g_strayEnds = 0;                // endDisabled() without a beginDisabled() (W7)

// The colours a disabled control loses (flat: no fill of its own).
constexpr ImGuiCol kFlatColors[] = {ImGuiCol_Button,         ImGuiCol_ButtonHovered,   ImGuiCol_ButtonActive,
                                    ImGuiCol_FrameBg,        ImGuiCol_FrameBgHovered,  ImGuiCol_FrameBgActive,
                                    ImGuiCol_CheckboxSelectedBg};
constexpr int kFlatColorCount = static_cast<int>(sizeof kFlatColors / sizeof kFlatColors[0]);

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
    g_drawn.clear();
    for (const Cue& c : g_cues) {
        if (c.window == nullptr || !c.window->WasActive) continue;
        // Hovered / pressed decided now, from the frame's final state (no lag, W3).
        const bool hot = !c.disabled && c.id != 0 && (c.id == g.ActiveId || c.id == g.HoveredId);
        if (!c.disabled && !hot) continue;
        ImDrawList* dl = c.window->DrawList;
        dl->PushClipRect(c.clip.Min, c.clip.Max, false);
        ImRect r = c.rect;
        if (c.disabled) {
            // A checkbox: its box, not its label. Only for ours: ImGui reports every
            // menu item as checkable too (W1).
            if (c.checkable && c.flatBlock) r.Max.x = r.Min.x + r.GetHeight();
            dashedRect(dl, r, border, std::max(4.0f, th * 2.0f));
            g_drawn.push_back({DrawnCue::Kind::Disabled, r.Min, r.Max});
        } else {
            // Inside the control, so a frame in a narrow table cell is not cut off.
            r.Expand(-th * 0.5f);
            dl->AddRect(r.Min, r.Max, accent, 0.0f, 0, th);
            g_drawn.push_back({c.id == g.ActiveId ? DrawnCue::Kind::Pressed : DrawnCue::Kind::Hovered, r.Min, r.Max});
        }
        dl->PopClipRect();
    }
}

const std::vector<DrawnCue>& lastDrawnCues()
{
    return g_drawn;
}

void beginDisabled(bool disabled)
{
    ImGui::BeginDisabled(disabled);
    g_disabledStack.push_back(disabled);
    if (!disabled) return;
    ++g_flatDepth;
    // Flat: no fill of its own (also a ticked checkbox, W2), and no solid frame
    // (the dashed one is drawn instead).
    const ImVec4 none(0, 0, 0, 0);
    for (ImGuiCol col : kFlatColors) ImGui::PushStyleColor(col, none);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
}

void endDisabled()
{
    // A stray call is ignored (and counted), never undefined behaviour (W7).
    if (g_disabledStack.empty()) {
        ++g_strayEnds;
        return;
    }
    const bool disabled = g_disabledStack.back();
    g_disabledStack.pop_back();
    if (disabled) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(kFlatColorCount);
        --g_flatDepth;
    }
    ImGui::EndDisabled();
}

int strayEndDisabledCount()
{
    return g_strayEnds;
}

bool choice(const char* label, bool selected, ImGuiSelectableFlags flags, const ImVec2& size)
{
    // "###" keeps the ID the same with or without the mark. The font is monospaced,
    // so "> " and two spaces are equally wide: entries do not move (W6).
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
// Called for every item with an ID; whether it is hovered is decided at the end
// of the frame (drawStateCues).

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* data)
{
    (void)bb;
    if (data == nullptr || id == 0) return;
    ImGuiContext& g = *ctx;
    ImGuiWindow* window = g.CurrentWindow;
    if (window == nullptr || id == window->ID) return;
    // Title bars and scrollbars are not controls a click acts on (W2).
    if (id == window->MoveId || id == ImGui::GetWindowScrollbarID(window, ImGuiAxis_X) ||
        id == ImGui::GetWindowScrollbarID(window, ImGuiAxis_Y)) {
        return;
    }
    // NavRect is the control itself (a slider's frame without its label); for most
    // items it is the same as Rect.
    const ImRect& rect = data->NavRect.GetWidth() > 0.0f ? data->NavRect : data->Rect;
    if (!rect.Overlaps(window->ClipRect)) return;
    minidaw::app::Cue cue;
    cue.window = window;
    cue.id = id;
    cue.rect = rect;
    cue.clip = window->ClipRect;
    cue.disabled = (data->ItemFlags & ImGuiItemFlags_Disabled) != 0;  // also ImGui's own (W1)
    cue.flatBlock = cue.disabled && minidaw::app::g_flatDepth > 0;
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
