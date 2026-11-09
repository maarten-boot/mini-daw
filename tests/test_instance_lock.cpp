// One mini-daw per config folder (P20, SPEC-RUN-01).

#include "minidaw/instance_lock.hpp"
#include "test_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

TEST_CASE("T-RUN-01: odd cases: an empty owner file, a missing folder, a read-only folder", "[run][T-RUN-01]")
{
    test::TempDir dir;
    std::ofstream(dir.path() / "mini-daw.lock");  // empty: no process ID
    CHECK(InstanceLock::ownerProcess(dir.path()) == 0);
    CHECK(InstanceLock::ownerProcess(dir.path() / "none") == 0);
    CHECK_FALSE(InstanceLock::requestShow(dir.path() / "none"));
#if !defined(_WIN32)
    // A folder that may not be written: Failed (mini-daw then starts without the lock).
    const auto ro = dir.path() / "readonly";
    std::filesystem::create_directories(ro);
    ::chmod(ro.c_str(), 0500);
    if (::geteuid() != 0) {  // root may write anyway
        InstanceLock lock;
        std::string err;
        CHECK(lock.acquire(ro, err) == InstanceLock::Result::Failed);
        CHECK_FALSE(err.empty());
    }
    ::chmod(ro.c_str(), 0700);
#endif
}

#if !defined(_WIN32)
TEST_CASE("T-RUN-01: another process cannot take the lock while it is held", "[run][T-RUN-01]")
{
    test::TempDir dir;
    std::string err;
    InstanceLock owner;
    REQUIRE(owner.acquire(dir.path(), err) == InstanceLock::Result::Acquired);
    const auto tryInChild = [&]() {
        const pid_t pid = ::fork();
        if (pid == 0) {  // the child: a second mini-daw
            InstanceLock other;
            std::string e;
            const auto r = other.acquire(dir.path(), e);
            ::_exit(r == InstanceLock::Result::HeldByOther ? 1 : r == InstanceLock::Result::Acquired ? 0 : 2);
        }
        int status = 0;
        ::waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    };
    CHECK(tryInChild() == 1);  // held by us
    owner.release();
    CHECK(tryInChild() == 0);  // free again
}
#endif
