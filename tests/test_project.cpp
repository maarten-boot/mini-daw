// Projects (SPEC-PRJ-01, 02, 02a).

#include "minidaw/config.hpp"
#include "minidaw/path_util.hpp"
#include "minidaw/project.hpp"
#include "minidaw/wav.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace minidaw;
namespace fs = std::filesystem;

namespace {

fs::path makeWav(const fs::path& p, unsigned int frames = 100)
{
    fs::create_directories(p.parent_path());
    WavWriter w;
    std::string err;
    REQUIRE(w.open(p, 48000, 1, err));
    std::vector<float> zeros(frames, 0.0f);
    REQUIRE(w.write(zeros.data(), frames, err));
    REQUIRE(w.close(err));
    return p;
}

std::vector<Track> sampleTracks(const fs::path& project)
{
    Track a;
    a.number = 1;
    a.name = "Gitaar \xc3\xa9\xc3\xa8 \xe2\x99\xaa";  // non-ASCII, as UTF-8
    a.channels = {0, 1};
    a.armed = true;
    a.monitor = true;
    a.volumeDb = -3.5f;
    a.solo = true;
    a.take = project / "takes" / "take-1" / pathFromUtf8("01-Gitaar \xc3\xa9.wav");
    a.takeStart = -1024;
    Track b;
    b.number = 3;
    b.name = "Keys";
    b.kind = TrackKind::Midi;
    b.midiPort = "Keystation 0";
    b.midiChannel = 2;
    b.midiOut = "loopMIDI Port 1";
    b.mute = true;
    return {a, b};
}

} // namespace

TEST_CASE("T-PRJ-01: a project is saved and opened again with all track settings", "[project][T-PRJ-01]")
{
    test::TempDir dir;
    const fs::path project = dir.path() / pathFromUtf8("Opnamen \xc3\xbc");
    std::string err;
    REQUIRE(createProjectFolder(project, err));
    CHECK(fs::is_directory(project / "takes"));
    ProjectData data;
    data.tracks = sampleTracks(project);
    data.sampleRate = 48000;
    makeWav(data.tracks[0].take);
    REQUIRE(saveProject(project, data, err));
    CHECK(isProjectFolder(project));
    CHECK_FALSE(fs::exists(project / "project.json.tmp"));

    // The take path is stored relative, with '/'.
    std::string text;
    {
        // Closed again before the folder is moved: Windows cannot move or replace an open file.
        std::ifstream in(project / kProjectFileName, std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    CHECK(text.find("\"takes/take-1/01-Gitaar \xc3\xa9.wav\"") != std::string::npos);

    // The folder can be moved: it still opens, with every setting.
    const fs::path moved = dir.path() / "elsewhere";
    fs::rename(project, moved);
    ProjectData back;
    std::vector<std::string> warnings;
    REQUIRE(loadProject(moved, back, warnings, err));
    CHECK(warnings.empty());
    CHECK(back.sampleRate == 48000);
    REQUIRE(back.tracks.size() == 2);
    const Track& a = back.tracks[0];
    CHECK(a.name == "Gitaar \xc3\xa9\xc3\xa8 \xe2\x99\xaa");
    CHECK(a.channels == std::vector<unsigned int>{0, 1});
    CHECK(a.armed);
    CHECK(a.monitor);
    CHECK(a.volumeDb == -3.5f);
    CHECK(a.solo);
    CHECK(a.takeStart == -1024);
    CHECK(fs::exists(a.take));
    CHECK(isInsideFolder(a.take, moved));
    const Track& b = back.tracks[1];
    CHECK(b.kind == TrackKind::Midi);
    CHECK(b.number == 3);
    CHECK(b.midiPort == "Keystation 0");
    CHECK(b.midiChannel == 2);
    CHECK(b.midiOut == "loopMIDI Port 1");
    CHECK(b.mute);
    CHECK(b.take.empty());

    // Saving again replaces the file in one step.
    back.tracks[1].name = "Keys 2";
    REQUIRE(saveProject(moved, back, err));
    REQUIRE(loadProject(moved, back, warnings, err));
    CHECK(back.tracks[1].name == "Keys 2");

    // A folder that already holds a project is not created over.
    CHECK_FALSE(createProjectFolder(moved, err));
    CHECK(err.find("already holds a project") != std::string::npos);
}

TEST_CASE("T-PRJ-02/03: missing takes, damaged or unknown project files", "[project][T-PRJ-02][T-PRJ-03]")
{
    ProjectData d;
    std::vector<std::string> w;
    std::string err;
    CHECK_FALSE(projectFromJson("{not json", "p", d, w, err));
    CHECK(err.find("not valid JSON") != std::string::npos);
    CHECK_FALSE(projectFromJson(R"({"format":"something else","version":1,"tracks":[]})", "p", d, w, err));
    CHECK_FALSE(projectFromJson(R"({"format":"mini-daw project","version":99,"tracks":[]})", "p", d, w, err));
    CHECK(err.find("newer") != std::string::npos);
    CHECK_FALSE(projectFromJson(R"({"format":"mini-daw project","version":1})", "p", d, w, err));
    // Wrong types for the header fields: an error, never an exception (crosscheck #7, N1).
    for (const char* bad : {R"({"format":"mini-daw project","version":"1","tracks":[]})",
                            R"({"format":5,"version":1,"tracks":[]})",
                            R"({"format":"mini-daw project","version":1.5,"tracks":[]})",
                            R"({"format":"mini-daw project","version":1,"tracks":{"a":1}})",
                            R"({"format":"mini-daw project","version":1,"sampleRate":-5,"tracks":[]})",
                            R"([1,2,3])", ""}) {
        CHECK_NOTHROW(projectFromJson(bad, "p", d, w, err));
    }
    CHECK_FALSE(projectFromJson(R"({"format":"mini-daw project","version":"1","tracks":[]})", "p", d, w, err));

    // Bad fields: warnings, the rest is loaded.
    const std::string text = R"({"format":"mini-daw project","version":1,"tracks":[
        {"number":1,"name":"A","kind":"audio","volumeDb":"loud","inputChannels":[0,1,2,3]},
        {"number":1,"name":"B","kind":"midi","midiChannel":40},
        {"number":7,"name":"E","kind":"audio","inputChannels":[-1]},
        {"number":2,"name":"C","kind":"video"},
        42,
        {"number":5,"name":"D","kind":"audio","take":"takes/gone.wav"}]})";
    w.clear();
    REQUIRE(projectFromJson(text, "p", d, w, err));
    REQUIRE(d.tracks.size() == 4);
    CHECK(d.tracks[0].volumeDb == 0.0f);                              // wrong type: default
    CHECK(d.tracks[0].channels == std::vector<unsigned int>{0});      // too many channels: input 1, warned
    CHECK(d.tracks[1].number == 2);                                   // duplicate number renumbered
    CHECK(d.tracks[1].midiChannel == 0);                              // out of range: all channels, warned (N7)
    CHECK(d.tracks[2].channels == std::vector<unsigned int>{0});      // negative channel: not wrapped (N7)
    CHECK(d.tracks[3].take.filename() == "gone.wav");                 // kept, but reported missing
    // volume, channels, MIDI channel, duplicate number, negative channel, unknown kind, not an object, missing take
    CHECK(w.size() == 8);
    CHECK_FALSE(loadProject("/no/such/folder", d, w, err));
}

TEST_CASE("T-PRJ-04: takes outside the project are copied into it", "[project][T-PRJ-04]")
{
    test::TempDir dir;
    const fs::path project = dir.path() / "P";
    std::string err;
    REQUIRE(createProjectFolder(project, err));
    const fs::path outside = makeWav(dir.path() / "rec" / "take-20261101-090000" / "01-Old.wav", 500);
    std::vector<Track> tracks(3);
    tracks[0].number = 1;
    tracks[0].take = outside;
    tracks[1].number = 2;
    tracks[1].take = makeWav(project / "takes" / "take-x" / "02-In.wav");  // already inside: untouched
    tracks[2].number = 3;
    tracks[2].take = dir.path() / "rec" / "missing.wav";
    auto r = importTakes(project, tracks);
    REQUIRE(r.copied.size() == 1);
    REQUIRE(r.missing.size() == 1);
    CHECK(r.failed.empty());
    CHECK(tracks[0].take == project / "takes" / "imported" / "take-20261101-090000" / "01-Old.wav");
    CHECK(sameFileContent(tracks[0].take, outside));
    CHECK(tracks[1].take == project / "takes" / "take-x" / "02-In.wav");
    CHECK(tracks[2].take == dir.path() / "rec" / "missing.wav");  // keeps its path
    // Importing again finds the copy (nothing copied twice).
    tracks[0].take = outside;
    r = importTakes(project, tracks);
    CHECK(r.copied.empty());
    CHECK(isInsideFolder(tracks[0].take, project));
    CHECK_FALSE(isInsideFolder(project, project));  // the folder itself is not "inside" (N8)

    CHECK(listProjects(dir.path()).empty());  // a folder without project.json is not a project yet
    REQUIRE(saveProject(project, ProjectData{}, err));
    CHECK(listProjects(dir.path()) == std::vector<fs::path>{project});
}

TEST_CASE("T-PRJ-07: copying takes in never mixes up or overwrites takes", "[project][T-PRJ-07]")
{
    test::TempDir dir;
    const fs::path project = dir.path() / "P";
    std::string err;
    REQUIRE(createProjectFolder(project, err));
    // Two different takes with the same take folder name and file name, same size (N3).
    const fs::path a = makeWav(dir.path() / "A" / "takes" / "take-1" / "01-Gitaar.wav", 300);
    const fs::path b = dir.path() / "B" / "takes" / "take-1" / "01-Gitaar.wav";
    makeWav(b, 300);
    {
        std::fstream f(b, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(-1, std::ios::end);
        f.put('\x7f');  // same size, other content
    }
    REQUIRE_FALSE(sameFileContent(a, b));
    std::vector<Track> tracks(3);
    tracks[0].number = 1;
    tracks[0].take = a;
    tracks[1].number = 2;
    tracks[1].take = b;
    tracks[2].number = 3;
    tracks[2].take = a;  // the same take twice: one copy
    const auto r = importTakes(project, tracks);
    CHECK(r.failed.empty());
    CHECK(r.copied.size() == 2);
    const fs::path dirIn = project / "takes" / "imported" / "take-1";
    CHECK(tracks[0].take == dirIn / "01-Gitaar.wav");
    CHECK(tracks[1].take == dirIn / "01-Gitaar-2.wav");
    CHECK(tracks[2].take == tracks[0].take);
    CHECK(sameFileContent(tracks[0].take, a));
    CHECK(sameFileContent(tracks[1].take, b));

    // A copy that cannot be made (a file where the folder should be): reported, path kept,
    // nothing else touched (N4).
    const fs::path c = makeWav(dir.path() / "C" / "take-9" / "05-X.wav");
    std::ofstream(project / "takes" / "imported" / "take-9") << "a file, not a folder";
    std::vector<Track> more(1);
    more[0].number = 5;
    more[0].take = c;
    const auto r2 = importTakes(project, more);
    CHECK(r2.failed.size() == 1);
    CHECK(more[0].take == c);
    CHECK(sameFileContent(dirIn / "01-Gitaar.wav", a));  // earlier copies untouched
}

TEST_CASE("T-PRJ-08: names that are not valid UTF-8 are saved, not a crash", "[project][T-PRJ-08]")
{
    test::TempDir dir;
    ProjectData p;
    Track t;
    t.name = "Pr\xfc" "fung";  // Latin-1, not UTF-8 (N2)
    t.kind = TrackKind::Midi;
    t.midiPort = "Port \xff";
    p.tracks.push_back(t);
    std::string text;
    REQUIRE_NOTHROW(text = projectToJson(p, dir.path()));
    std::string err;
    REQUIRE(saveProject(dir.path(), p, err));
    ProjectData back;
    std::vector<std::string> w;
    REQUIRE(loadProject(dir.path(), back, w, err));
    REQUIRE(back.tracks.size() == 1);
    CHECK(back.tracks[0].name == "Pr\xef\xbf\xbd" "fung");  // the bad byte became U+FFFD
}

TEST_CASE("T-PRJ-05: last and recent projects in the config", "[project][config][T-PRJ-05]")
{
    ProjectSettings s;
    for (int i = 0; i < 10; ++i) rememberProject(s, "p" + std::to_string(i));
    rememberProject(s, "p5");  // again: moves to the front, no duplicate
    REQUIRE(s.recent.size() == kMaxRecentProjects);
    CHECK(s.recent[0] == "p5");
    CHECK(s.recent[1] == "p9");
    CHECK(std::count(s.recent.begin(), s.recent.end(), "p5") == 1);

    test::TempDir dir;
    AppConfig cfg;
    cfg.project.last = "D:\\Music\\mini-daw\\projects\\Jam \"live\"";
    cfg.project.recent = {cfg.project.last, "E:\\x\\"};
    REQUIRE(saveConfigFile(dir.path() / "c.ini", cfg).empty());
    const auto back = loadConfigFile(dir.path() / "c.ini");
    CHECK(back.warnings.empty());
    CHECK(back.config.project == cfg.project);
    CHECK(parseConfig(defaultConfigText()).config.project.last.empty());
}

TEST_CASE("T-VST-15: instrument and its state saved in the project; base64", "[project][vst][T-VST-15]")
{
    std::string out;
    for (const std::string& s : {std::string(), std::string("a"), std::string("ab"), std::string("abc"),
                                std::string("\0\xff\x10 binary\x80", 12)}) {
        REQUIRE(base64Decode(base64Encode(s), out));
        CHECK(out == s);
    }
    CHECK(base64Encode("Man") == "TWFu");
    CHECK(base64Encode("Ma") == "TWE=");
    CHECK_FALSE(base64Decode("abc", out));    // not a multiple of 4
    CHECK_FALSE(base64Decode("ab!d", out));   // bad character

    test::TempDir dir;
    std::string err;
    REQUIRE(createProjectFolder(dir.path() / "P", err));
    ProjectData d;
    Track t;
    t.kind = TrackKind::Midi;
    t.instrumentClass = "0123456789ABCDEF0123456789ABCDEF";
    t.instrumentModule = "C:/Program Files/Common Files/VST3/Synth.vst3";
    t.instrumentName = "Synth";
    t.instrumentState = std::string("\x01\x02\x00\xfe", 4);
    t.instrumentControllerState = "ctrl";
    d.tracks.push_back(t);
    REQUIRE(saveProject(dir.path() / "P", d, err));
    ProjectData back;
    std::vector<std::string> w;
    REQUIRE(loadProject(dir.path() / "P", back, w, err));
    CHECK(w.empty());
    REQUIRE(back.tracks.size() == 1);
    CHECK(back.tracks[0].instrumentClass == t.instrumentClass);
    CHECK(back.tracks[0].instrumentModule == t.instrumentModule);
    CHECK(back.tracks[0].instrumentState == t.instrumentState);
    CHECK(back.tracks[0].instrumentControllerState == "ctrl");

    // A damaged state: a warning, the instrument stays (with its defaults).
    const std::string text = R"({"format":"mini-daw project","version":1,"tracks":[{"number":1,"name":"K","kind":"midi",
        "instrument":{"classId":"0123456789ABCDEF0123456789ABCDEF","state":"#bad"}}]})";
    REQUIRE(projectFromJson(text, dir.path(), back, w, err));
    CHECK(back.tracks[0].instrumentClass.size() == 32);
    CHECK(back.tracks[0].instrumentState.empty());
    CHECK(w.size() == 1);

    // Wrong-typed fields: warnings for that track, the project still opens (crosscheck #9, Q8).
    const std::string typed = R"({"format":"mini-daw project","version":1,"tracks":[{"number":1,"name":"K","kind":"midi",
        "instrument":{"classId":"0123456789ABCDEF0123456789ABCDEF","module":42,"name":["x"],"state":7}}]})";
    w.clear();
    REQUIRE(projectFromJson(typed, dir.path(), back, w, err));
    INFO(err);
    REQUIRE(back.tracks.size() == 1);
    CHECK(back.tracks[0].instrumentClass.size() == 32);
    CHECK(back.tracks[0].instrumentModule.empty());
    CHECK(back.tracks[0].instrumentState.empty());
    CHECK(w.size() == 2);  // module/name, and the state

    // Extra instrument folders in the config.
    AppConfig cfg;
    cfg.instruments.folders = {"D:\\VST3 extra", "/opt/vst3"};
    REQUIRE(saveConfigFile(dir.path() / "c.ini", cfg).empty());
    CHECK(loadConfigFile(dir.path() / "c.ini").config.instruments == cfg.instruments);
}

TEST_CASE("T-VST-27: the linked sound track and Sound from are kept in the project; bad links are repaired",
          "[project][vst][T-VST-27]")
{
    test::TempDir dir;
    ProjectData data;
    Track m;
    m.number = 1;
    m.kind = TrackKind::Midi;
    m.name = "Keys";
    m.instrumentClass = "0123456789ABCDEF0123456789ABCDEF";
    m.soundFromRecording = true;
    Track l;
    l.number = 2;
    l.kind = TrackKind::Audio;
    l.name = "Keys sound";
    l.channels = {0};
    l.instrumentOf = 1;
    data.tracks = {m, l};
    std::vector<std::string> w;
    std::string err;
    ProjectData back;
    REQUIRE(projectFromJson(projectToJson(data, dir.path()), dir.path(), back, w, err));
    CHECK(w.empty());
    REQUIRE(back.tracks.size() == 2);
    CHECK(back.tracks[0].soundFromRecording);
    CHECK(back.tracks[1].instrumentOf == 1);

    // A hand-edited linked track that is armed and monitored: never on its own (R9).
    data.tracks[1].armed = true;
    data.tracks[1].monitor = true;
    REQUIRE(projectFromJson(projectToJson(data, dir.path()), dir.path(), back, w, err));
    CHECK_FALSE(back.tracks[1].armed);
    CHECK_FALSE(back.tracks[1].monitor);

    // A link to a missing track, a second link to the same track, a bad Sound from value.
    const std::string bad = R"({"format":"mini-daw project","version":1,"tracks":[
        {"number":1,"name":"K","kind":"midi","soundFrom":"loud"},
        {"number":2,"name":"A","kind":"audio","instrumentOf":1},
        {"number":3,"name":"B","kind":"audio","instrumentOf":1},
        {"number":4,"name":"C","kind":"audio","instrumentOf":9},
        {"number":5,"name":"D","kind":"midi","soundFrom":"recording"}]})";
    w.clear();
    REQUIRE(projectFromJson(bad, dir.path(), back, w, err));
    REQUIRE(back.tracks.size() == 5);
    CHECK_FALSE(back.tracks[0].soundFromRecording);  // "loud": instrument
    CHECK(back.tracks[1].instrumentOf == 1);         // the first link stays
    CHECK(back.tracks[2].instrumentOf == 0);         // a second one becomes a normal audio track
    CHECK(back.tracks[3].instrumentOf == 0);         // track 9 does not exist
    CHECK_FALSE(back.tracks[4].soundFromRecording);  // nothing recorded to play
    CHECK(w.size() == 3);
}

