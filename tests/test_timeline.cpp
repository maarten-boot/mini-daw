// Timeline of recordings (SPEC-PLAY-07, P17): T-PLAY-18.

#include "minidaw/path_util.hpp"
#include "minidaw/smf.hpp"
#include "minidaw/timeline.hpp"
#include "minidaw/wav.hpp"
#include "test_support.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <ctime>
#include <fstream>

using namespace minidaw;

TEST_CASE("T-PLAY-18: recording times from folder names", "[timeline][T-PLAY-18]")
{
    std::int64_t a = 0, b = 0, c = 0;
    REQUIRE(recordingTime("take-20261106-212340", false, a));
    REQUIRE(recordingTime("2026-11-06 21-50-13", true, b));
    REQUIRE(recordingTime("2026-11-06 21-50-13-2", true, c));
    CHECK(b - a == 26 * 60 + 33);
    CHECK(c == b);
    CHECK(clockLabel(a, false) == "21:23");
    CHECK(clockLabel(a, true) == "2026-11-06 21:23");
    std::int64_t x = 0;
    CHECK_FALSE(recordingTime("take-2026110-212340", false, x));
    CHECK_FALSE(recordingTime("notes", false, x));
    CHECK_FALSE(recordingTime("2026-11-06 21-50-13", false, x));  // an auto name is not a take name
    CHECK_FALSE(recordingTime("2026-13-06 21-50-13", true, x));
    CHECK_FALSE(recordingTime("2026-11-06 21-50-13x", true, x));
    // Crosscheck #12, T8: the "-N" suffix has 1-9 digits; the day must exist in its month.
    CHECK_FALSE(recordingTime("take-20261106-212340-", false, x));
    CHECK_FALSE(recordingTime("take-20261106-212340-1234567890", false, x));
    CHECK(recordingTime("take-20261106-212340-123456789", false, x));
    CHECK_FALSE(recordingTime("2026-02-29 10-00-00", true, x));
    CHECK(recordingTime("2028-02-29 10-00-00", true, x));
    CHECK_FALSE(recordingTime("2026-04-31 10-00-00", true, x));
    CHECK(recordingTime("2026-12-31 23-59-59", true, x));
    CHECK(clockLabel(x, true) == "2026-12-31 23:59");
}

TEST_CASE("T-PLAY-18: gap labels", "[timeline][T-PLAY-18]")
{
    CHECK(gapLabel(45) == "+45 s");
    CHECK(gapLabel(12 * 60 + 10) == "+12 min");
    CHECK(gapLabel(2 * 3600 + 5 * 60) == "+2 h 05 min");
    CHECK(gapLabel(3 * 86400 + 100) == "+3 days");
    CHECK(gapLabel(86400) == "+1 day");
    // Crosscheck #12, T9: the unit follows the rounded value.
    CHECK(gapLabel(59.4) == "+59 s");
    CHECK(gapLabel(59.6) == "+1 min");
    CHECK(gapLabel(3569) == "+59 min");
    CHECK(gapLabel(3590) == "+1 h 00 min");
    CHECK(gapLabel(86390) == "+1 day");
}

TEST_CASE("T-PLAY-18: short gaps to scale, long gaps as a marker", "[timeline][T-PLAY-18]")
{
    std::vector<RecordingInfo> r(4);
    r[0].start = 1000;  // 10 s
    r[0].seconds = 10;
    r[1].start = 1015;  // 5 s after it: real distance
    r[1].seconds = 5;
    r[2].start = 1000 + 3600;  // an hour later: a marker
    r[2].seconds = 20;
    r[3].start = 1000 + 3620;  // right after, very short: at least minWidth
    r[3].seconds = 0.5;
    const TimelineLayout l = layoutTimeline(r, 10.0, 10.0, 40.0, 20.0);
    REQUIRE(l.blocks.size() == 4);
    CHECK(l.blocks[0].x == Catch::Approx(0));
    CHECK(l.blocks[0].width == Catch::Approx(100));
    CHECK(l.blocks[1].x == Catch::Approx(150));
    CHECK(l.blocks[1].width == Catch::Approx(50));
    REQUIRE(l.gaps.size() == 1);
    CHECK(l.gaps[0].x == Catch::Approx(200));
    CHECK(l.gaps[0].width == Catch::Approx(40));
    CHECK(l.gaps[0].label == "+1 h 00 min");  // 3580 s (T9: was "+60 min")
    CHECK(l.blocks[2].x == Catch::Approx(240));
    CHECK(l.blocks[2].width == Catch::Approx(200));
    CHECK(l.blocks[3].x == Catch::Approx(440));
    CHECK(l.blocks[3].width == Catch::Approx(20));
    CHECK(l.width == Catch::Approx(460));
    CHECK(layoutTimeline({}, 10, 10, 40, 20).blocks.empty());

    // A short recording drawn wider than its time: the pause after it is still its real length.
    std::vector<RecordingInfo> s(2);
    s[0].start = 0;
    s[0].seconds = 3;
    s[1].start = 54;
    s[1].seconds = 8;
    const TimelineLayout m = layoutTimeline(s, 4.0, 10.0, 40.0, 160.0);
    REQUIRE(m.gaps.size() == 1);
    CHECK(m.gaps[0].label == "+51 s");
    CHECK(m.gaps[0].x == Catch::Approx(160));
    CHECK(m.blocks[1].x == Catch::Approx(200));
}

TEST_CASE("T-PLAY-18: recordings in the same second do not hide each other", "[timeline][T-PLAY-18]")
{
    // Crosscheck #12, T10: a "-2" part in the same second as a short one, then a take
    // (another row) that overlaps both.
    std::vector<RecordingInfo> r(4);
    r[0].autoPart = true;
    r[0].start = 100;
    r[0].seconds = 0.5;
    r[1].autoPart = true;
    r[1].start = 100;  // "-2": the same second
    r[1].seconds = 3;
    r[2].start = 101;  // a take: the other row, may overlap the parts
    r[2].seconds = 1;
    r[3].autoPart = true;
    r[3].start = 105;
    r[3].seconds = 1;
    const TimelineLayout l = layoutTimeline(r, 10.0, 10.0, 40.0, 20.0);
    REQUIRE(l.blocks.size() == 4);
    CHECK(l.blocks[0].x == Catch::Approx(0));
    CHECK(l.blocks[1].x >= l.blocks[0].x + l.blocks[0].width);  // not on top of it
    CHECK(l.blocks[2].x < l.blocks[1].x + l.blocks[1].width);   // the other row may overlap
    CHECK(l.blocks[3].x >= l.blocks[1].x + l.blocks[1].width);  // after the row's last block
    CHECK(l.width >= l.blocks[3].x + l.blocks[3].width);
}

TEST_CASE("T-PLAY-18: the recordings in a takes folder, oldest first", "[timeline][T-PLAY-18]")
{
    test::TempDir dir;
    std::string err;
    const auto take = dir / "take-20261106-212340";
    const auto part = dir / "auto" / "2026-11-06 21-50-13";
    std::filesystem::create_directories(take);
    std::filesystem::create_directories(part);
    std::filesystem::create_directories(dir / "notes");  // not a recording
    {
        WavWriter w;
        REQUIRE(w.open(take / "01-Gtr.wav", 48000, 2, err));
        REQUIRE(w.write(nullptr, 72000, err));  // 1.5 s
        REQUIRE(w.close(err));
    }
    REQUIRE(writeSmf(part / "01-Keys.mid", "p", {SmfTrack{}}, 48000, MidiTiming::Ppq, 96000, false, err));  // 2 s
    const auto recs = listRecordings(dir.path());
    REQUIRE(recs.size() == 2);
    CHECK(recs[0].folder == take);
    CHECK_FALSE(recs[0].autoPart);
    CHECK(recs[0].seconds == Catch::Approx(1.5));
    REQUIRE(recs[0].files.size() == 1);
    CHECK(recs[1].folder == part);
    CHECK(recs[1].autoPart);
    CHECK(recs[1].seconds == Catch::Approx(2.0).margin(0.01));
    CHECK(recs[1].start - recs[0].start == 26 * 60 + 33);
    CHECK(recs[1].clock - recs[0].clock == 26 * 60 + 33);
    CHECK(clockLabel(recs[0].clock, false) == "21:23");
}

TEST_CASE("T-PLAY-18: empty folders, unreadable files, -N folders and reading again", "[timeline][T-PLAY-18]")
{
    // Crosscheck #12: T11 (empty folders left out, unreadable files named), T4 (unchanged folders reused).
    test::TempDir dir;
    std::string err;
    const auto a = dir / "auto" / "2026-11-06 21-50-13";
    const auto b = dir / "auto" / "2026-11-06 21-50-13-2";
    const auto empty = dir / "take-20261106-210000";
    for (const auto& f : {a, b, empty}) std::filesystem::create_directories(f);
    {
        WavWriter w;
        REQUIRE(w.open(a / "01-Gtr.wav", 48000, 1, err));
        REQUIRE(w.write(nullptr, 48000, err));  // 1 s
        REQUIRE(w.close(err));
    }
    { std::ofstream(b / "01-Gtr.wav", std::ios::binary) << "not a wav file"; }
    { std::ofstream(empty / "notes.txt") << "x"; }
    auto recs = listRecordings(dir.path());
    REQUIRE(recs.size() == 2);  // the folder without recordings is left out
    CHECK(recs[0].folder == a);
    CHECK(recs[1].folder == b);  // "-2": after the first of that second
    CHECK(recs[0].problems.empty());
    REQUIRE(recs[1].problems.size() == 1);
    CHECK(recs[1].problems[0].find("01-Gtr.wav") != std::string::npos);
    CHECK(recs[1].seconds == 0.0);

    // Unchanged: taken from the previous list (its length is not read again).
    recs[0].seconds = 99.0;
    auto again = listRecordings(dir.path(), recs);
    REQUIRE(again.size() == 2);
    CHECK(again[0].seconds == 99.0);
    // Grown (a part being recorded): read again.
    {
        WavWriter w;
        REQUIRE(w.open(a / "01-Gtr.wav", 48000, 1, err));
        REQUIRE(w.write(nullptr, 96000, err));  // 2 s
        REQUIRE(w.close(err));
    }
    again = listRecordings(dir.path(), again);
    CHECK(again[0].seconds == Catch::Approx(2.0));
}

#ifndef _WIN32
TEST_CASE("T-PLAY-18: pauses across a daylight-saving change", "[timeline][T-PLAY-18]")
{
    // Crosscheck #12, T7: Central European time (POSIX rule, no zone files needed).
    const char* old = std::getenv("TZ");
    const std::string saved = old != nullptr ? old : "";
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    std::int64_t before = 0, after = 0;
    // Autumn: 01:30 summer time to 03:30 winter time is 3 hours.
    REQUIRE(recordingTime("take-20261025-013000", false, before));
    REQUIRE(recordingTime("take-20261025-033000", false, after));
    CHECK(after - before == 2 * 3600);  // the clock
    CHECK(continuousTime(after) - continuousTime(before) == 3 * 3600);
    // Spring: 01:30 winter time to 03:30 summer time is 1 hour.
    REQUIRE(recordingTime("take-20260329-013000", false, before));
    REQUIRE(recordingTime("take-20260329-033000", false, after));
    CHECK(continuousTime(after) - continuousTime(before) == 3600);
    if (old != nullptr) {
        setenv("TZ", saved.c_str(), 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
}
#endif

TEST_CASE("T-PLAY-18: loading a recording onto the tracks keeps them", "[timeline][T-PLAY-18]")
{
    test::TempDir dir;
    std::string err;
    REQUIRE(writeSmf(dir / "01-Keys.mid", "p", {SmfTrack{}}, 48000, MidiTiming::Ppq, 48000, false, err));
    for (const char* name : {"02-Keys sound.wav", "05-Other.wav"}) {
        WavWriter w;
        REQUIRE(w.open(dir / name, 48000, 2, err));
        REQUIRE(w.close(err));
    }
    REQUIRE(writeSmf(dir / "03-Odd.mid", "p", {SmfTrack{}}, 48000, MidiTiming::Ppq, 48000, false, err));
    std::vector<Track> tracks(3);
    tracks[0].number = 1;
    tracks[0].kind = TrackKind::Midi;
    tracks[0].name = "Keys";
    tracks[0].instrumentClass = "0123456789ABCDEF0123456789ABCDEF";
    tracks[1].number = 2;
    tracks[1].kind = TrackKind::Audio;
    tracks[1].instrumentOf = 1;
    tracks[1].takeStart = 5000;
    tracks[2].number = 3;
    tracks[2].kind = TrackKind::Audio;  // 03-Odd.mid is MIDI: not for this track
    tracks[2].take = "old.wav";
    CHECK(assignTakeFolder(tracks, dir.path(), err) == 4);
    REQUIRE(tracks.size() == 5);
    CHECK(tracks[0].take == dir / "01-Keys.mid");
    CHECK(tracks[0].instrumentClass == "0123456789ABCDEF0123456789ABCDEF");  // kept
    CHECK(tracks[1].take == dir / "02-Keys sound.wav");
    CHECK(tracks[1].takeStart == 0);
    CHECK(tracks[1].instrumentOf == 1);
    CHECK(tracks[2].take == "old.wav");  // unchanged
    CHECK(tracks[3].kind == TrackKind::Midi);  // 03-Odd.mid: a new track, number 3 was taken
    CHECK(tracks[3].number == 4);
    CHECK(tracks[4].number == 5);  // 05-Other.wav
    CHECK(tracks[4].kind == TrackKind::Audio);

    test::TempDir empty;
    CHECK(assignTakeFolder(tracks, empty.path(), err) == 0);
    CHECK_FALSE(err.empty());
}
