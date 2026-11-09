#pragma once

// The run loop plugin editors use on Linux (P24, SPEC-VST-05b). A VST3 editor
// on Linux has no event loop of its own: it asks the host (IRunLoop, from the
// view's frame) to call it when a file descriptor can be read (its X11
// connection) and at a timer interval. mini-daw's GUI thread calls poll() once
// per frame (about 60 times a second, 20 while minimised).
//
// This class is the bookkeeping, apart from the VST3 types: handlers are
// callbacks with an owner tag, so all of one editor's can be removed when it
// closes. A callback may add or remove handlers (also itself) while it runs.
// GUI thread only.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace minidaw {

class RunLoop {
public:
    using Clock = std::chrono::steady_clock;
    using Id = std::uint64_t;  // 0 = none

    // Called when `fd` can be read (or has an error or hung up). POSIX only:
    // on Windows the handler is kept but never called.
    Id addReader(int fd, std::function<void()> callback, const void* owner);
    // Called every `interval` (at least 1 ms), from the first poll() that is
    // `interval` or more after it was added. A poll that is late calls it once
    // (no catching up); the next time is then `interval` after that poll.
    Id addTimer(std::chrono::milliseconds interval, std::function<void()> callback, const void* owner,
                Clock::time_point now = Clock::now());
    // False if `id` is not (or no longer) registered.
    bool remove(Id id);
    // Removes every handler of `owner`; returns how many.
    std::size_t removeOwner(const void* owner);

    // Calls the readers whose descriptor is ready, then the timers that are due.
    // Returns how many callbacks ran.
    std::size_t poll(Clock::time_point now = Clock::now());

    std::size_t readerCount() const;
    std::size_t timerCount() const;

private:
    struct Entry {
        Id id = 0;
        bool timer = false;
        int fd = -1;
        Clock::duration interval{};
        Clock::time_point due{};
        std::function<void()> callback;
        const void* owner = nullptr;
    };
    std::vector<Entry> entries_;
    Id next_ = 1;
};

// The run loop of mini-daw's editors (one for the process).
RunLoop& editorRunLoop();

} // namespace minidaw
