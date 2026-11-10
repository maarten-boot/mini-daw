// The editors' run loop (P24, SPEC-VST-05b).

#include "minidaw/run_loop.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace minidaw;
using namespace std::chrono_literals;

TEST_CASE("T-VST-37: run loop timers: due times, no catching up, removed in their own call", "[vst][T-VST-37]")
{
    RunLoop loop;
    const auto t0 = RunLoop::Clock::now();
    int a = 0, b = 0;
    const RunLoop::Id ta = loop.addTimer(10ms, [&] { ++a; }, &a, t0);
    const RunLoop::Id tb = loop.addTimer(0ms, [&] { ++b; }, &b, t0);  // at least 1 ms
    CHECK(ta != 0);
    CHECK(tb != ta);
    CHECK(loop.timerCount() == 2);
    CHECK(loop.readerCount() == 0);
    CHECK(loop.addTimer(5ms, {}, nullptr, t0) == 0);  // no callback: not added

    CHECK(loop.poll(t0) == 0);           // nothing due yet
    CHECK(loop.poll(t0 + 1ms) == 1);     // b
    CHECK(b == 1);
    CHECK(loop.poll(t0 + 10ms) == 2);    // a and b
    CHECK(a == 1);
    CHECK(loop.poll(t0 + 15ms) == 1);    // only b: a is next due at 20 ms
    CHECK(loop.poll(t0 + 100ms) == 2);   // late: each once, no catching up
    CHECK(a == 2);
    CHECK(loop.poll(t0 + 105ms) == 1);   // a next at 110 ms
    CHECK(loop.poll(t0 + 110ms) == 2);
    CHECK(a == 3);

    // A timer that removes itself, and one that removes another in the same poll.
    int c = 0;
    RunLoop::Id tc = 0;
    tc = loop.addTimer(1ms, [&] { ++c; loop.remove(tc); }, &c, t0 + 110ms);
    CHECK(loop.poll(t0 + 111ms) == 2);  // b and c (a is due at 120 ms)
    CHECK(c == 1);
    CHECK_FALSE(loop.remove(tc));       // already gone
    loop.addTimer(1ms, [&] { loop.remove(ta); }, &c, t0 + 111ms);
    // The new one runs at 120 ms after a (added first) ... a runs too, then it is removed.
    loop.poll(t0 + 120ms);
    CHECK(a == 4);
    CHECK(loop.poll(t0 + 200ms) >= 1);
    CHECK(a == 4);  // removed

    // All of one owner.
    CHECK(loop.removeOwner(&c) == 1);
    CHECK(loop.removeOwner(&c) == 0);
    CHECK(loop.removeOwner(&b) == 1);
    CHECK(loop.timerCount() == 0);
    CHECK(loop.poll(t0 + 1s) == 0);
}

namespace {
// Like a plugin object held by a handler, whose destructor unregisters (Z2).
struct RemovesOnDestroy {
    RunLoop* loop = nullptr;
    RunLoop::Id other = 0;
    int* destroyed = nullptr;
    ~RemovesOnDestroy()
    {
        ++*destroyed;
        if (loop && other) loop->remove(other);
        if (loop) loop->removeOwner(this);  // its own owner: already gone, nothing to do
    }
};
} // namespace

TEST_CASE("T-VST-37: a handler's destructor may remove handlers while the run loop removes it", "[vst][T-VST-37]")
{
    RunLoop loop;
    const auto t0 = RunLoop::Clock::now();
    int destroyed = 0;
    int calls = 0;
    const RunLoop::Id keep = loop.addTimer(5ms, [&] { ++calls; }, &calls, t0);

    // remove(): the destructor removes another entry.
    const RunLoop::Id other = loop.addTimer(5ms, [] {}, &destroyed, t0);
    auto obj = std::make_shared<RemovesOnDestroy>();
    obj->loop = &loop;
    obj->other = other;
    obj->destroyed = &destroyed;
    const RunLoop::Id a = loop.addTimer(5ms, [obj] { (void)obj; }, &destroyed, t0);
    obj.reset();  // the handler holds the last reference
    CHECK(loop.timerCount() == 3);
    CHECK(loop.remove(a));
    CHECK(destroyed == 1);
    CHECK(loop.timerCount() == 1);  // a, and the other one through the destructor
    CHECK_FALSE(loop.remove(other));

    // removeOwner(): two of one owner; the first's destructor removes the second.
    auto first = std::make_shared<RemovesOnDestroy>();
    first->loop = &loop;
    first->destroyed = &destroyed;
    const RunLoop::Id second = loop.addTimer(5ms, [] {}, &first, t0);
    const RunLoop::Id unrelated = loop.addTimer(5ms, [] {}, &destroyed, t0);
    first->other = unrelated;  // it removes an entry of another owner
    loop.addTimer(5ms, [first] { (void)first; }, &first, t0);
    first.reset();
    CHECK(loop.removeOwner(&first) == 2);
    CHECK(destroyed == 2);
    CHECK_FALSE(loop.remove(second));
    CHECK_FALSE(loop.remove(unrelated));
    CHECK(loop.timerCount() == 1);
    CHECK(loop.poll(t0 + 5ms) == 1);
    CHECK(calls == 1);
    CHECK(loop.remove(keep));
}

#if !defined(_WIN32)
TEST_CASE("T-VST-37: run loop readers: called when their descriptor can be read", "[vst][T-VST-37]")
{
    RunLoop loop;
    int p[2] = {-1, -1};
    REQUIRE(::pipe(p) == 0);
    ::fcntl(p[0], F_SETFL, O_NONBLOCK);
    int calls = 0;
    const RunLoop::Id r = loop.addReader(p[0], [&] {
        char buf[16];
        while (::read(p[0], buf, sizeof buf) > 0) {
        }
        ++calls;
    }, &calls);
    CHECK(r != 0);
    CHECK(loop.addReader(-1, [] {}, nullptr) == 0);  // no descriptor
    CHECK(loop.readerCount() == 1);
    CHECK(loop.poll() == 0);  // nothing to read
    const char x = 1;
    REQUIRE(::write(p[1], &x, 1) == 1);
    CHECK(loop.poll() == 1);
    CHECK(calls == 1);
    CHECK(loop.poll() == 0);  // read empty again

    // Closed write end: hang-up counts as ready (the handler sees end of file).
    ::close(p[1]);
    CHECK(loop.poll() == 1);
    // A reader that removes the other reader of the same poll: that one is not called.
    int q[2] = {-1, -1};
    REQUIRE(::pipe(q) == 0);
    REQUIRE(::write(q[1], &x, 1) == 1);
    int other = 0;
    loop.remove(r);
    RunLoop::Id second = 0;
    loop.addReader(q[0], [&] { loop.remove(second); }, &q);
    second = loop.addReader(q[0], [&] { ++other; }, &q);
    CHECK(loop.poll() == 1);
    CHECK(other == 0);
    CHECK(loop.removeOwner(&q) == 1);
    CHECK(loop.readerCount() == 0);
    ::close(p[0]);
    ::close(q[0]);
    ::close(q[1]);
}

TEST_CASE("T-VST-37: a reader whose descriptor was closed is dropped, not called", "[vst][T-VST-37]")
{
    RunLoop loop;
    int p[2] = {-1, -1};
    REQUIRE(::pipe(p) == 0);
    int calls = 0;
    const RunLoop::Id r = loop.addReader(p[0], [&] { ++calls; }, &calls);
    const std::size_t before = loop.droppedCount();
    ::close(p[0]);  // closed by its owner, still registered
    ::close(p[1]);
    CHECK(loop.poll() == 0);
    CHECK(calls == 0);
    CHECK(loop.readerCount() == 0);
    CHECK(loop.droppedCount() == before + 1);
    CHECK_FALSE(loop.remove(r));  // already gone; unregistering later is harmless
    CHECK(loop.poll() == 0);
    CHECK(loop.droppedCount() == before + 1);
}
#endif
