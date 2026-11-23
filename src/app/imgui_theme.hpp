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

// The same colours, pushed for one area with its own background (a tab page,
// the transport, the status line; P35) and popped after it. Every pushPalette needs one popPalette.
void pushPalette(const Palette& palette, double disabledOpacity);
void popPalette();

} // namespace minidaw::app
