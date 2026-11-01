#include "imgui_theme.hpp"

#include <imgui.h>

namespace minidaw::app {

Role roleForImGuiColor(int col)
{
    switch (col) {
    case ImGuiCol_Text:                    return Role::Text;
    case ImGuiCol_TextDisabled:            return Role::TextDisabled;

    case ImGuiCol_WindowBg:
    case ImGuiCol_ChildBg:
    case ImGuiCol_PopupBg:
    case ImGuiCol_MenuBarBg:
    case ImGuiCol_TableRowBg:
    case ImGuiCol_TabDimmed:
    case ImGuiCol_DragDropTargetBg:        return Role::Background;

    case ImGuiCol_FrameBg:
    case ImGuiCol_Button:
    case ImGuiCol_Tab:
    case ImGuiCol_TitleBg:
    case ImGuiCol_TitleBgCollapsed:
    case ImGuiCol_CheckboxSelectedBg:      return Role::Control;
    case ImGuiCol_FrameBgHovered:
    case ImGuiCol_ButtonHovered:
    case ImGuiCol_TabHovered:              return Role::ControlHovered;
    case ImGuiCol_FrameBgActive:
    case ImGuiCol_ButtonActive:            return Role::ControlActive;

    case ImGuiCol_Header:
    case ImGuiCol_TabSelected:
    case ImGuiCol_TabDimmedSelected:
    case ImGuiCol_TitleBgActive:
    case ImGuiCol_TextSelectedBg:          return Role::Selection;
    case ImGuiCol_HeaderHovered:           return Role::SelectionHovered;
    case ImGuiCol_HeaderActive:            return Role::SelectionActive;

    case ImGuiCol_TableHeaderBg:           return Role::TableHeader;
    case ImGuiCol_TableRowBgAlt:           return Role::TableRowAlt;
    case ImGuiCol_ScrollbarBg:             return Role::ScrollbarTrack;

    case ImGuiCol_ScrollbarGrab:
    case ImGuiCol_ScrollbarGrabHovered:
    case ImGuiCol_ScrollbarGrabActive:     return Role::ScrollbarThumb;

    case ImGuiCol_Border:
    case ImGuiCol_Separator:
    case ImGuiCol_SeparatorHovered:
    case ImGuiCol_SeparatorActive:
    case ImGuiCol_ResizeGrip:
    case ImGuiCol_ResizeGripHovered:
    case ImGuiCol_ResizeGripActive:
    case ImGuiCol_TableBorderStrong:
    case ImGuiCol_TableBorderLight:
    case ImGuiCol_TreeLines:
    case ImGuiCol_PlotLines:
    case ImGuiCol_PlotHistogram:           return Role::Border;

    case ImGuiCol_CheckMark:
    case ImGuiCol_SliderGrab:
    case ImGuiCol_SliderGrabActive:
    case ImGuiCol_InputTextCursor:
    case ImGuiCol_TabSelectedOverline:
    case ImGuiCol_TabDimmedSelectedOverline:
    case ImGuiCol_PlotLinesHovered:
    case ImGuiCol_PlotHistogramHovered:
    case ImGuiCol_TextLink:
    case ImGuiCol_DragDropTarget:
    case ImGuiCol_UnsavedMarker:
    case ImGuiCol_NavCursor:
    case ImGuiCol_NavWindowingHighlight:   return Role::Accent;

    default:                               return Role::Text;  // BorderShadow, dim overlays: handled below
    }
}

void applyPalette(const Palette& palette)
{
    ImGuiStyle& style = ImGui::GetStyle();
    for (int i = 0; i < ImGuiCol_COUNT; ++i) {
        const Rgb& c = palette[roleForImGuiColor(i)];
        style.Colors[i] = ImVec4(static_cast<float>(c.r), static_cast<float>(c.g), static_cast<float>(c.b), 1.0f);
    }
    // Not foreground/background pairs: shadows are off, and the dim overlays
    // only darken what is behind a modal window.
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.35f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.35f);

    // Borders everywhere, so controls stay visible even at 21:1 where
    // control and window backgrounds are both pure white.
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 1.0f;
}

} // namespace minidaw::app
