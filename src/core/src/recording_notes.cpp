#include "minidaw/recording_notes.hpp"

#include "minidaw/path_util.hpp"
#include "minidaw/presets.hpp"  // readWholeFile, writeFileReplacing

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <system_error>

namespace minidaw {
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
constexpr const char* kFormat = "mini-daw recording";
constexpr int kVersion = 1;
} // namespace

bool loadRecordingNotes(const fs::path& folder, RecordingNotes& notes, std::string& error)
{
    notes = RecordingNotes{};
    const fs::path p = folder / kRecordingNotesFile;
    std::error_code ec;
    if (!fs::exists(p, ec)) return true;
    std::string bytes;
    if (!readWholeFile(p, bytes, error)) return false;
    try {
        const json j = json::parse(bytes);
        if (!j.is_object() || j.value("format", std::string()) != kFormat) {
            error = pathToUtf8(p) + " is not a mini-daw recording file";
            return false;
        }
        if (j.contains("version") && j["version"].is_number_integer() && j["version"].get<int>() > kVersion) {
            error = pathToUtf8(p) + " is from a newer mini-daw";
            return false;
        }
        if (j.contains("name") && j["name"].is_string()) notes.name = j["name"].get<std::string>();
        if (j.contains("note") && j["note"].is_string()) notes.note = j["note"].get<std::string>();
        if (j.contains("marks") && j["marks"].is_array()) {
            for (const auto& m : j["marks"]) {
                if (!m.is_object() || !m.contains("seconds") || !m["seconds"].is_number()) continue;
                RecordingMark mark;
                mark.seconds = m["seconds"].get<double>();
                if (!std::isfinite(mark.seconds) || mark.seconds < 0) continue;
                if (m.contains("text") && m["text"].is_string()) mark.text = m["text"].get<std::string>();
                notes.marks.push_back(std::move(mark));
            }
        }
        return true;
    } catch (const std::exception& e) {
        notes = RecordingNotes{};
        error = pathToUtf8(p) + " is damaged: " + e.what();
        return false;
    }
}

bool saveRecordingNotes(const fs::path& folder, const RecordingNotes& notes, std::string& error)
{
    const fs::path p = folder / kRecordingNotesFile;
    if (notes.empty()) {
        std::error_code ec;
        fs::remove(p, ec);
        if (ec) {
            error = "cannot remove " + pathToUtf8(p) + ": " + ec.message();
            return false;
        }
        return true;
    }
    json j = {{"format", kFormat}, {"version", kVersion}};
    if (!notes.name.empty()) j["name"] = notes.name;
    if (!notes.note.empty()) j["note"] = notes.note;
    if (!notes.marks.empty()) {
        json marks = json::array();
        for (const auto& m : notes.marks) marks.push_back({{"seconds", m.seconds}, {"text", m.text}});
        j["marks"] = marks;
    }
    // Names that are not valid UTF-8 are written with U+FFFD (as project.json).
    const std::string text = j.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
    return writeFileReplacing(p, text, error);
}

bool addRecordingMark(const fs::path& folder, const RecordingMark& mark, std::string& error)
{
    RecordingNotes notes;
    if (!loadRecordingNotes(folder, notes, error)) return false;  // never overwrite what could not be read
    const auto at = std::upper_bound(notes.marks.begin(), notes.marks.end(), mark.seconds,
                                     [](double s, const RecordingMark& m) { return s < m.seconds; });
    notes.marks.insert(at, mark);
    return saveRecordingNotes(folder, notes, error);
}

bool moveRecordingToDeleted(const fs::path& takesFolder, const fs::path& folder, fs::path& movedTo,
                            std::string& error)
{
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        error = pathToUtf8(folder) + " is not there any more";
        return false;
    }
    const fs::path parent = folder.parent_path();
    const bool autoPart = parent.filename() == "auto" && parent.parent_path() == takesFolder;
    if (parent != takesFolder && !autoPart) {
        error = pathToUtf8(folder) + " is not a recording in " + pathToUtf8(takesFolder);
        return false;
    }
    const fs::path target = takesFolder / "deleted" / (autoPart ? fs::path("auto") : fs::path());
    fs::create_directories(target, ec);
    if (ec) {
        error = "cannot make " + pathToUtf8(target) + ": " + ec.message();
        return false;
    }
    fs::path to = target / folder.filename();
    for (int n = 2; fs::exists(to, ec) && n < 1000; ++n) {
        to = target / (pathToUtf8(folder.filename()) + "-" + std::to_string(n));
    }
    fs::rename(folder, to, ec);
    if (ec) {
        error = "cannot move " + pathToUtf8(folder) + " to " + pathToUtf8(to) + ": " + ec.message() +
                " (is a file of it open in another program?)";
        return false;
    }
    movedTo = to;
    return true;
}

} // namespace minidaw
