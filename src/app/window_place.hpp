#pragma once

// Where mini-daw's own windows first open (P26a, SPEC-UI-09a): Dear ImGui puts
// every new window at the same default place, so two windows opened together
// (e.g. Log and Tabs restored at start) lay exactly on top of each other.
// placeNewWindow gives a window, the first time it opens in this run, the first
// place of a cascade that no open window already has; after that ImGui keeps the
// place where the user leaves it.

#include <imgui.h>

#include <vector>

namespace minidaw::app {

// The first of base, base + (step, step), base + 2 (step, step), ... whose top
// left corner is not within step / 2 of one in `taken` (in both directions).
// A place that would put the corner past areaMax - (8 steps) starts again
// at the top, one step further right. If every try is taken: base.
ImVec2 cascadePlace(ImVec2 base, float step, ImVec2 areaMin, ImVec2 areaMax, const std::vector<ImVec2>& taken);

// Call just before ImGui::Begin(title, ...). Does nothing once ImGui knows the
// window (it was open before in this run).
void placeNewWindow(const char* title);

} // namespace minidaw::app
