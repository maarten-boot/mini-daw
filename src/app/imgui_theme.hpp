#pragma once

// Applies a minidaw::Palette to Dear ImGui. Every ImGuiCol slot is set
// explicitly from a palette role, so no default ImGui colour can slip past
// the contrast check.

#include "minidaw/theme.hpp"

namespace minidaw::app {

// Role used for an ImGuiCol slot.
Role roleForImGuiColor(int imguiCol);

void applyPalette(const Palette& palette);

} // namespace minidaw::app
