// Tracks and take names (SPEC-REC-01a, SPEC-REC-07): T-REC-19.

#include "minidaw/path_util.hpp"
#include "minidaw/smf.hpp"
#include "minidaw/track.hpp"
#include "minidaw/wav.hpp"
#include "test_support.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace minidaw;

TEST_CASE("T-REC-19: safe file names", "[track][T-REC-19]")
{
    CHECK(safeFileName("Bass") == "Bass");
    CHECK(safeFileName("a/b\\c:d*e?f\"g<h>i|j") == "a_b_c_d_e_f_g_h_i_j");
    CHECK(safeFileName("  .hidden.  ") == "hidden");
    CHECK(safeFileName("...") == "track");
    CHECK(safeFileName("") == "track");
    CHECK(safeFileName("tab\there") == "tab_here");

    Track t;
    t.number = 3;
    t.name = "Keys: left";
    t.kind = TrackKind::Midi;
    CHECK(takeFileName(t) == "03-Keys_ left.mid");
    t.kind = TrackKind::Audio;
    t.number = 12;
    CHECK(takeFileName(t) == "12-Keys_ left.wav");
}

TEST_CASE("T-REC-19: take folders", "[track][T-REC-19]")
{
    std::tm when{};
    when.tm_year = 126;
    when.tm_mon = 10;
    when.tm_mday = 2;
    when.tm_hour = 9;
    when.tm_min = 5;
    when.tm_sec = 7;
    CHECK(takeFolderName(when) == "take-20261102-090507");

    test::TempDir dir;
    std::string err;
    const auto a = createTakeFolder(dir / "rec", "take-x", err);
    const auto b = createTakeFolder(dir / "rec", "take-x", err);
    CHECK(a.filename() == "take-x");
    CHECK(b.filename() == "take-x-2");
    CHECK(std::filesystem::is_directory(b));
}

TEST_CASE("T-REC-19: default tracks and numbering", "[track][T-REC-19]")
{
    CHECK(defaultTracks(0).empty());
    const auto mono = defaultTracks(1);
    REQUIRE(mono.size() == 1);
    CHECK(mono[0].channels == std::vector<unsigned int>{0});
    const auto stereo = defaultTracks(32);
    REQUIRE(stereo.size() == 1);
    CHECK(stereo[0].channels == std::vector<unsigned int>{0, 1});
    CHECK(stereo[0].kind == TrackKind::Audio);
    CHECK_FALSE(stereo[0].armed);

    std::vector<Track> tracks(2);
    tracks[0].number = 1;
    tracks[1].number = 3;
    CHECK(nextTrackNumber(tracks) == 2);
    tracks[1].number = 2;
    CHECK(nextTrackNumber(tracks) == 3);
}

TEST_CASE("T-PLAY-04: track gain from volume, mute and solo", "[track][T-PLAY-04]")
{
    Track a, b;
    a.number = 1;
    b.number = 2;
    CHECK(trackGain(a, false) == 1.0f);
    a.volumeDb = -6.0f;
    CHECK(trackGain(a, false) == Catch::Approx(0.501).margin(0.001));
    a.volumeDb = kMinVolumeDb;
    CHECK(trackGain(a, false) == 0.0f);  // bottom of the slider: silent
    a.volumeDb = 0.0f;
    a.mute = true;
    CHECK(trackGain(a, false) == 0.0f);
    a.mute = false;
    b.solo = true;
    const std::vector<Track> both = {a, b};
    CHECK(anySolo(both));
    CHECK(trackGain(a, anySolo(both)) == 0.0f);  // not soloed while another is
    CHECK(trackGain(b, anySolo(both)) == 1.0f);
    b.mute = true;
    CHECK(trackGain(b, true) == 0.0f);           // mute wins over solo
}

TEST_CASE("T-PLAY-11: take folders listed and opened as tracks", "[track][T-PLAY-11]")
{
    test::TempDir dir;
    for (const char* name : {"take-20261101-090000", "take-20261102-120000", "take-20261102-120000-2", "other"}) {
        std::filesystem::create_directories(dir.path() / name);
    }
    const auto folders = listTakeFolders(dir.path());
    REQUIRE(folders.size() == 3);
    CHECK(folders[0].filename() == "take-20261102-120000-2");
    CHECK(folders[2].filename() == "take-20261101-090000");

    const auto take = dir.path() / "take-20261102-120000";
    std::string err;
    {
        WavWriter w;
        // UTF-8 -> path explicitly: a narrow string would go through the ANSI code page on Windows (M0).
        REQUIRE(w.open(take / pathFromUtf8("01-Gitaar \xc3\xa9.wav"), 48000, 2, err));
        REQUIRE(w.close(err));
        WavWriter m;
        REQUIRE(m.open(take / "03-Mic.wav", 48000, 1, err));
        REQUIRE(m.close(err));
    }
    SmfTrack st;
    REQUIRE(writeSmf(take / "02-Keys.mid", "t", {st}, 48000, MidiTiming::Ppq, 48000, false, err));
    std::ofstream(take / "notes.txt") << "ignored";
    std::ofstream(take / "04-broken.wav") << "not a wav";  // not playable: skipped

    const auto tracks = tracksFromTakeFolder(take, err);
    REQUIRE(tracks.size() == 3);
    CHECK(tracks[0].number == 1);
    CHECK(tracks[0].name == "Gitaar \xc3\xa9");
    CHECK(tracks[0].kind == TrackKind::Audio);
    CHECK(tracks[0].channels == std::vector<unsigned int>{0, 1});
    CHECK(tracks[1].kind == TrackKind::Midi);
    CHECK(tracks[1].name == "Keys");
    CHECK(tracks[1].midiOut.empty());  // no output until chosen
    CHECK(tracks[2].channels == std::vector<unsigned int>{0});
    CHECK(tracks[2].take.filename() == "03-Mic.wav");
    CHECK(tracks[2].takeStart == 0);
    CHECK_FALSE(tracks[0].armed);

    CHECK(tracksFromTakeFolder(dir.path() / "other", err).empty());
    CHECK(err.find("No takes") != std::string::npos);
}
