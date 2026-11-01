#pragma once

// Small helpers shared by the tests.

#include <chrono>
#include <filesystem>
#include <random>
#include <string>

namespace minidaw::test {

// A fresh, empty folder under the system temp folder; removed again on scope exit.
class TempDir {
public:
    TempDir()
    {
        std::random_device rd;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("minidaw-test-" + std::to_string(stamp) + "-" + std::to_string(rd()));
        std::filesystem::create_directories(path_);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }
    std::filesystem::path operator/(const std::string& name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

} // namespace minidaw::test
