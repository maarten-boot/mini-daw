// Tracks and take names (SPEC-REC-01a, SPEC-REC-07): T-REC-19.

#include "minidaw/track.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

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
