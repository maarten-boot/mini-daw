// Timeline of recordings (SPEC-PLAY-07, P17): T-PLAY-18.

#include "minidaw/path_util.hpp"
#include "minidaw/smf.hpp"
#include "minidaw/timeline.hpp"
#include "minidaw/wav.hpp"
#include "test_support.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
}

TEST_CASE("T-PLAY-18: gap labels", "[timeline][T-PLAY-18]")
{
    CHECK(gapLabel(45) == "+45 s");
    CHECK(gapLabel(12 * 60 + 10) == "+12 min");
    CHECK(gapLabel(2 * 3600 + 5 * 60) == "+2 h 05 min");
    CHECK(gapLabel(3 * 86400 + 100) == "+3 days");
    CHECK(gapLabel(86400) == "+1 day");
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
    CHECK(l.gaps[0].label == "+60 min");
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
}

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
