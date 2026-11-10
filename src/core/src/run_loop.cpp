#include "minidaw/run_loop.hpp"

#include <algorithm>

#if !defined(_WIN32)
#include <poll.h>
#endif

namespace minidaw {

RunLoop::Id RunLoop::addReader(int fd, std::function<void()> callback, const void* owner)
{
    if (fd < 0 || !callback) return 0;
    Entry e;
    e.id = next_++;
    e.fd = fd;
    e.callback = std::move(callback);
    e.owner = owner;
    entries_.push_back(std::move(e));
    return entries_.back().id;
}

RunLoop::Id RunLoop::addTimer(std::chrono::milliseconds interval, std::function<void()> callback, const void* owner,
                              Clock::time_point now)
{
    if (!callback) return 0;
    Entry e;
    e.id = next_++;
    e.timer = true;
    e.interval = std::max(interval, std::chrono::milliseconds(1));
    e.due = now + e.interval;
    e.callback = std::move(callback);
    e.owner = owner;
    entries_.push_back(std::move(e));
    return entries_.back().id;
}

// Removing a handler can destroy the last reference to a plugin object, whose
// destructor may remove handlers too (Z2). So the entries are moved out and
// the list is made consistent first; they are destroyed only after that, at
// the end of these functions.

bool RunLoop::remove(Id id)
{
    const auto it = std::find_if(entries_.begin(), entries_.end(), [id](const Entry& e) { return e.id == id; });
    if (it == entries_.end()) return false;
    const Entry gone = std::move(*it);
    entries_.erase(it);
    return true;  // `gone` is destroyed here
}

std::size_t RunLoop::removeOwner(const void* owner)
{
    std::vector<Entry> gone;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->owner == owner) {
            gone.push_back(std::move(*it));
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
    return gone.size();  // `gone` is destroyed after the count is taken
}

std::size_t RunLoop::poll(Clock::time_point now)
{
    // Work on a copy of the ids: a callback may change the list. Each callback
    // is copied before it runs, so removing itself does not destroy it mid-call.
    std::size_t calls = 0;
#if !defined(_WIN32)
    std::vector<pollfd> fds;
    std::vector<Id> readers;
    for (const Entry& e : entries_) {
        if (e.timer) continue;
        fds.push_back(pollfd{e.fd, POLLIN, 0});
        readers.push_back(e.id);
    }
    if (!fds.empty() && ::poll(fds.data(), static_cast<nfds_t>(fds.size()), 0) > 0) {
        for (std::size_t i = 0; i < fds.size(); ++i) {
            if ((fds[i].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) == 0) continue;
            const auto it = std::find_if(entries_.begin(), entries_.end(),
                                         [&](const Entry& e) { return e.id == readers[i]; });
            if (it == entries_.end()) continue;  // removed by an earlier callback
            if (fds[i].revents & POLLNVAL) {
                // Not an open descriptor: its owner closed it without removing
                // the handler (Z1). Calling it would repeat every poll, and once
                // the number is reused it would be about someone else's file.
                remove(readers[i]);
                ++dropped_;
                continue;
            }
            const std::function<void()> callback = it->callback;
            callback();
            ++calls;
        }
    }
#endif
    std::vector<Id> due;
    for (const Entry& e : entries_) {
        if (e.timer && e.due <= now) due.push_back(e.id);
    }
    for (const Id id : due) {
        const auto it = std::find_if(entries_.begin(), entries_.end(), [id](const Entry& e) { return e.id == id; });
        if (it == entries_.end()) continue;
        it->due = now + it->interval;  // before the call: it may remove itself
        const std::function<void()> callback = it->callback;
        callback();
        ++calls;
    }
    return calls;
}

std::size_t RunLoop::readerCount() const
{
    return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return !e.timer; }));
}

std::size_t RunLoop::timerCount() const
{
    return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.timer; }));
}

RunLoop& editorRunLoop()
{
    static RunLoop loop;
    return loop;
}

} // namespace minidaw
