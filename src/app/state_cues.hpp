#pragma once

// State cues (PL1, SPEC-UI-15): every control shows that it is hovered, pressed,
// selected or disabled in a way that is not only a colour change, so it also
// works at Max contrast (21:1, black and white only).
//
// - Hovered or pressed: a frame in the Accent role around the control (pressed
//   also has the darker ControlActive fill).
// - Disabled: flat (no fill), a dashed frame instead of the solid one, and
//   faded only as far as the contrast minimum allows (Theme: disabledOpacity).
// - Selected list item: "> " in front of it and an Accent bar on its left.
//
// The hovered / pressed / disabled controls are found through Dear ImGui's item
// hook (IMGUI_ENABLE_TEST_ENGINE, which only reports items; no test engine is
// linked), and the frames are drawn at the end of the frame.

#include "minidaw/theme.hpp"

#include <imgui.h>

namespace minidaw::app {

// After ImGui::CreateContext().
void enableStateCues();
// After ImGui::NewFrame() / before ImGui::Render().
void beginStateCuesFrame();
void drawStateCues(const Palette& palette);

// ImGui::BeginDisabled / EndDisabled with the flat, dashed look. Always pair them.
void beginDisabled(bool disabled = true);
void endDisabled();

// A list choice (combo entries and the like): ImGui::Selectable with "> " and a
// bar when selected. The label is shown as given; its ID stays the same
// whether or not it is selected.
bool choice(const char* label, bool selected, ImGuiSelectableFlags flags = 0, const ImVec2& size = ImVec2(0, 0));

// Thickness of the frames and bars, in pixels, for the current text size.
float cueThickness();

} // namespace minidaw::app
