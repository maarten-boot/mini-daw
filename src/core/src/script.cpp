#include "minidaw/script.hpp"

#include <charconv>
#include <cmath>

namespace minidaw {

namespace {

std::string lowerAscii(std::string s)
{
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::optional<double> parseNumber(std::string_view text)
{
    if (text.empty()) return std::nullopt;
    double v = 0.0;
    const char* end = text.data() + text.size();
    const auto r = std::from_chars(text.data(), end, v);
    if (r.ec != std::errc() || r.ptr != end || !std::isfinite(v)) return std::nullopt;
    return v;
}

} // namespace

const std::vector<ScriptCommand>& scriptCommands()
{
    static const std::vector<ScriptCommand> list = {
        {"echo", 0, -1, "echo <text>", "Writes the text into the report."},
        {"wait", 1, 1, "wait <seconds>", "Lets mini-daw run that long (0 to 3600 s)."},
        {"wait-for", 3, 4, "wait-for <seconds> <path> [op] <value>",
         "Waits until the state value holds, at most that long (a failure if it does not)."},
        {"expect", 2, 3, "expect <path> [op] <value>",
         "Checks a state value, e.g. expect tracks.3.instrument \"DX7 V\" or expect log.errors == 0."},
        {"state", 0, 0, "state", "Writes mini-daw's state (JSON) into the report."},
        {"stop-on-failure", 1, 1, "stop-on-failure on|off", "Ends the script at the first failure (default off)."},
        {"stay", 0, 0, "stay", "Keeps mini-daw open after the script, instead of quitting."},
        {"quit", 0, 0, "quit", "Ends the script here and quits."},
        {"scan", 0, 0, "scan", "Scans the instrument folders (like Settings > Instruments > Scan) and waits for it."},
        {"open", 1, 1, "open <project folder>", "Opens a project (like File > Open project)."},
        {"save", 0, 0, "save", "Saves the project."},
        {"add-track", 1, 1, "add-track midi|audio",
         "Adds a track (like Add MIDI track / Add audio track); <track> \"last\" is then this one."},
        {"remove-track", 1, 1, "remove-track <track>", "Removes a track (like its X button)."},
        {"instrument", 2, 2, "instrument <track> <name or class ID>",
         "Chooses an instrument for a MIDI track (as in its Output list) and waits until it is loaded."},
        {"output", 2, 2, "output <track> <MIDI output>|none", "Sends a MIDI track to a MIDI output, or nowhere."},
        {"arm", 2, 2, "arm <track> on|off", "Ticks or clears a track's Arm."},
        {"edit", 1, 1, "edit <track>", "Opens the instrument's own window (Edit)."},
        {"close-edit", 1, 1, "close-edit <track>", "Closes it (Close)."},
        {"play", 0, 0, "play", "Starts playing (if not playing)."},
        {"stop", 0, 0, "stop", "Stops playing (if playing)."},
        {"home", 0, 0, "home", "Back to the start (|< Start)."},
        {"record", 0, 0, "record", "Starts or stops recording (Record, Ctrl+R)."},
    };
    return list;
}

const ScriptCommand* findScriptCommand(std::string_view name)
{
    for (const ScriptCommand& c : scriptCommands()) {
        if (c.name == name) return &c;
    }
    return nullptr;
}

bool splitScriptWords(std::string_view line, std::vector<std::string>& words, std::string& error)
{
    words.clear();
    std::size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (c == '#') break;  // a comment to the end of the line
        std::string word;
        if (c == '"') {
            ++i;
            bool closed = false;
            while (i < line.size()) {
                const char d = line[i];
                if (d == '"') {
                    closed = true;
                    ++i;
                    break;
                }
                if (d == '\\' && i + 1 < line.size() && (line[i + 1] == '"' || line[i + 1] == '\\')) {
                    word += line[i + 1];
                    i += 2;
                    continue;
                }
                word += d;
                ++i;
            }
            if (!closed) {
                error = "a quote is not closed";
                return false;
            }
        } else {
            while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') word += line[i++];
        }
        words.push_back(std::move(word));
    }
    return true;
}

ScriptParse parseScript(std::string_view text)
{
    ScriptParse out;
    int number = 0;
    std::size_t start = 0;
    // A UTF-8 byte order mark (Notepad) is not part of the first command.
    if (text.substr(0, 3) == "\xEF\xBB\xBF") start = 3;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view raw = text.substr(start, end - start);
        if (!raw.empty() && raw.back() == '\r') raw.remove_suffix(1);
        ++number;
        start = end + 1;
        ScriptLine line;
        line.number = number;
        line.text = std::string(raw);
        std::string error;
        const std::string where = "line " + std::to_string(number) + ": ";
        if (!splitScriptWords(raw, line.words, error)) {
            out.errors.push_back(where + error);
            continue;
        }
        if (line.words.empty()) continue;
        line.words[0] = lowerAscii(line.words[0]);
        const ScriptCommand* cmd = findScriptCommand(line.words[0]);
        if (!cmd) {
            out.errors.push_back(where + "unknown command \"" + line.words[0] + "\"");
            continue;
        }
        const int args = static_cast<int>(line.words.size()) - 1;
        if (args < cmd->minArgs || (cmd->maxArgs >= 0 && args > cmd->maxArgs)) {
            out.errors.push_back(where + "use: " + std::string(cmd->usage));
            continue;
        }
        // The values that can be checked before running.
        const auto& w = line.words;
        if ((w[0] == "wait" || w[0] == "wait-for") && !parseScriptSeconds(w[1])) {
            out.errors.push_back(where + "\"" + w[1] + "\" is not a number of seconds from 0 to 3600");
            continue;
        }
        const std::size_t opAt = w[0] == "expect" ? 2 : 3;  // the word that is an operator when there is one more
        if ((w[0] == "expect" || w[0] == "wait-for") && w.size() == opAt + 2 && !isCompareOperator(w[opAt])) {
            out.errors.push_back(where + "\"" + w[opAt] + "\" is not one of == != < <= > >=");
            continue;
        }
        if (w[0] == "add-track" && w[1] != "midi" && w[1] != "audio") {
            out.errors.push_back(where + "use: add-track midi|audio");
            continue;
        }
        if ((w[0] == "arm" || w[0] == "stop-on-failure") && w.back() != "on" && w.back() != "off") {
            out.errors.push_back(where + "use: " + std::string(cmd->usage));
            continue;
        }
        out.lines.push_back(std::move(line));
        if (end == text.size()) break;
    }
    return out;
}

std::optional<nlohmann::json> stateValue(const nlohmann::json& state, std::string_view path)
{
    const nlohmann::json* at = &state;
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t dot = path.find('.', start);
        if (dot == std::string_view::npos) dot = path.size();
        const std::string key(path.substr(start, dot - start));
        if (key.empty()) return std::nullopt;
        if (at->is_object()) {
            const auto it = at->find(key);
            if (it == at->end()) return std::nullopt;
            at = &*it;
        } else if (at->is_array()) {
            const auto index = parseNumber(key);
            if (!index || *index < 0 || std::floor(*index) != *index || *index >= static_cast<double>(at->size()))
                return std::nullopt;
            at = &(*at)[static_cast<std::size_t>(*index)];
        } else {
            return std::nullopt;
        }
        if (dot == path.size()) break;
        start = dot + 1;
    }
    return *at;
}

bool isCompareOperator(std::string_view op)
{
    return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
}

bool compareStateValue(const nlohmann::json& state, std::string_view path, std::string_view op,
                       std::string_view wanted, std::string& seen, std::string& error)
{
    error.clear();
    if (!isCompareOperator(op)) {
        error = "unknown operator " + std::string(op);
        return false;
    }
    const auto value = stateValue(state, path);
    seen = value ? value->dump() : std::string("(none)");
    const bool ordered = op != "==" && op != "!=";
    if (ordered) {
        const auto want = parseNumber(wanted);
        if (!want) {
            error = std::string(op) + " needs a number, not \"" + std::string(wanted) + "\"";
            return false;
        }
        if (!value || !value->is_number()) return false;
        const double v = value->get<double>();
        if (op == "<") return v < *want;
        if (op == "<=") return v <= *want;
        if (op == ">") return v > *want;
        return v >= *want;
    }
    bool equal = false;
    if (!value) {
        equal = false;
    } else if (wanted == "true" || wanted == "false") {
        equal = value->is_boolean() && value->get<bool>() == (wanted == "true");
    } else if (wanted == "null") {
        equal = value->is_null();
    } else if (const auto number = parseNumber(wanted); number && value->is_number()) {
        equal = value->get<double>() == *number;
    } else {
        equal = value->is_string() && value->get<std::string>() == wanted;
    }
    return op == "==" ? equal : !equal;
}

std::optional<double> parseScriptSeconds(std::string_view text)
{
    const auto v = parseNumber(text);
    if (!v || *v < 0.0 || *v > 3600.0) return std::nullopt;
    return v;
}

} // namespace minidaw
