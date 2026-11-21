#include "minidaw/build_info.hpp"
#include "minidaw/version.hpp"

#include <catch2/catch_test_macros.hpp>

#include <regex>
#include <string>

TEST_CASE("Version string is set from CMake", "[version]")
{
    const std::string v = minidaw::kVersion;
    REQUIRE_FALSE(v.empty());
    REQUIRE(v.find('@') == std::string::npos);  // configure_file actually substituted it
}

TEST_CASE("Product name does not contain 'ASIO' (SPEC-LIC-04, T-LIC-05)", "[licence]")
{
    const std::string name = minidaw::kProductName;
    REQUIRE(name.find("ASIO") == std::string::npos);
    REQUIRE(name.find("asio") == std::string::npos);
}

TEST_CASE("Version is <major>.<minor>.<patch> and builds can be told apart (SPEC-UI-18, T-UI-32)", "[version]")
{
    using minidaw::BuildInfo;
    // 0.<step>.<round> before the first release, 1.<minor>.<patch> since 1.0.0 (P33).
    REQUIRE(std::regex_match(minidaw::versionText(), std::regex(R"((0\.[1-9][0-9]*|[1-9][0-9]*\.[0-9]+)\.[0-9]+)")));
    REQUIRE(minidaw::versionText() != "0.1.0");  // raised with the steps since P28a

    CHECK(minidaw::buildText(BuildInfo{"380f0af", "2026-11-16", false, "43849674053"}) ==
          "commit 380f0af of 2026-11-16, CI run 43849674053");
    CHECK(minidaw::buildText(BuildInfo{"380f0af", "2026-11-16", true, ""}) ==
          "commit 380f0af of 2026-11-16 with local changes, local build");
    CHECK(minidaw::buildText(BuildInfo{"", "", false, ""}) == "local build");
    CHECK(minidaw::buildText(BuildInfo{"", "", false, "17"}) == "CI run 17");

    // This build: a commit when built from git (always in CI).
    const BuildInfo& b = minidaw::buildInfo();
    REQUIRE(std::regex_match(std::string(b.commit), std::regex(R"((|[0-9a-f]{7,40}))")));
    REQUIRE(std::regex_match(std::string(b.ciRun), std::regex(R"((|[0-9]+))")));
    if (*b.ciRun) REQUIRE(*b.commit);
    REQUIRE(minidaw::fullVersionText().rfind(minidaw::versionText() + " (", 0) == 0);
    REQUIRE_FALSE(minidaw::platformText().empty());
}
