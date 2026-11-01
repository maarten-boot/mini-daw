#include "minidaw/config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace minidaw {
namespace {

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Strip a trailing "; comment" or "# comment" from a value.
std::string_view stripComment(std::string_view value)
{
    for (std::size_t i = 0; i < value.size(); ++i) {
        if ((value[i] == ';' || value[i] == '#') &&
            (i == 0 || std::isspace(static_cast<unsigned char>(value[i - 1])))) {
            return trim(value.substr(0, i));
        }
    }
    return trim(value);
}

std::vector<std::string_view> splitLines(std::string_view text)
{
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        start = end + 1;
    }
    return lines;
}

// "[name]" -> "name" (lower case); otherwise empty.
std::string sectionName(std::string_view line)
{
    line = trim(line);
    if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
        return lower(trim(line.substr(1, line.size() - 2)));
    }
    return {};
}

std::string getEnv(const char* name)
{
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

} // namespace

ConfigLoadResult parseConfig(std::string_view text, std::string_view sourceName)
{
    ConfigLoadResult result;
    std::string section;
    int lineNo = 0;
    auto warn = [&](const std::string& msg) {
        result.warnings.push_back(std::string(sourceName) + " line " + std::to_string(lineNo) + ": " + msg);
    };

    for (std::string_view raw : splitLines(text)) {
        ++lineNo;
        std::string_view line = trim(raw);
        if (line.empty() || line.front() == ';' || line.front() == '#') {
            continue;
        }
        if (line.front() == '[') {
            section = sectionName(line);
            if (section.empty()) warn("malformed section header");
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            warn("expected 'key = value'");
            continue;
        }
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string_view value = stripComment(line.substr(eq + 1));

        if (section == "ui" && key == "min_contrast") {
            ContrastParseResult parsed = parseContrastSetting(value);
            if (parsed.setting) {
                result.config.contrast = *parsed.setting;
            } else {
                warn(parsed.error + " Using AA (4.5:1).");
            }
        } else {
            warn("unknown setting '" + key + "' in section [" + section + "]");
        }
    }
    return result;
}

std::string setConfigValue(std::string_view text, std::string_view section, std::string_view key,
                           std::string_view value)
{
    const std::string wantSection = lower(section);
    const std::string wantKey = lower(key);
    const std::string newLine = std::string(key) + " = " + std::string(value);

    std::vector<std::string> lines;
    for (std::string_view l : splitLines(text)) lines.emplace_back(l);

    std::string current;
    std::ptrdiff_t sectionEnd = -1;  // index after the last line of the wanted section
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string_view line = trim(lines[i]);
        if (!line.empty() && line.front() == '[') {
            current = sectionName(line);
            if (current == wantSection) sectionEnd = static_cast<std::ptrdiff_t>(i) + 1;
            continue;
        }
        if (current != wantSection) continue;
        if (!line.empty() && line.front() != ';' && line.front() != '#') {
            sectionEnd = static_cast<std::ptrdiff_t>(i) + 1;
        }
        const std::size_t eq = line.find('=');
        if (eq != std::string_view::npos && lower(trim(line.substr(0, eq))) == wantKey) {
            lines[i] = newLine;
            std::string out;
            for (const auto& l : lines) out += l + "\n";
            return out;
        }
    }

    if (sectionEnd >= 0) {
        lines.insert(lines.begin() + sectionEnd, newLine);
    } else {
        if (!lines.empty() && !trim(lines.back()).empty()) lines.emplace_back();
        lines.push_back("[" + std::string(section) + "]");
        lines.push_back(newLine);
    }
    std::string out;
    for (const auto& l : lines) out += l + "\n";
    return out;
}

std::string defaultConfigText()
{
    return "; mini-daw configuration\n"
           "\n"
           "[ui]\n"
           "; Minimum contrast ratio for all screen colours:\n"
           ";   AA  = 4.5:1 (default), AAA = 7:1, Max = 21:1 (pure black on white),\n"
           ";   or a number from 4.5 to 21.\n"
           "min_contrast = AA\n";
}

std::filesystem::path defaultConfigPath()
{
    namespace fs = std::filesystem;
#if defined(_WIN32)
    const std::string base = getEnv("APPDATA");
    if (!base.empty()) return fs::path(base) / "mini-daw" / "config.ini";
#elif defined(__APPLE__)
    const std::string home = getEnv("HOME");
    if (!home.empty()) return fs::path(home) / "Library" / "Application Support" / "mini-daw" / "config.ini";
#else
    const std::string xdg = getEnv("XDG_CONFIG_HOME");
    if (!xdg.empty()) return fs::path(xdg) / "mini-daw" / "config.ini";
    const std::string home = getEnv("HOME");
    if (!home.empty()) return fs::path(home) / ".config" / "mini-daw" / "config.ini";
#endif
    return fs::path("mini-daw-config.ini");
}

ConfigLoadResult loadConfigFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream text;
    text << in.rdbuf();
    return parseConfig(text.str(), path.filename().string());
}

std::string saveContrastSetting(const std::filesystem::path& path, const ContrastSetting& setting)
{
    std::string text;
    {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            text = ss.str();
        } else {
            text = defaultConfigText();
        }
    }
    text = setConfigValue(text, "ui", "min_contrast", setting.toString());

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return "cannot create folder " + path.parent_path().string() + ": " + ec.message();
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return "cannot write " + path.string();
    out << text;
    if (!out) return "error while writing " + path.string();
    return {};
}

} // namespace minidaw
