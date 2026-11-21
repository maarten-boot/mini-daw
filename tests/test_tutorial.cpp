// Getting started (P32, SPEC-UI-19): T-UI-35.

#include "minidaw/tutorial.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

using namespace minidaw;

TEST_CASE("T-UI-35: the lessons, their steps and checks", "[tutorial][T-UI-35]")
{
    const auto& lessons = tutorialLessons();
    REQUIRE(lessons.size() == 4);
    std::size_t steps = 0;
    std::map<TutorialCheck, int> used;
    for (const auto& l : lessons) {
        INFO(l.title);
        CHECK_FALSE(l.title.empty());
        CHECK_FALSE(l.intro.empty());
        CHECK(l.steps.size() >= 4);
        for (const auto& s : l.steps) {
            INFO(s.text);
            CHECK(s.text.size() > 20);
            CHECK_FALSE(s.where.empty());
            CHECK(s.text.find('\n') == std::string::npos);
            ++used[s.check];
            ++steps;
        }
    }
    CHECK(tutorialStepCount() == steps);
    // Every check is used by one step exactly (None by any number).
    for (int c = static_cast<int>(TutorialCheck::AudioRunning); c <= static_cast<int>(TutorialCheck::DawExported); ++c) {
        INFO("check " << c);
        CHECK(used[static_cast<TutorialCheck>(c)] == 1);
    }
}

TEST_CASE("T-UI-35: a step is done when its fact is seen", "[tutorial][T-UI-35]")
{
    TutorialFacts f;
    for (const auto& l : tutorialLessons()) {
        for (const auto& s : l.steps) CHECK_FALSE(tutorialCheckDone(s.check, f));
    }
    f.trackArmed = true;
    CHECK(tutorialCheckDone(TutorialCheck::TrackArmed, f));
    CHECK_FALSE(tutorialCheckDone(TutorialCheck::Recorded, f));
    CHECK_FALSE(tutorialCheckDone(TutorialCheck::None, f));
    f = TutorialFacts{true, true, true, true, true, true, true, true, true,
                      true, true, true, true, true, true, true, true, true};
    for (const auto& l : tutorialLessons()) {
        for (const auto& s : l.steps) CHECK(tutorialCheckDone(s.check, f) == (s.check != TutorialCheck::None));
    }
}

TEST_CASE("T-UI-35: docs/Getting-started.md is the tutorial's text", "[tutorial][T-UI-35]")
{
    const std::string md = tutorialMarkdown();
    CHECK(md.rfind("# Getting started with mini-daw\n", 0) == 0);
    for (const auto& l : tutorialLessons()) CHECK(md.find("## " + l.title + "\n") != std::string::npos);

    const std::filesystem::path doc = std::filesystem::path(MINIDAW_SOURCE_DIR) / "docs" / "Getting-started.md";
    std::ifstream in(doc, std::ios::binary);
    std::stringstream ss;
    if (in) ss << in.rdbuf();
    std::string have = ss.str();
    // A Windows checkout may have CRLF line ends (git's autocrlf): the text counts.
    have.erase(std::remove(have.begin(), have.end(), '\r'), have.end());
    if (!in || have != md) {
        std::ofstream("Getting-started.md", std::ios::binary) << md;
        FAIL(doc.string() << " is missing or differs from src/core/src/tutorial.cpp: the new text is in "
             << (std::filesystem::current_path() / "Getting-started.md").string() << "; copy it over the docs file");
    }
}
