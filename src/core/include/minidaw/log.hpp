#pragma once

// Diagnostics log (SPEC-LOG-01): a file `mini-daw.log` next to the config
// file, started fresh at each run; the previous run is kept as
// `mini-daw.old.log`. The latest lines are also kept in memory for the
// View -> Log window.
//
// Thread-safe (a mutex), so never call it from the audio callback
// (SPEC-NF-01). Each line is flushed at once, so the file is complete up to
// a crash; it is not forced to the disk.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace minidaw {

enum class LogLevel { Info, Warning, Error };

// "INFO", "WARNING", "ERROR": the level is always written as text (SPEC-UI-07).
std::string_view logLevelName(LogLevel level);

struct LogEntry {
    std::string time;    // local time, "2026-11-02 21:05:03.123"
    LogLevel level = LogLevel::Info;
    std::string text;
};

class Log {
public:
    static constexpr std::size_t kMemoryLines = 2000;
    static constexpr std::uintmax_t kMaxFileBytes = 5u * 1024 * 1024;  // then the file stops growing

    Log() = default;
    ~Log();
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    // Renames an existing `file` to the .old.log name and starts a new one.
    // Returns an error text, empty on success; without a file the log still
    // works in memory.
    std::string open(const std::filesystem::path& file);
    void close();

    void write(LogLevel level, std::string_view text);
    void info(std::string_view text) { write(LogLevel::Info, text); }
    void warning(std::string_view text) { write(LogLevel::Warning, text); }
    void error(std::string_view text) { write(LogLevel::Error, text); }

    // Copy of the lines kept in memory, oldest first.
    std::vector<LogEntry> recent() const;
    // Grows with every line written (to see whether the window must refresh).
    std::uint64_t lineCount() const;
    std::uint64_t warningCount() const;
    std::uint64_t errorCount() const;
    std::filesystem::path file() const;

    // "mini-daw.log" -> "mini-daw.old.log".
    static std::filesystem::path oldFileFor(const std::filesystem::path& file);
    // One line as written to the file: "<time> WARNING <text>"; line breaks
    // inside the text are indented so each entry stays recognisable.
    static std::string formatLine(const LogEntry& entry);

private:
    mutable std::mutex mutex_;
    std::FILE* fp_ = nullptr;
    std::filesystem::path file_;
    std::uintmax_t fileBytes_ = 0;
    bool fileFull_ = false;
    std::vector<LogEntry> lines_;    // ring of kMemoryLines
    std::size_t next_ = 0;           // ring position once full
    std::uint64_t count_ = 0;
    std::uint64_t warnings_ = 0;
    std::uint64_t errors_ = 0;
};

// Notes shown in the GUI (e.g. audio driver messages; crosscheck #13, U8): a note
// already in the list is counted ("… (3 times)") instead of added again; when the
// list is full a last line points to the log, where every message is kept.
inline constexpr std::size_t kMaxNotes = 50;
inline constexpr std::string_view kMoreNotesInLog = "More notes: see View > Log.";
void addNote(std::vector<std::string>& notes, const std::string& note, std::size_t maxNotes = kMaxNotes);

} // namespace minidaw
