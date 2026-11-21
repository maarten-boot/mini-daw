#pragma once
// Compact recordings (P31, SPEC-REC-10): a take's WAV file becomes FLAC
// (lossless, about half the size). Recording itself still writes WAV; the
// conversion comes afterwards, one file at a time in the background.
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace minidaw {

// The WAV take files ("NN-name.wav", any case) in a recording folder, and in
// its auto record parts and subfolders when `recursive` (not in "deleted").
std::vector<std::filesystem::path> wavTakeFiles(const std::filesystem::path& folder, bool recursive);

// The same file named two ways (relative / absolute, "..", on Windows in
// another case)? Also when the file is gone (crosscheck #27 CC5).
bool sameTakePath(const std::filesystem::path& a, const std::filesystem::path& b);

struct CompactFileResult {
    bool ok = false;           // the FLAC is there and the WAV removed
    bool skipped = false;      // nothing done (float WAV, a FLAC of that name is there already, ...)
    std::filesystem::path wav;
    std::filesystem::path flac;
    std::string error;         // or why it was skipped
    std::uintmax_t before = 0; // bytes
    std::uintmax_t after = 0;
};

// Converts `wav` into "<stem>.flac" beside it: written as ".<stem>.flac.part",
// decoded again and compared with the WAV sample by sample, renamed, then the
// WAV removed. If the WAV cannot be removed (in use), the FLAC goes again and
// the WAV stays. A float WAV is skipped (FLAC holds integers only), as is a
// WAV format mini-daw does not read. `cancel` stops it (nothing changed). A
// ".*.flac.part" left in the folder by an earlier crash is removed first.
// The FLAC is on the disk (synced) before the WAV is removed.
CompactFileResult compactFile(const std::filesystem::path& wav, const std::atomic<bool>* cancel = nullptr);

// Converts queued files one after another in its own thread. The GUI pauses
// it while files are in use (recording, playing, rendering, exporting): a
// file already started is finished, the next waits.
class Compactor {
public:
    Compactor() = default;
    ~Compactor();
    Compactor(const Compactor&) = delete;
    Compactor& operator=(const Compactor&) = delete;

    // Queues files (a file already queued is not added twice).
    void add(const std::vector<std::filesystem::path>& wavFiles);
    void setPaused(bool paused);
    bool paused() const;
    std::size_t pending() const;    // queued, not yet started
    bool working() const;           // a file is being converted
    std::string current() const;    // its name
    // The results since the last call (GUI thread: update the tracks' takes).
    std::vector<CompactFileResult> takeResults();
    // Stops the thread; a file being converted is given up (nothing changed).
    void stop();

private:
    void run();

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::filesystem::path> queue_;
    std::vector<CompactFileResult> results_;
    std::string current_;           // its name, shown
    std::filesystem::path currentPath_;  // the whole path, for "already queued" (crosscheck #27 CC7)
    bool paused_ = false;
    bool working_ = false;
    bool stopping_ = false;
    std::atomic<bool> cancel_{false};
    std::thread worker_;
};

} // namespace minidaw
