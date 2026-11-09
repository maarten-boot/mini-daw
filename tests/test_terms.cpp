// The Terms tab's word list (P23, SPEC-UI-17).

#include "minidaw/config.hpp"
#include "minidaw/terms.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>
#include <string>

using namespace minidaw;

namespace {

std::string lowered(std::string s)
{
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::vector<std::string> namesOf(const std::vector<std::size_t>& found)
{
    std::vector<std::string> out;
    for (std::size_t i : found) out.push_back(terms()[i].name);
    return out;
}

} // namespace

TEST_CASE("T-UI-28: every term has a text and a known topic, no name twice, every see-also exists", "[ui][T-UI-28]")
{
    const auto& all = terms();
    REQUIRE(all.size() >= 30);
    std::set<std::string> names;
    std::size_t lastGroup = 0;
    for (const Term& t : all) {
        INFO(t.name);
        CHECK(!t.name.empty());
        CHECK(t.text.size() >= 40);
        CHECK(names.insert(lowered(t.name)).second);
        // Listed by topic, the topics in termGroups() order.
        const auto g = std::find(termGroups().begin(), termGroups().end(), t.group);
        REQUIRE(g != termGroups().end());
        const auto group = static_cast<std::size_t>(g - termGroups().begin());
        CHECK(group >= lastGroup);
        lastGroup = group;
        for (const std::string& other : t.seeAlso) {
            INFO("see also " << other);
            CHECK(termIndex(other) < all.size());
            CHECK(lowered(other) != lowered(t.name));
        }
    }
    // Every topic has terms.
    for (const std::string& group : termGroups()) {
        INFO(group);
        CHECK(std::any_of(all.begin(), all.end(), [&](const Term& t) { return t.group == group; }));
    }
    // The words the user asked for (2026-11-09) and those planned in P23.
    for (const char* word : {"mini-daw", "Track", "Take", "Instrument", "Arm", "Take folder", "Auto record part",
                             "Overdub", "Monitor", "Linked sound track", "Render", "Program", "Preset", "Project",
                             "Timeline", "dBFS", "CLIP", "Sample rate", "Buffer", "Latency"}) {
        INFO(word);
        CHECK(termIndex(word) < all.size());
    }
}

TEST_CASE("T-UI-28: finding a word: names first, any case, spaces at the ends ignored", "[ui][T-UI-28]")
{
    CHECK(findTerms("").size() == terms().size());
    CHECK(findTerms("   ").size() == terms().size());
    CHECK(termIndex("TAKE") == termIndex("take"));
    CHECK(termIndex("no such word") == terms().size());

    // "take": the terms called ...take... come first, in list order; then those that only mention it.
    const auto take = namesOf(findTerms("  TAKE "));
    REQUIRE(take.size() > 3);
    CHECK(take[0] == "Take");
    CHECK(take[1] == "Take folder");
    CHECK(take[2] == "Takes folder");
    const auto firstOther = std::find_if(take.begin(), take.end(), [](const std::string& n) {
        return lowered(n).find("take") == std::string::npos;
    });
    CHECK(firstOther != take.end());
    CHECK(std::all_of(firstOther, take.end(),
                      [](const std::string& n) { return lowered(n).find("take") == std::string::npos; }));

    // The place counts too ("Settings > Recording..." is only in where).
    const auto settings = namesOf(findTerms("settings > recording"));
    CHECK(std::find(settings.begin(), settings.end(), "Threshold") != settings.end());

    CHECK(findTerms("zzqx").empty());
}

TEST_CASE("T-UI-28: the Terms tab is in the tab order; older configs get it at the end", "[ui][config][T-UI-28]")
{
    CHECK(isTabId("terms"));
    CHECK(defaultTabOrder().back() == "terms");
    const auto old = parseConfig("[tabs]\norder = recording, timeline, audio\nlast = terms\n");
    CHECK(old.warnings.empty());
    CHECK(old.config.tabs.order == std::vector<std::string>{"recording", "timeline", "audio", "terms"});
    CHECK(old.config.tabs.last == "terms");
}
