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

TEST_CASE("T-VST-24: linked sound track and the Sound from rule", "[track][T-VST-24]")
{
    std::vector<Track> tracks(3);
    tracks[0].number = 1;
    tracks[0].kind = TrackKind::Midi;
    tracks[0].instrumentClass = "0123456789ABCDEF0123456789ABCDEF";
    tracks[1].number = 2;
    tracks[1].kind = TrackKind::Audio;
    tracks[1].instrumentOf = 1;  // the sound of track 1
    tracks[1].volumeDb = -6.0f;
    tracks[2].number = 3;
    tracks[2].kind = TrackKind::Audio;
    REQUIRE(linkedSoundTrack(tracks, 1) == &tracks[1]);
    CHECK(linkedSoundTrack(tracks, 3) == nullptr);
    CHECK(soundOwner(tracks, tracks[1]) == &tracks[0]);
    CHECK(soundOwner(tracks, tracks[2]) == nullptr);

    // Sound from the instrument (default): the linked track is silent, the instrument plays the take.
    CHECK(instrumentPlaysTake(tracks[0]));
    CHECK(playbackGain(tracks[1], tracks) == 0.0f);
    CHECK(playbackGain(tracks[0], tracks) == Catch::Approx(1.0f));
    CHECK(playbackGain(tracks[2], tracks) == Catch::Approx(1.0f));

    // Sound from the recording: the linked track plays at its own volume, the instrument gets no take MIDI.
    tracks[0].soundFromRecording = true;
    CHECK_FALSE(instrumentPlaysTake(tracks[0]));
    CHECK(playbackGain(tracks[1], tracks) == Catch::Approx(0.501f).margin(0.001));
    // Muting the MIDI track silences its sound either way.
    tracks[0].mute = true;
    CHECK(playbackGain(tracks[1], tracks) == 0.0f);
    tracks[0].mute = false;
    // Soloing the MIDI track lets its linked track sound too; other tracks go silent.
    tracks[0].solo = true;
    CHECK(playbackGain(tracks[1], tracks) == Catch::Approx(0.501f).margin(0.001));
    CHECK(playbackGain(tracks[2], tracks) == 0.0f);
    tracks[0].solo = false;

    // Placement: a linked track like its MIDI take, an input track as audio (R3).
    CHECK_FALSE(placedAsAudioInput(tracks[0]));
    CHECK_FALSE(placedAsAudioInput(tracks[1]));
    CHECK(placedAsAudioInput(tracks[2]));

    // What a recording sees (R4, R9): no sound from an instrument that is not loaded; linked tracks not armed.
    tracks[1].armed = true;
    auto rec = tracksToRecord(tracks, [](int) { return false; });
    CHECK(rec[0].instrumentClass.empty());
    CHECK_FALSE(rec[1].armed);
    CHECK(tracks[0].instrumentClass.size() == 32);  // the originals are unchanged
    rec = tracksToRecord(tracks, [](int n) { return n == 1; });
    CHECK(rec[0].instrumentClass.size() == 32);
    tracks[1].armed = false;

    // Removing the linked track: Sound from goes back to the instrument (R2).
    std::vector<Track> a = tracks;
    removeTrack(a, 1);
    REQUIRE(a.size() == 2);
    CHECK_FALSE(a[0].soundFromRecording);
    CHECK(instrumentPlaysTake(a[0]));
    // Removing the MIDI track: the linked track becomes a normal audio track.
    std::vector<Track> b = tracks;
    removeTrack(b, 0);
    REQUIRE(b.size() == 2);
    CHECK(b[0].instrumentOf == 0);
    CHECK(soundOwner(b, b[0]) == nullptr);
    CHECK(playbackGain(b[0], b) == Catch::Approx(0.501f).margin(0.001));  // it plays again on its own
    removeTrack(b, 9);  // out of range: nothing
    CHECK(b.size() == 2);
}

TEST_CASE("T-VST-31: linked track made with the instrument; overdub count", "[track][T-VST-31]")
{
    // MIDI 1 "Keys", an audio track 2 the user made below it, MIDI 3 without instrument.
    std::vector<Track> tracks(3);
    tracks[0].number = 1;
    tracks[0].name = "Keys";
    tracks[0].kind = TrackKind::Midi;
    tracks[1].number = 2;
    tracks[1].name = "Guitar";
    tracks[1].kind = TrackKind::Audio;
    tracks[2].number = 3;
    tracks[2].kind = TrackKind::Midi;

    // Made right below its MIDI track, before the user's audio track; silent (Sound from instrument).
    tracks[0].soundFromRecording = true;  // not possible without a linked track; reset when it is made
    const int n = addLinkedSoundTrack(tracks, 1);
    REQUIRE(tracks.size() == 4);
    CHECK(n == 4);
    CHECK(tracks[1].number == 4);
    CHECK(tracks[1].name == "Keys sound");
    CHECK(tracks[1].kind == TrackKind::Audio);
    CHECK(tracks[1].instrumentOf == 1);
    CHECK(tracks[1].take.empty());
    CHECK_FALSE(tracks[0].soundFromRecording);
    CHECK(tracks[2].name == "Guitar");
    // Only one per MIDI track; none for an audio track or a missing track.
    CHECK(addLinkedSoundTrack(tracks, 1) == 4);
    CHECK(addLinkedSoundTrack(tracks, 2) == 0);
    CHECK(addLinkedSoundTrack(tracks, 9) == 0);
    CHECK(tracks.size() == 4);

    // Instrument taken away before anything was recorded: the empty sound track goes.
    CHECK(removeUnusedLinkedTrack(tracks, 1));
    REQUIRE(tracks.size() == 3);
    CHECK(linkedSoundTrack(tracks, 1) == nullptr);
    CHECK_FALSE(removeUnusedLinkedTrack(tracks, 1));  // none left
    // With a take it stays.
    addLinkedSoundTrack(tracks, 1);
    linkedSoundTrack(tracks, 1)->take = "04-Keys sound.wav";
    CHECK_FALSE(removeUnusedLinkedTrack(tracks, 1));
    CHECK(tracks.size() == 4);

    // Overdub count: tracks 1 (MIDI take), 4 (its sound), 2 (audio), 3 (MIDI, no take).
    tracks[0].take = "01-Keys.mid";
    tracks[2].take = "02-Guitar.wav";
    CHECK(overdubTrackCount(tracks) == 2);  // Keys (instrument plays it) and Guitar; the sound track is silent
    tracks[0].soundFromRecording = true;
    CHECK(overdubTrackCount(tracks) == 3);  // the sound track plays its recording too
    tracks[0].armed = true;                 // re-record Keys: neither it nor its old sound plays
    CHECK(overdubTrackCount(tracks) == 1);
    tracks[2].armed = true;
    CHECK(overdubTrackCount(tracks) == 0);
    tracks[0].armed = false;
    CHECK(overdubTrackCount(tracks) == 2);
}
