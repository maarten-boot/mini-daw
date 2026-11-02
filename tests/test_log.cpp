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
    CHECK(midiPortBaseName("Keystation 49 MK3 1") == "Keystation 49 MK3");        // Windows: index
    CHECK(midiPortBaseName("Keystation 49 MK3:Keystation 49 MK3 MIDI 1 24:0") ==
          "Keystation 49 MK3:Keystation 49 MK3 MIDI 1");                           // ALSA: client:port
    CHECK(midiPortBaseName("Midi Through:Midi Through Port-0 14:0") == "Midi Through:Midi Through Port-0");
    CHECK(midiPortBaseName("NoNumber") == "NoNumber");
    CHECK(midiPortBaseName("Ends:5") == "Ends:5");
    CHECK(midiPortBaseName("12") == "12");

    const std::vector<std::string> now = {"Midi Through:Midi Through Port-0 14:0", "Keys:Keys MIDI 1 28:0",
                                          "loopMIDI Port 2"};
    CHECK(matchMidiPort(now, "Keys:Keys MIDI 1 28:0") == 1u);  // same name
    CHECK(matchMidiPort(now, "Keys:Keys MIDI 1 24:0") == 1u);  // plugged in again, new client number
    CHECK(matchMidiPort(now, "loopMIDI Port 0") == 2u);        // Windows renumbered it
    CHECK_FALSE(matchMidiPort(now, "Pads 1").has_value());     // not there

    // Two devices of the same kind: only an exact name is trusted.
    const std::vector<std::string> twins = {"Keys 0", "Keys 1"};
    CHECK(matchMidiPort(twins, "Keys 1") == 1u);
    CHECK_FALSE(matchMidiPort(twins, "Keys 2").has_value());
}
