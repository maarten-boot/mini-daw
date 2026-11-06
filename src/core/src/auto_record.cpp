#include "minidaw/auto_record.hpp"

#include <algorithm>
#include <cstdio>

namespace minidaw {

AutoRecordTiming autoRecordTiming(int pauseSeconds, unsigned int sampleRate)
{
    AutoRecordTiming t;
    t.pause = static_cast<std::uint64_t>(std::clamp(pauseSeconds, 1, 60)) * sampleRate;
    t.tail = sampleRate;
    t.preroll = sampleRate;
    t.maxHeldQuiet = 60ull * sampleRate;
    return t;
}

void AutoPartTracker::reset(std::uint64_t now, std::uint64_t lastActivity)
{
    seen_ = lastActivity;
    lastPoll_ = now;
    inPart_ = false;
    start_ = 0;
}

AutoPartTracker::Action AutoPartTracker::update(std::uint64_t now, std::uint64_t lastActivity, bool held,
                                                std::uint64_t earliestNew)
{
    Action a;
    if (!inPart_) {
        if (lastActivity > seen_) {
            // It happened after the last look (MIDI can arrive a little late, so
            // from the earlier of the two), minus the preroll.
            const std::uint64_t noticed = std::min({lastActivity, lastPoll_, earliestNew});
            start_ = noticed > timing_.preroll ? noticed - timing_.preroll : 0;
            inPart_ = true;
            a = {Kind::Begin, start_};
        }
        lastPoll_ = now;
        return a;
    }
    lastPoll_ = now;
    const std::uint64_t quietSince = std::max(lastActivity, start_);
    const std::uint64_t quiet = now > quietSince ? now - quietSince : 0;
    if ((!held && quiet >= timing_.pause) || quiet >= timing_.maxHeldQuiet) {
        std::uint64_t end = std::min(quietSince + timing_.tail, now);
        end = std::max(end, start_);
        inPart_ = false;
        seen_ = std::max(seen_, lastActivity);
        a = {Kind::End, end};
    }
    return a;
}

AutoPartTracker::Action AutoPartTracker::finish(std::uint64_t now)
{
    if (!inPart_) return {};
    inPart_ = false;
    return {Kind::End, std::max(now, start_)};
}

std::string autoPartName(const std::tm& t)
{
    char buf[96];  // room for any int values (the compiler checks)
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                  t.tm_hour, t.tm_min, t.tm_sec);
    return buf;
}

} // namespace minidaw
