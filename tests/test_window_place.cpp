// T-UI-31: own windows opened together do not cover each other (P26a,
// src/app/window_place.cpp), in a headless Dear ImGui context.

#include "window_place.hpp"

#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include <functional>
#include <string>
#include <vector>

using minidaw::app::cascadePlace;
using minidaw::app::placeNewWindow;

TEST_CASE("T-UI-31: the cascade skips places that are taken", "[ui][T-UI-31]")
{
    const ImVec2 base(60, 60), lo(0, 0), hi(1000, 700);
    auto same = [](ImVec2 a, ImVec2 b) { return a.x == b.x && a.y == b.y; };
    CHECK(same(cascadePlace(base, 32, lo, hi, {}), base));
    CHECK(same(cascadePlace(base, 32, lo, hi, {ImVec2(500, 500)}), base));
    CHECK(same(cascadePlace(base, 32, lo, hi, {ImVec2(70, 50)}), ImVec2(92, 92)));  // within half a step: taken
    CHECK(same(cascadePlace(base, 32, lo, hi, {ImVec2(60, 60), ImVec2(92, 92)}), ImVec2(124, 124)));
    // Past the bottom: a new column at the top, one step further right.
    std::vector<ImVec2> column;
    for (float y = 60; y <= 700; y += 32) column.push_back(ImVec2(60 + (y - 60), y));
    const ImVec2 next = cascadePlace(base, 32, lo, hi, column);
    CHECK(same(next, ImVec2(92, 60)));
    CHECK(same(cascadePlace(base, 0, lo, hi, {base}), base));  // no step: the base
}

namespace {

struct Headless {
    Headless()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1000, 700);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefaultVector();
    }
    ~Headless() { ImGui::DestroyContext(); }
    static void frame(const std::function<void()>& ui)
    {
        ImGui::NewFrame();
        ui();
        ImGui::Render();
    }
};

// Height 0: fitted to the contents, as most of mini-daw's windows; ImGui then
// hides it in its first frame while it measures it, and it must still count.
ImVec2 window(const char* title)
{
    placeNewWindow(title);
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_Appearing);
    ImGui::Begin(title);
    ImGui::TextUnformatted("text");
    const ImVec2 p = ImGui::GetWindowPos();
    ImGui::End();
    return p;
}

} // namespace

TEST_CASE("T-UI-31: windows opened in the same frame get their own place", "[ui][T-UI-31]")
{
    Headless h;
    ImVec2 log, tabs, third;
    h.frame([&] {
        log = window("Log");
        tabs = window("Tabs");
    });
    CHECK(log.x == 60.0f);
    CHECK(log.y == 60.0f);
    CHECK(tabs.x > log.x);
    CHECK(tabs.y > log.y);
    // Later: a third one opens beside both.
    h.frame([&] {
        window("Log");
        window("Tabs");
        third = window("Name###third");
    });
    CHECK(third.x > tabs.x);
    // The user moves Tabs, closes it and opens it again: ImGui's own place is kept.
    h.frame([&] {
        window("Log");
        ImGui::SetNextWindowPos(ImVec2(400, 300));
        window("Tabs");
    });
    h.frame([&] { window("Log"); });
    ImVec2 again;
    h.frame([&] {
        window("Log");
        again = window("Tabs");
    });
    CHECK(again.x == 400.0f);
    CHECK(again.y == 300.0f);
}

TEST_CASE("T-UI-31: other kinds of windows, ### titles, many windows (crosscheck #21 CC7)", "[ui][T-UI-31]")
{
    Headless h;
    ImVec2 first, again, popupOwner;
    h.frame([&] {
        // The main window (no title bar) and a popup at ImGui's default place do not count.
        ImGui::SetNextWindowPos(ImVec2(60, 60));
        ImGui::SetNextWindowSize(ImVec2(500, 400));
        ImGui::Begin("main", nullptr, ImGuiWindowFlags_NoTitleBar);
        ImGui::OpenPopup("menu");
        ImGui::SetNextWindowPos(ImVec2(60, 60));
        if (ImGui::BeginPopup("menu")) {
            ImGui::TextUnformatted("item");
            ImGui::EndPopup();
        }
        ImGui::End();
        popupOwner = window("Note - song A###note");
    });
    CHECK(popupOwner.x == 60.0f);
    CHECK(popupOwner.y == 60.0f);
    // The same ### window under another visible title is the same window: not placed again.
    h.frame([&] {
        ImGui::SetNextWindowPos(ImVec2(300, 200));
        first = window("Note - song A###note");
    });
    h.frame([] {});  // closed
    h.frame([&] { again = window("Note - song B###note"); });
    CHECK(first.x == 300.0f);
    CHECK(again.x == 300.0f);
    CHECK(again.y == 200.0f);

    // Many windows at once: every one gets a place of its own, inside the screen (1000 x 700).
    std::vector<ImVec2> places;
    h.frame([&] {
        for (int i = 0; i < 30; ++i) places.push_back(window(("W" + std::to_string(i)).c_str()));
    });
    int same = 0;
    for (std::size_t i = 0; i < places.size(); ++i) {
        CHECK(places[i].x < 1000.0f);
        CHECK(places[i].y < 700.0f);
        for (std::size_t j = 0; j < i; ++j) same += (places[i].x == places[j].x && places[i].y == places[j].y) ? 1 : 0;
    }
    CHECK(same == 0);
}
