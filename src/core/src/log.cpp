#include "minidaw/log.hpp"

#include "file_util.hpp"
#include "minidaw/path_util.hpp"

#include <chrono>
#include <ctime>
#include <system_error>

namespace minidaw {

namespace {

std::string nowText()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[40];
    const std::size_t n = std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &local);
    std::snprintf(buf + n, sizeof buf - n, ".%03d", static_cast<int>(ms));
    return buf;
}

} // namespace

std::string_view logLevelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Info:    return "INFO";
    case LogLevel::Warning: return "WARNING";
    case LogLevel::Error:   return "ERROR";
    }
    return "INFO";
}

Log::~Log()
{
    close();
}

std::filesystem::path Log::oldFileFor(const std::filesystem::path& file)
{
    std::filesystem::path old = file;
    old.replace_filename(file.stem().native() + std::filesystem::path(".old").native() + file.extension().native());
    return old;
}

std::string Log::formatLine(const LogEntry& e)
{
    std::string line = e.time + " " + std::string(logLevelName(e.level)) + " ";
    for (char c : e.text) {
        if (c == '\r') continue;
        line += c;
        if (c == '\n') line += "    ";
    }
    line += '\n';
    return line;
}

std::string Log::open(const std::filesystem::path& file)
{
    close();
    std::lock_guard<std::mutex> lock(mutex_);
    std::error_code ec;
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path(), ec);
    std::string renameError;
    if (std::filesystem::exists(file, ec)) {
        const auto old = oldFileFor(file);
        std::error_code rmEc;
        std::filesystem::remove(old, rmEc);  // Windows' rename does not replace
        std::error_code mvEc;
        std::filesystem::rename(file, old, mvEc);
        if (mvEc) renameError = mvEc.message();
    }
    fileBytes_ = 0;
    fileFull_ = false;
    // If the previous run could not be moved aside (e.g. another mini-daw has
    // it open), add to it instead of overwriting it (crosscheck #5, K5).
    fp_ = renameError.empty() ? detail::openForWriting(file, false) : detail::openForAppending(file);
    if (!fp_) {
        return "The log file " + pathToUtf8(file) + " could not be " + (renameError.empty() ? "created." : "opened.") +
               (renameError.empty() ? "" : " (The previous log could not be renamed: " + renameError + ")");
    }
    file_ = file;
    if (!renameError.empty()) {
        fileBytes_ = std::filesystem::file_size(file, ec);
        if (ec) fileBytes_ = 0;
        const std::string line = "\n" + nowText() +
                                 " WARNING ---- New run. The previous log could not be renamed to " +
                                 pathToUtf8(oldFileFor(file).filename()) + " (" + renameError +
                                 "), so this run is added to it. ----\n";
        std::fwrite(line.data(), 1, line.size(), fp_);
        std::fflush(fp_);
        fileBytes_ += line.size();
    }
    return {};
}

void Log::close()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (fp_) std::fclose(fp_);
    fp_ = nullptr;
}

void Log::write(LogLevel level, std::string_view text)
{
    LogEntry e{nowText(), level, std::string(text)};
    std::lock_guard<std::mutex> lock(mutex_);
    if (fp_ && !fileFull_) {
        std::string line = formatLine(e);
        if (fileBytes_ + line.size() > kMaxFileBytes) {
            line = e.time + " WARNING The log file is full (5 MB); later lines are only shown in View -> Log.\n";
            fileFull_ = true;
        }
        std::fwrite(line.data(), 1, line.size(), fp_);
        std::fflush(fp_);
        fileBytes_ += line.size();
    }
    ++count_;
    if (level == LogLevel::Warning) ++warnings_;
    if (level == LogLevel::Error) ++errors_;
    if (lines_.size() < kMemoryLines) {
        lines_.push_back(std::move(e));
    } else {
        lines_[next_] = std::move(e);
        next_ = (next_ + 1) % kMemoryLines;
    }
}

std::vector<LogEntry> Log::recent() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LogEntry> out;
    out.reserve(lines_.size());
    for (std::size_t i = 0; i < lines_.size(); ++i) out.push_back(lines_[(next_ + i) % lines_.size()]);
    return out;
}

std::uint64_t Log::lineCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

std::uint64_t Log::warningCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return warnings_;
}

std::uint64_t Log::errorCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return errors_;
}

std::filesystem::path Log::file() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return file_;
}

void addNote(std::vector<std::string>& notes, const std::string& note, std::size_t maxNotes)
{
    const std::string prefix = note + " (";
    const std::string suffix = " times)";
    for (std::string& n : notes) {
        if (n == note) {
            n = note + " (2 times)";
            return;
        }
        if (n.size() > prefix.size() + suffix.size() && n.compare(0, prefix.size(), prefix) == 0 &&
            n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0) {
            const std::string count = n.substr(prefix.size(), n.size() - prefix.size() - suffix.size());
            if (!count.empty() && count.find_first_not_of("0123456789") == std::string::npos && count.size() < 10) {
                n = prefix + std::to_string(std::stoul(count) + 1) + suffix;
                return;
            }
        }
    }
    if (maxNotes == 0) return;
    if (!notes.empty() && notes.back() == kMoreNotesInLog) return;  // full: the rest is in the log
    if (notes.size() + 1 >= maxNotes) {
        notes.emplace_back(kMoreNotesInLog);
        return;
    }
    notes.push_back(note);
}

} // namespace minidaw
