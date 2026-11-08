// T-UI-25 (automated part): the state cues of src/app/state_cues.cpp in a headless
// Dear ImGui context (no window, no renderer).

#include "state_cues.hpp"

#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include <functional>

using minidaw::app::DrawnCue;

namespace {

struct Headless {
    Headless()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(800, 600);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;  // no renderer: textures stay in the draw data
        io.Fonts->AddFontDefaultVector();
        minidaw::app::enableStateCues();
    }
    ~Headless() { ImGui::DestroyContext(); }

    // One frame with the mouse at `mouse` (button held or not).
    void frame(ImVec2 mouse, bool down, const std::function<void()>& ui)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(mouse.x, mouse.y);
        io.AddMouseButtonEvent(0, down);
        ImGui::NewFrame();
        minidaw::app::beginStateCuesFrame();
        ui();
        minidaw::app::drawStateCues(minidaw::defaultPalette());
        ImGui::Render();
    }

    static int count(DrawnCue::Kind kind)
    {
        int n = 0;
        for (const auto& c : minidaw::app::lastDrawnCues()) n += c.kind == kind ? 1 : 0;
        return n;
    }
};

// A titled window at (100, 100) with the given contents.
void window(const std::function<void()>& contents)
{
    ImGui::SetNextWindowPos(ImVec2(100, 100));
    ImGui::SetNextWindowSize(ImVec2(400, 300));
    ImGui::Begin("Test");
    contents();
    ImGui::End();
}

} // namespace

TEST_CASE("T-UI-25: hovered and pressed controls get a frame, decided in the same frame", "[state_cues][T-UI-25]")
{
    Headless h;
    ImVec2 min, max;
    auto ui = [&] {
        window([&] {
            ImGui::Button("Press me", ImVec2(120, 30));
            min = ImGui::GetItemRectMin();
            max = ImGui::GetItemRectMax();
        });
    };
    h.frame(ImVec2(0, 0), false, ui);  // layout
    h.frame(ImVec2(0, 0), false, ui);
    CHECK(Headless::count(DrawnCue::Kind::Hovered) == 0);
    const ImVec2 inside((min.x + max.x) / 2, (min.y + max.y) / 2);
    h.frame(inside, false, ui);
    REQUIRE(Headless::count(DrawnCue::Kind::Hovered) == 1);  // no frame of delay (W3)
    const DrawnCue c = minidaw::app::lastDrawnCues().front();
    CHECK(c.min.x >= min.x);  // inside the control
    CHECK(c.max.x <= max.x);
    h.frame(inside, true, ui);
    CHECK(Headless::count(DrawnCue::Kind::Pressed) == 1);
    h.frame(inside, false, ui);
    h.frame(ImVec2(0, 0), false, ui);
    CHECK(minidaw::app::lastDrawnCues().empty());  // gone at once when the mouse leaves
}

TEST_CASE("T-UI-25: disabled controls get a dashed frame, also ImGui's own disabled menu items", "[state_cues][T-UI-25]")
{
    Headless h;
    ImVec2 boxMin, boxMax, itemMin, itemMax;
    bool ticked = true;
    auto ui = [&] {
        window([&] {
            minidaw::app::beginDisabled(true);
            ImGui::Button("Disabled");
            ImGui::Checkbox("A ticked checkbox with a long label", &ticked);
            boxMin = ImGui::GetItemRectMin();
            boxMax = ImGui::GetItemRectMax();
            minidaw::app::endDisabled();
            ImGui::MenuItem("Not now", nullptr, false, false);  // disabled by ImGui itself (W1)
            itemMin = ImGui::GetItemRectMin();
            itemMax = ImGui::GetItemRectMax();
        });
    };
    h.frame(ImVec2(0, 0), false, ui);
    h.frame(ImVec2(0, 0), false, ui);
    REQUIRE(Headless::count(DrawnCue::Kind::Disabled) == 3);
    bool sawBox = false, sawMenuRow = false;
    for (const auto& c : minidaw::app::lastDrawnCues()) {
        if (c.min.y == boxMin.y) {
            sawBox = true;
            CHECK(c.max.x - c.min.x < (boxMax.x - boxMin.x) / 2);  // the box only, not the label
        }
        if (c.min.y == itemMin.y) {
            sawMenuRow = true;
            CHECK(c.max.x - c.min.x > (itemMax.x - itemMin.x) / 2);  // the whole menu row, not a square
        }
    }
    CHECK(sawBox);
    CHECK(sawMenuRow);
    // A disabled control gets no hover frame.
    h.frame(ImVec2((itemMin.x + itemMax.x) / 2, (itemMin.y + itemMax.y) / 2), false, ui);
    CHECK(Headless::count(DrawnCue::Kind::Hovered) == 0);
}

TEST_CASE("T-UI-25: no frame on a title bar or a scrollbar", "[state_cues][T-UI-25]")
{
    Headless h;
    auto ui = [&] {
        window([&] {
            for (int i = 0; i < 60; ++i) ImGui::Text("line %d", i);  // makes a vertical scrollbar
        });
    };
    h.frame(ImVec2(0, 0), false, ui);
    h.frame(ImVec2(0, 0), false, ui);
    h.frame(ImVec2(200, 108), false, ui);  // on the title bar
    h.frame(ImVec2(200, 108), false, ui);
    CHECK(minidaw::app::lastDrawnCues().empty());
    h.frame(ImVec2(495, 250), false, ui);  // on the scrollbar at the right edge
    h.frame(ImVec2(495, 250), false, ui);
    CHECK(minidaw::app::lastDrawnCues().empty());
}

TEST_CASE("T-UI-25: beginDisabled / endDisabled stay balanced; a stray end is ignored", "[state_cues][T-UI-25]")
{
    Headless h;
    int colors = -1, vars = -1, disabled = -1;
    h.frame(ImVec2(0, 0), false, [&] {
        window([&] {
            ImGuiContext& g = *ImGui::GetCurrentContext();
            const int c0 = g.ColorStack.Size, v0 = g.StyleVarStack.Size;
            minidaw::app::beginDisabled(true);
            minidaw::app::beginDisabled(false);  // inside a disabled block
            ImGui::Button("x");
            minidaw::app::beginDisabled(true);
            ImGui::Button("y");
            minidaw::app::endDisabled();
            minidaw::app::endDisabled();
            minidaw::app::endDisabled();
            colors = g.ColorStack.Size - c0;
            vars = g.StyleVarStack.Size - v0;
            disabled = g.DisabledStackSize;
        });
    });
    CHECK(colors == 0);
    CHECK(vars == 0);
    CHECK(disabled == 0);
    const int before = minidaw::app::strayEndDisabledCount();
    h.frame(ImVec2(0, 0), false, [&] { window([&] { minidaw::app::endDisabled(); }); });
    CHECK(minidaw::app::strayEndDisabledCount() == before + 1);
}

TEST_CASE("T-UI-25: a choice keeps its ID with or without the mark", "[state_cues][T-UI-25]")
{
    Headless h;
    ImGuiID selectedId = 0, plainId = 0;
    h.frame(ImVec2(0, 0), false, [&] {
        window([&] {
            minidaw::app::choice("Entry", true);
            selectedId = ImGui::GetItemID();
        });
    });
    h.frame(ImVec2(0, 0), false, [&] {
        window([&] {
            minidaw::app::choice("Entry", false);
            plainId = ImGui::GetItemID();
        });
    });
    CHECK(selectedId != 0);
    CHECK(selectedId == plainId);
}
