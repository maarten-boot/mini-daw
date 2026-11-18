#pragma once

// Which build this is (P28a, SPEC-UI-18): the version from CMake plus the
// commit, its date and the CI run, written at build time
// (cmake/build_info.cmake), so two builds of one version can be told apart.

#include <string>

namespace minidaw {

struct BuildInfo {
    const char* commit;     // short hash, "" when not built from git
    const char* date;       // the commit's date, YYYY-MM-DD
    bool localChanges;      // built with changes not committed
    const char* ciRun;      // GitHub Actions run ID, "" for a local build
};

// Generated at build time.
const BuildInfo& buildInfo();

// "0.28.1"
std::string versionText();
// "commit 380f0af of 2026-11-16, CI run 43849674053" / "commit 380f0af of
// 2026-11-16 with local changes, local build" / "local build"
std::string buildText(const BuildInfo& info);
std::string buildText();
// "0.28.1 (commit 380f0af of 2026-11-16, CI run 43849674053)"
std::string fullVersionText();
// "Windows, MSVC 19.44" / "Linux, GCC 13.3.0"
std::string platformText();

} // namespace minidaw
