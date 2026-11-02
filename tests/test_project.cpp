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

    // Bad fields: warnings, the rest is loaded.
    const std::string text = R"({"format":"mini-daw project","version":1,"tracks":[
        {"number":1,"name":"A","kind":"audio","volumeDb":"loud","inputChannels":[0,1,2,3]},
        {"number":1,"name":"B","kind":"midi","midiChannel":40},
        {"number":2,"name":"C","kind":"video"},
        42,
        {"number":5,"name":"D","kind":"audio","take":"takes/gone.wav"}]})";
    w.clear();
    REQUIRE(projectFromJson(text, "p", d, w, err));
    REQUIRE(d.tracks.size() == 3);
    CHECK(d.tracks[0].volumeDb == 0.0f);                              // wrong type: default
    CHECK(d.tracks[0].channels == std::vector<unsigned int>{0});      // too many channels: default
    CHECK(d.tracks[1].number == 2);                                   // duplicate number renumbered
    CHECK(d.tracks[1].midiChannel == 16);                             // clamped
    CHECK(d.tracks[2].take.filename() == "gone.wav");                 // kept, but reported missing
    CHECK(w.size() == 5);  // volume, duplicate number, unknown kind, not an object, missing take
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
    std::vector<std::string> copied;
    const auto errors = importTakes(project, tracks, copied);
    REQUIRE(copied.size() == 1);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].find("missing") != std::string::npos);
    CHECK(tracks[0].take == project / "takes" / "imported" / "take-20261101-090000" / "01-Old.wav");
    CHECK(fs::file_size(tracks[0].take) == fs::file_size(outside));
    CHECK(tracks[1].take == project / "takes" / "take-x" / "02-In.wav");
    CHECK(tracks[2].take == dir.path() / "rec" / "missing.wav");  // keeps its path
    // Importing again finds the copy (nothing copied twice).
    tracks[0].take = outside;
    copied.clear();
    importTakes(project, tracks, copied);
    CHECK(copied.empty());
    CHECK(isInsideFolder(tracks[0].take, project));

    CHECK(listProjects(dir.path()).empty());  // a folder without project.json is not a project yet
    REQUIRE(saveProject(project, ProjectData{}, err));
    CHECK(listProjects(dir.path()) == std::vector<fs::path>{project});
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
