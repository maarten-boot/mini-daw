// Diagnostics log (SPEC-LOG-01) and MIDI port matching for hot-plug (SPEC-REC-01b).

#include "minidaw/log.hpp"
#include "minidaw/midi_input.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <sstream>
#include <thread>

using namespace minidaw;
namespace fs = std::filesystem;

namespace {

std::string readAll(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST_CASE("T-LOG-01: log file per run, previous run kept", "[log][T-LOG-01]")
{
    test::TempDir dir;
    const fs::path file = dir.path() / "sub" / "mini-daw.log";
    CHECK(Log::oldFileFor(file).filename() == "mini-daw.old.log");
    {
        Log log;
        REQUIRE(log.open(file).empty());
        log.info("first run");
        log.warning("careful\nsecond line");
    }
    const std::string first = readAll(file);
    CHECK(first.find(" INFO first run\n") != std::string::npos);
    CHECK(first.find(" WARNING careful\n    second line\n") != std::string::npos);
    CHECK(first.size() > 24);
    CHECK(first[4] == '-');   // "2026-11-02 21:05:03.123 ..."
    CHECK(first[19] == '.');
    {
        Log log;
        REQUIRE(log.open(file).empty());
        log.error("second run");
        CHECK(log.file() == file);
    }
    CHECK(readAll(file).find("second run") != std::string::npos);
    CHECK(readAll(file).find("first run") == std::string::npos);
    CHECK(readAll(Log::oldFileFor(file)) == first);
    {
        Log log;  // third run: the second becomes the old one
        REQUIRE(log.open(file).empty());
    }
    CHECK(readAll(Log::oldFileFor(file)).find("second run") != std::string::npos);
}

TEST_CASE("T-LOG-02: lines in memory, counts, levels as text", "[log][T-LOG-02]")
{
    Log log;  // no file: memory only
    log.info("a");
    log.warning("b");
    log.error("c");
    CHECK(log.lineCount() == 3);
    CHECK(log.warningCount() == 1);
    CHECK(log.errorCount() == 1);
    const auto r = log.recent();
    REQUIRE(r.size() == 3);
    CHECK(r[0].text == "a");
    CHECK(r[2].level == LogLevel::Error);
    CHECK(logLevelName(LogLevel::Warning) == "WARNING");
    CHECK(Log::formatLine(r[1]).find(" WARNING b\n") != std::string::npos);

    for (std::size_t i = 0; i < Log::kMemoryLines + 5; ++i) log.info(std::to_string(i));
    const auto many = log.recent();
    CHECK(many.size() == Log::kMemoryLines);
    CHECK(many.front().text == "5");  // oldest lines dropped, order kept
    CHECK(many.back().text == std::to_string(Log::kMemoryLines + 4));

    // Several threads at once (MIDI and GUI threads may both write).
    Log shared;
    std::thread t1([&] { for (int i = 0; i < 1000; ++i) shared.info("x"); });
    std::thread t2([&] { for (int i = 0; i < 1000; ++i) shared.warning("y"); });
    t1.join();
    t2.join();
    CHECK(shared.lineCount() == 2000);
    CHECK(shared.warningCount() == 1000);
}

TEST_CASE("T-LOG-05: the previous log cannot be renamed: the new run is added, nothing lost", "[log][T-LOG-05]")
{
    test::TempDir dir;
    const fs::path file = dir.path() / "mini-daw.log";
    {
        Log log;
        REQUIRE(log.open(file).empty());
        log.info("first run");
    }
    // A non-empty folder in the place of the .old.log cannot be removed or replaced.
    fs::create_directories(Log::oldFileFor(file) / "blocker");
    {
        Log log;
        REQUIRE(log.open(file).empty());
        log.info("second run");
    }
    const std::string text = readAll(file);
    CHECK(text.find("first run") != std::string::npos);
    CHECK(text.find("could not be renamed") != std::string::npos);
    CHECK(text.find("second run") != std::string::npos);
}

TEST_CASE("T-LOG-03: a log file that cannot be created is reported; memory still works", "[log][T-LOG-03]")
{
    test::TempDir dir;
    std::ofstream(dir.path() / "blocker") << "x";
    Log log;
    CHECK_FALSE(log.open(dir.path() / "blocker" / "mini-daw.log").empty());  // parent is a file
    log.info("still kept");
    CHECK(log.recent().size() == 1);
}

TEST_CASE("T-REC-31: MIDI ports are found again under a new number", "[midi][T-REC-31]")
{
    const auto W = MidiNaming::WinMM;
    const auto A = MidiNaming::Alsa;
    const auto P = MidiNaming::Plain;
    CHECK(midiPortBaseName("Keystation 49 MK3 1", W) == "Keystation 49 MK3");                // WinMM: index
    CHECK(midiPortBaseName("Keystation 49 MK3:Keystation 49 MK3 MIDI 1 24:0", A) ==
          "Keystation 49 MK3:Keystation 49 MK3 MIDI 1");                                     // ALSA: client:port
    CHECK(midiPortBaseName("Midi Through:Midi Through Port-0 14:0", A) == "Midi Through:Midi Through Port-0");
    CHECK(midiPortBaseName("Synth 1", A) == "Synth 1");        // ALSA only strips " C:P" (K2)
    CHECK(midiPortBaseName("Keys 24:0", W) == "Keys 24:0");     // WinMM only strips " N"
    CHECK(midiPortBaseName("Synth 1", P) == "Synth 1");        // CoreMIDI/JACK: the number is part of the name
    CHECK(midiPortBaseName("NoNumber", W) == "NoNumber");
    CHECK(midiPortBaseName("Ends:5", A) == "Ends:5");
    CHECK(midiPortBaseName("12", W) == "12");

    const std::vector<std::string> alsa = {"Midi Through:Midi Through Port-0 14:0", "Keys:Keys MIDI 1 28:0"};
    CHECK(matchMidiPort(alsa, "Keys:Keys MIDI 1 28:0", A) == 1u);  // same name
    CHECK(matchMidiPort(alsa, "Keys:Keys MIDI 1 24:0", A) == 1u);  // plugged in again, new client number
    CHECK_FALSE(matchMidiPort(alsa, "Pads 1", A).has_value());     // not there
    CHECK(matchMidiPort({"loopMIDI Port 2"}, "loopMIDI Port 0", W) == 0u);  // Windows renumbered it
    // Without a backend number, "Synth 1" never opens "Synth 2" (K2).
    CHECK_FALSE(matchMidiPort({"Synth 2"}, "Synth 1", P).has_value());
    CHECK(matchMidiPort({"Synth 2", "Synth 1"}, "Synth 1", P) == 1u);

    // Two devices of the same kind: only an exact name is trusted.
    const std::vector<std::string> twins = {"Keys 0", "Keys 1"};
    CHECK(matchMidiPort(twins, "Keys 1", W) == 1u);
    CHECK_FALSE(matchMidiPort(twins, "Keys 2", W).has_value());
}

TEST_CASE("T-REC-33: identical MIDI devices: an unplug is never missed or guessed", "[midi][T-REC-33]")
{
    const auto W = MidiNaming::WinMM;
    auto open = [](std::string name, std::string as) { return MidiPortState{std::move(name), true, std::move(as), false}; };

    SECTION("one device renumbered by Windows: stays connected under its new name")
    {
        const auto plan = planMidiCheck({open("Keys 1", "Keys 1")}, {"Keys 0"}, W);
        CHECK(plan.disconnect.empty());
        CHECK(plan.block.empty());
        REQUIRE(plan.rename.size() == 1);
        CHECK(plan.rename[0].second == "Keys 0");
    }
    SECTION("one device gone")
    {
        const auto plan = planMidiCheck({open("Keys 0", "Keys 0")}, {"Other 0"}, W);
        CHECK(plan.disconnect == std::vector<std::size_t>{0});
    }
    SECTION("two identical devices, the first unplugged (the survivor is renamed 'X 0'): both closed and blocked")
    {
        const auto plan = planMidiCheck({open("X 0", "X 0"), open("X 1", "X 1")}, {"X 0"}, W);
        CHECK(plan.block == std::vector<std::size_t>{0, 1});
        CHECK(plan.disconnect.empty());
        CHECK(plan.connect.empty());
    }
    SECTION("two identical devices, both unplugged: plain disconnects, reopened when back")
    {
        const auto plan = planMidiCheck({open("X 0", "X 0"), open("X 1", "X 1")}, {}, W);
        CHECK(plan.disconnect == std::vector<std::size_t>{0, 1});
        CHECK(plan.block.empty());
    }
    SECTION("three identical devices all present: none closed (was wrongly closed before K1)")
    {
        const auto plan = planMidiCheck({open("X 0", "X 0"), open("X 1", "X 1"), open("X 2", "X 2")},
                                        {"X 0", "X 1", "X 2"}, W);
        CHECK(plan.disconnect.empty());
        CHECK(plan.block.empty());
        CHECK(plan.rename.empty());
    }
    SECTION("three open, one unplugged: all three blocked")
    {
        const auto plan =
            planMidiCheck({open("X 0", "X 0"), open("X 1", "X 1"), open("X 2", "X 2")}, {"X 0", "X 1"}, W);
        CHECK(plan.block.size() == 3);
    }
    SECTION("a blocked port is not reopened; a waiting port does not take an entry another port holds")
    {
        std::vector<MidiPortState> ports = {open("X 0", "X 0"), {"X 1", false, "", true}, {"Y 0", false, "", false}};
        const auto plan = planMidiCheck(ports, {"X 0", "X 1", "Y 3"}, W);
        REQUIRE(plan.connect.size() == 1);
        CHECK(plan.connect[0].first == 2u);
        CHECK(plan.connect[0].second == "Y 3");
    }
    SECTION("two waiting ports for two identical devices: each gets its own")
    {
        std::vector<MidiPortState> ports = {{"X 0", false, "", false}, {"X 1", false, "", false}};
        const auto plan = planMidiCheck(ports, {"X 0", "X 1"}, W);
        REQUIRE(plan.connect.size() == 2);
        CHECK(plan.connect[0].second == "X 0");
        CHECK(plan.connect[1].second == "X 1");
    }
}
