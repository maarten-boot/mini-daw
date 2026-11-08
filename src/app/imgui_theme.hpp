#pragma once

// Applies a minidaw::Palette to Dear ImGui. Every ImGuiCol slot is set
// explicitly from a palette role, so no default ImGui colour can slip past
// the contrast check.

#include "minidaw/theme.hpp"

namespace minidaw::app {

// Role used for an ImGuiCol slot.
Role roleForImGuiColor(int imguiCol);

// disabledOpacity: how far disabled controls are faded (Theme: disabledOpacity,
// so they still meet the contrast minimum; SPEC-UI-02a).
void applyPalette(const Palette& palette, double disabledOpacity);

} // namespace minidaw::app
