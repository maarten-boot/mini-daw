// One mini-daw per config folder (P20, SPEC-RUN-01).

#include "minidaw/instance_lock.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace minidaw;

TEST_CASE("T-RUN-01: a second lock on the same folder is refused until the first is released", "[run][T-RUN-01]")
{
    test::TempDir dir;
    const auto folder = dir.path() / "config";  // made by acquire
    std::string err;
    InstanceLock first;
    REQUIRE(first.acquire(folder, err) == InstanceLock::Result::Acquired);
    CHECK(first.held());
    CHECK(InstanceLock::ownerProcess(folder) != 0);

    InstanceLock second;  // another handle, as another process would have
    CHECK(second.acquire(folder, err) == InstanceLock::Result::HeldByOther);
    CHECK_FALSE(second.held());

    InstanceLock elsewhere;  // another config folder: its own lock
    CHECK(elsewhere.acquire(dir.path() / "other", err) == InstanceLock::Result::Acquired);

    first.release();
    CHECK(second.acquire(folder, err) == InstanceLock::Result::Acquired);
}

TEST_CASE("T-RUN-01: a second start's show request is seen once by the owner", "[run][T-RUN-01]")
{
    test::TempDir dir;
    std::string err;
    REQUIRE(InstanceLock::requestShow(dir.path()));  // an old request, before the owner started
    InstanceLock owner;
    REQUIRE(owner.acquire(dir.path(), err) == InstanceLock::Result::Acquired);
    CHECK_FALSE(owner.showRequested());              // old ones do not count
    REQUIRE(InstanceLock::requestShow(dir.path()));
    CHECK(owner.showRequested());
    CHECK_FALSE(owner.showRequested());              // once
    REQUIRE(InstanceLock::requestShow(dir.path()));
    CHECK(owner.showRequested());                    // each new one
}
