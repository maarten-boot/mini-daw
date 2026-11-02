#pragma once

// Paths <-> UTF-8 text (crosscheck #4, J6).
//
// Text in the GUI and the config file is UTF-8. On Windows, path(std::string)
// and path::string() use the ANSI code page instead, which garbles names such
// as "Gitaar é" (or throws). These helpers convert explicitly.

#include <filesystem>
#include <string>
#include <string_view>

namespace minidaw {

inline std::filesystem::path pathFromUtf8(std::string_view text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

inline std::string pathToUtf8(const std::filesystem::path& path)
{
    const std::u8string u = path.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

} // namespace minidaw
