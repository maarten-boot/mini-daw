#include "minidaw/version.hpp"

#include <catch2/catch_test_macros.hpp>

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
