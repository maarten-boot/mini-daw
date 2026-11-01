// Lock-free ring buffer (SPEC-NF-01, SPEC-REC-05): T-REC-06.

#include "minidaw/ring_buffer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <thread>
#include <vector>

using namespace minidaw;

TEST_CASE("Ring buffer basics: capacity, full, empty, wrap-around", "[ring][T-REC-06]")
{
    SpscRing<int> r(5);
    CHECK(r.capacity() == 8);
    CHECK(r.readAvailable() == 0);
    CHECK(r.writeSpace() == 8);

    const int a[] = {1, 2, 3, 4, 5, 6};
    REQUIRE(r.write(a, 6));
    CHECK_FALSE(r.write(a, 3));  // all or nothing: only 2 free
    CHECK(r.readAvailable() == 6);

    int out[8] = {};
    CHECK(r.read(out, 4) == 4);
    CHECK(out[0] == 1);
    CHECK(out[3] == 4);

    REQUIRE(r.write(a, 6));      // wraps around the end
    CHECK(r.read(out, 8) == 8);
    const int expect[] = {5, 6, 1, 2, 3, 4, 5, 6};
    for (int i = 0; i < 8; ++i) CHECK(out[i] == expect[i]);
    CHECK(r.read(out, 8) == 0);
}

TEST_CASE("Ring buffer slot()/commitWrite() fill in place", "[ring][T-REC-06]")
{
    SpscRing<float> r(4);
    float x = 0;
    r.read(&x, 0);
    const float pre[] = {9, 9, 9};
    REQUIRE(r.write(pre, 3));
    float sink[3];
    r.read(sink, 3);             // write position is now 3, so the next block wraps
    REQUIRE(r.canWrite(4));
    for (std::size_t i = 0; i < 4; ++i) r.slot(i) = static_cast<float>(i + 1);
    r.commitWrite(4);
    float out[4];
    REQUIRE(r.read(out, 4) == 4);
    CHECK(out[0] == 1.0f);
    CHECK(out[3] == 4.0f);
    CHECK_FALSE(r.canWrite(5));
}

TEST_CASE("T-REC-06: 10^8 values through the ring, nothing lost or duplicated", "[ring][T-REC-06]")
{
    constexpr std::uint64_t total = 100'000'000;
    SpscRing<std::uint32_t> ring(8192);

    std::thread producer([&] {
        std::vector<std::uint32_t> block(520);  // blocks are 7 .. 515 values
        std::uint64_t next = 0;
        std::size_t size = 1;
        while (next < total) {
            size = size % 509 + 7;  // changing block sizes
            const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(size, total - next));
            for (std::size_t i = 0; i < n; ++i) block[i] = static_cast<std::uint32_t>(next + i);
            while (!ring.write(block.data(), n)) std::this_thread::yield();
            next += n;
        }
    });

    std::vector<std::uint32_t> got(1000);
    std::uint64_t expected = 0;
    std::uint64_t errors = 0;
    std::size_t ask = 1;
    while (expected < total) {
        ask = ask % 997 + 3;
        const std::size_t n = ring.read(got.data(), ask);
        if (n == 0) {
            std::this_thread::yield();
            continue;
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (got[i] != static_cast<std::uint32_t>(expected + i)) ++errors;
        }
        expected += n;
    }
    producer.join();
    CHECK(errors == 0);
    CHECK(expected == total);
    CHECK(ring.readAvailable() == 0);
}
