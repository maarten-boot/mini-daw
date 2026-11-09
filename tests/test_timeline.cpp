// Timeline of recordings (SPEC-PLAY-07, P17): T-PLAY-18.

#include "minidaw/path_util.hpp"
#include "minidaw/project.hpp"
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

namespace {
void wavFile(const std::filesystem::path& p, unsigned frames)
{
    std::string err;
    std::filesystem::create_directories(p.parent_path());
    WavWriter w;
    REQUIRE(w.open(p, 48000, 1, err));
    REQUIRE(w.write(nullptr, frames, err));
    REQUIRE(w.close(err));
}
} // namespace

TEST_CASE("T-PLAY-19: a recording's name and note are kept beside it and shown", "[timeline][T-PLAY-19]")
{
    test::TempDir dir;
    std::string err;
    const auto take = dir / "take-20261109-143000";
    wavFile(take / "01-Gtr.wav", 4800);

    RecordingNotes n;
    REQUIRE(loadRecordingNotes(take, n, err));  // none yet: empty, fine
    CHECK(n.empty());
    n.name = "Chorus — take 2";
    n.note = "line one\nline \"two\"";
    n.marks.push_back({83.25, "good part"});
    REQUIRE(saveRecordingNotes(take, n, err));
    RecordingNotes back;
    REQUIRE(loadRecordingNotes(take, back, err));
    CHECK(back.name == n.name);
    CHECK(back.note == n.note);
    REQUIRE(back.marks.size() == 1);
    CHECK(back.marks[0].seconds == Catch::Approx(83.25));
    CHECK(back.marks[0].text == "good part");

    // The timeline shows it; the folder keeps its name (and its place in time).
    auto recs = listRecordings(dir.path());
    REQUIRE(recs.size() == 1);
    CHECK(recs[0].notes.name == n.name);
    CHECK(recs[0].folder == take);
    // Changed notes are read again, also when the files are the same (no reading again of the WAV).
    n.name = "Verse";
    REQUIRE(saveRecordingNotes(take, n, err));
    recs = listRecordings(dir.path(), recs);
    CHECK(recs[0].notes.name == "Verse");

    // Empty notes remove the file.
    REQUIRE(saveRecordingNotes(take, RecordingNotes{}, err));
    CHECK_FALSE(std::filesystem::exists(take / kRecordingNotesFile));

    // Damaged, another file, newer version: an error, empty notes, the recording still listed.
    for (const char* text : {"{not json", R"({"format":"other"})", R"({"format":"mini-daw recording","version":9})"}) {
        std::ofstream(take / kRecordingNotesFile) << text;
        CHECK_FALSE(loadRecordingNotes(take, back, err));
        CHECK(back.empty());
    }
    recs = listRecordings(dir.path());
    REQUIRE(recs.size() == 1);
    CHECK_FALSE(recs[0].notesProblem.empty());
    // Wrong-typed fields are left out, the rest is read.
    std::ofstream(take / kRecordingNotesFile)
        << R"({"format":"mini-daw recording","version":1,"name":7,"note":"ok","marks":[{"seconds":"x"},{"seconds":-1},{"seconds":2,"text":"t"}]})";
    REQUIRE(loadRecordingNotes(take, back, err));
    CHECK(back.name.empty());
    CHECK(back.note == "ok");
    CHECK(back.marks.size() == 1);
}

TEST_CASE("T-PLAY-19: deleting a recording moves it to 'deleted', out of the timeline", "[timeline][T-PLAY-19]")
{
    test::TempDir dir;
    std::string err;
    const auto take = dir / "take-20261109-143000";
    const auto part = dir / "auto" / "2026-11-09 14-35-00";
    wavFile(take / "01-Gtr.wav", 4800);
    wavFile(part / "01-Gtr.wav", 4800);
    REQUIRE(listRecordings(dir.path()).size() == 2);

    CHECK(isInsideFolder(take / "01-Gtr.wav", take));
    CHECK_FALSE(isInsideFolder(dir / "take-20261109-143000x" / "01-Gtr.wav", take));
    CHECK_FALSE(isInsideFolder(take, take));

    std::filesystem::path to;
    REQUIRE(moveRecordingToDeleted(dir.path(), take, to, err));
    CHECK(to == dir.path() / "deleted" / "take-20261109-143000");
    CHECK(std::filesystem::exists(to / "01-Gtr.wav"));
    REQUIRE(moveRecordingToDeleted(dir.path(), part, to, err));
    CHECK(to == dir.path() / "deleted" / "auto" / "2026-11-09 14-35-00");
    CHECK(listRecordings(dir.path()).empty());

    // The same name deleted again: "-2".
    wavFile(take / "01-Gtr.wav", 4800);
    REQUIRE(moveRecordingToDeleted(dir.path(), take, to, err));
    CHECK(to.filename() == "take-20261109-143000-2");

    // Not a recording of this takes folder, or gone: refused.
    CHECK_FALSE(moveRecordingToDeleted(dir.path(), dir / "elsewhere" / "take-20261109-150000", to, err));
    std::filesystem::create_directories(dir / "other" / "take-20261109-150000");
    CHECK_FALSE(moveRecordingToDeleted(dir.path(), dir / "other" / "take-20261109-150000", to, err));
}

TEST_CASE("T-PLAY-19: a block with a name can be wider than the others", "[timeline][T-PLAY-19]")
{
    std::vector<RecordingInfo> recs(2);
    recs[0].start = 0;
    recs[0].seconds = 1.0;
    recs[1].start = 5;
    recs[1].seconds = 1.0;
    const std::vector<double> widths = {200.0, 0.0};
    const auto l = layoutTimeline(recs, 4.0, 10.0, 50.0, 80.0, &widths);
    REQUIRE(l.blocks.size() == 2);
    CHECK(l.blocks[0].width == 200.0);
    CHECK(l.blocks[1].width == 80.0);
    CHECK(l.blocks[1].x >= l.blocks[0].x + 200.0);  // never starts inside the wider one
}

TEST_CASE("T-REC-35: notes made while recording are stored as marks, in time order", "[timeline][T-REC-35]")
{
    test::TempDir dir;
    std::string err;
    const auto take = dir / "take-20261109-160000";  // not there yet (a MIDI-only take while recording)
    REQUIRE(addRecordingMark(take, {12.5, "intro ok"}, err));
    REQUIRE(addRecordingMark(take, {3.0, "count in"}, err));
    RecordingNotes n;
    REQUIRE(loadRecordingNotes(take, n, err));
    REQUIRE(n.marks.size() == 2);
    CHECK(n.marks[0].text == "count in");
    CHECK(n.marks[1].seconds == Catch::Approx(12.5));
    // Name and note stay when marks are added, and marks stay when they are edited (P21).
    n.name = "Verse";
    REQUIRE(saveRecordingNotes(take, n, err));
    REQUIRE(addRecordingMark(take, {12.5, "same moment"}, err));
    REQUIRE(loadRecordingNotes(take, n, err));
    CHECK(n.name == "Verse");
    REQUIRE(n.marks.size() == 3);
    CHECK(n.marks[2].text == "same moment");  // after the earlier one at that moment
    // A file that cannot be read is not overwritten.
    std::ofstream(take / kRecordingNotesFile) << "{damaged";
    CHECK_FALSE(addRecordingMark(take, {1.0, "x"}, err));
    std::string bytes;
    std::ifstream in(take / kRecordingNotesFile);
    std::getline(in, bytes);
    CHECK(bytes == "{damaged");
}
