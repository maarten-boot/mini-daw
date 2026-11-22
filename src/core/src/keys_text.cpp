#include "minidaw/keys_text.hpp"

namespace minidaw {

namespace {

void replaceAll(std::string& s, std::string_view from, std::string_view to)
{
    for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) {
        s.replace(at, from.size(), to);
    }
}

} // namespace

std::string keysForMac(std::string_view text)
{
    std::string s(text);
    replaceAll(s, "Ctrl+", "Cmd+");
    replaceAll(s, "Alt+F4", "Cmd+Q");
    return s;
}

std::string keysHere(std::string_view text)
{
#if defined(__APPLE__)
    return keysForMac(text);
#else
    return std::string(text);
#endif
}

} // namespace minidaw
