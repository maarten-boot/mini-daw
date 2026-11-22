#pragma once
// Key names in texts as this system writes them (P34d). mini-daw's texts say
// Ctrl+...; on macOS the same shortcuts are Cmd+... (ImGui's Ctrl modifier is
// the Command key there) and Quit is Cmd+Q.

#include <string>
#include <string_view>

namespace minidaw {

// "Ctrl+" -> "Cmd+", "Alt+F4" -> "Cmd+Q".
std::string keysForMac(std::string_view text);

// keysForMac on macOS, the text unchanged elsewhere.
std::string keysHere(std::string_view text);

} // namespace minidaw
