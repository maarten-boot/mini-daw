// Auto record (SPEC-REC-08, P16): T-REC-34, the part tracker.

#include "minidaw/auto_record.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace minidaw;
using Kind = AutoPartTracker::Kind;

TEST_CASE("T-REC-34: auto record timing from the settings", "[auto][T-REC-34]")
{
    const AutoRecordTiming t = autoRecordTiming(5, 48000);
    CHECK(t.pause == 240000);
    CHECK(t.tail == 48000);
    CHECK(t.preroll == 48000);
    CHECK(t.maxHeldQuiet == 60u * 48000);
    CHECK(autoRecordTiming(0, 1000).pause == 1000);    // at least 1 s
    CHECK(autoRecordTiming(99, 1000).pause == 60000);  // at most 60 s
}

TEST_CASE("T-REC-34: parts start at playing and end after a pause", "[auto][T-REC-34]")
{
    AutoPartTracker tr(autoRecordTiming(5, 1000));  // pause 5000, tail 1000, preroll 1000 frames
    tr.reset(0, 500);                               // activity before Auto was switched on: old
    CHECK(tr.update(100, 500, false).kind == Kind::None);
    CHECK(tr.update(1990, 500, false).kind == Kind::None);

    // Played at 2000, noticed at 2010: the part starts 1 s before (from the last look, 1990).
    auto a = tr.update(2010, 2000, false);
    CHECK(a.kind == Kind::Begin);
    CHECK(a.frame == 990);
    CHECK(tr.inPart());
    CHECK(tr.partStart() == 990);

    CHECK(tr.update(3000, 2500, false).kind == Kind::None);
    // A note held from 3500 on keeps the part open while it is quiet.
    CHECK(tr.update(9000, 3500, true).kind == Kind::None);
    // Released (no new activity): 5 s quiet since 3500 -> ends 1 s after it.
    a = tr.update(9000, 3500, false);
    CHECK(a.kind == Kind::End);
    CHECK(a.frame == 4500);
    CHECK_FALSE(tr.inPart());
    CHECK(tr.update(9100, 3500, false).kind == Kind::None);  // the same activity starts nothing

    // The next playing starts a new part.
    CHECK(tr.update(11990, 3500, false).kind == Kind::None);
    a = tr.update(12000, 12000, false);
    CHECK(a.kind == Kind::Begin);
    CHECK(a.frame == 10990);
    // Switched off while a part is open: it ends now.
    a = tr.finish(13000);
    CHECK(a.kind == Kind::End);
    CHECK(a.frame == 13000);
    CHECK(tr.finish(14000).kind == Kind::None);
}

TEST_CASE("T-REC-34: a note held for ever ends the part after 60 s; the start is not before 0",
          "[auto][T-REC-34]")
{
    AutoPartTracker tr(autoRecordTiming(5, 1000));
    tr.reset(0, 0);
    auto a = tr.update(300, 300, true);
    CHECK(a.kind == Kind::Begin);
    CHECK(a.frame == 0);  // 1 s before 0 is 0
    CHECK(tr.update(60000, 300, true).kind == Kind::None);
    a = tr.update(60300, 300, true);
    CHECK(a.kind == Kind::End);
    CHECK(a.frame == 1300);
}

TEST_CASE("T-REC-34: part folder names", "[auto][T-REC-34]")
{
    std::tm t{};
    t.tm_year = 126;
    t.tm_mon = 10;
    t.tm_mday = 6;
    t.tm_hour = 21;
    t.tm_min = 5;
    t.tm_sec = 3;
    CHECK(autoPartName(t) == "2026-11-06 21-05-03");
}

TEST_CASE("T-REC-34: MIDI that arrives late starts the part from its own frame", "[auto][T-REC-34]")
{
    AutoPartTracker tr(autoRecordTiming(5, 1000));
    tr.reset(0, 0);
    CHECK(tr.update(5000, 0, false).kind == Kind::None);
    // Two messages (at 4950 and 5100) arrive together at 5200.
    const auto a = tr.update(5200, 5100, false, 4950);
    CHECK(a.kind == Kind::Begin);
    CHECK(a.frame == 3950);
}

TEST_CASE("T-REC-34: a part that could not start is forgotten until the next playing (S4)", "[auto][T-REC-34]")
{
    AutoPartTracker tr(autoRecordTiming(5, 1000));
    tr.reset(0, 0);
    CHECK(tr.update(1000, 1000, false).kind == Kind::Begin);
    tr.abort(1000);
    CHECK_FALSE(tr.inPart());
    CHECK(tr.update(1020, 1000, false).kind == Kind::None);  // not again for the same playing
    CHECK(tr.update(2000, 2000, false).kind == Kind::Begin);  // the next playing tries again
}

TEST_CASE("T-REC-34: a note-off alone does not start a part (S6)", "[auto][T-REC-34]")
{
    AutoPartTracker tr(autoRecordTiming(5, 1000));
    tr.reset(0, 0);
    // Only a note-off (activity, but nothing that starts sound) at 1000.
    CHECK(tr.update(1010, 1000, false, UINT64_MAX, 0).kind == Kind::None);
    // A note-on at 2000 starts one; the note-off at 2500 keeps it going.
    auto a = tr.update(2010, 2000, false, UINT64_MAX, 2000);
    CHECK(a.kind == Kind::Begin);
    CHECK(tr.update(7000, 2500, false, UINT64_MAX, 2000).kind == Kind::None);
    a = tr.update(7500, 2500, false, UINT64_MAX, 2000);
    CHECK(a.kind == Kind::End);
    CHECK(a.frame == 3500);
    // A late note-off at 9000 starts nothing.
    CHECK(tr.update(9010, 9000, false, UINT64_MAX, 2000).kind == Kind::None);
}
