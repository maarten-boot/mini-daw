#include "minidaw/build_info.hpp"

#include "minidaw/version.hpp"

namespace minidaw {

std::string versionText()
{
    return kVersion;
}

std::string buildText(const BuildInfo& info)
{
    std::string s;
    if (info.commit && *info.commit) {
        s = std::string("commit ") + info.commit;
        if (info.date && *info.date) s += std::string(" of ") + info.date;
        if (info.localChanges) s += " with local changes";
        s += ", ";
    }
    if (info.ciRun && *info.ciRun) {
        s += std::string("CI run ") + info.ciRun;
    } else {
        s += "local build";
    }
    return s;
}

std::string buildText()
{
    return buildText(buildInfo());
}

std::string fullVersionText()
{
    return versionText() + " (" + buildText() + ")";
}

std::string platformText()
{
#if defined(_WIN32)
    std::string s = "Windows";
#elif defined(__APPLE__)
    std::string s = "macOS";
#else
    std::string s = "Linux";
#endif
#if defined(_MSC_VER)
    s += ", MSVC " + std::to_string(_MSC_VER / 100) + "." + std::to_string(_MSC_VER % 100);
#elif defined(__clang__)
    s += ", Clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
#if defined(__MINGW32__)
    s += ", MinGW GCC ";
#else
    s += ", GCC ";
#endif
    s += std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__);
#endif
    s += sizeof(void*) == 8 ? ", 64-bit" : ", 32-bit";
    return s;
}

} // namespace minidaw
