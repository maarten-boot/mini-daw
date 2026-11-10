#pragma once

// Script files (P25a, SPEC-RUN-02): `mini-daw --script <file>` runs the GUI
// and does what the file says, one command per line, as if the user did it,
// then writes a report and quits. For tests with real devices and plugins
// (e.g. on the Windows machine).
//
// This part is the text handling, apart from the GUI: reading the file into
// words, checking each command against the list of commands, and comparing
// a value of mini-daw's state (JSON) with what an `expect` line says. The GUI
// runs the commands (main.cpp).
//
// Syntax: one command per line; words split at spaces; "double quotes" keep
// spaces in a word (inside them \" is a quote and \\ a backslash, any other
// backslash stays, so "E:\test\songs" is fine); # at the start of a word
// begins a comment up to the end of the line; empty lines are skipped.

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace minidaw {

struct ScriptLine {
    int number = 0;                  // line in the file, from 1
    std::string text;                // as written (without the line end)
    std::vector<std::string> words;  // words[0] = the command, in lower case
};

struct ScriptParse {
    std::vector<ScriptLine> lines;  // only lines with a command
    std::vector<std::string> errors;  // "line N: ..." (unknown command, wrong number of words, open quote)
};

// The commands, with how many words follow them and what they do (for --help
// and the errors).
struct ScriptCommand {
    std::string_view name;
    int minArgs = 0;
    int maxArgs = 0;  // -1 = any number
    std::string_view usage;  // "instrument <track> <name or class ID>"
    std::string_view text;   // one line
};
const std::vector<ScriptCommand>& scriptCommands();
const ScriptCommand* findScriptCommand(std::string_view name);

// Splits one line into words (see above). False with `error` for an open quote.
bool splitScriptWords(std::string_view line, std::vector<std::string>& words, std::string& error);
// The whole file: every line checked; a script with errors is not run at all.
ScriptParse parseScript(std::string_view text);

// The value at a dotted path in the state: "playing", "tracks.3.instrument",
// "log.errors". Nothing if there is none.
std::optional<nlohmann::json> stateValue(const nlohmann::json& state, std::string_view path);

// expect / wait-for: compares the state's value at `path` with `wanted` by
// `op` (==, !=, <, <=, >, >=; the last four for numbers only). `wanted` is
// read as true / false / null / a number when it looks like one, else text;
// text compares exactly. Returns true if it holds; `seen` is the value found
// (JSON text, or "(none)").
bool compareStateValue(const nlohmann::json& state, std::string_view path, std::string_view op,
                       std::string_view wanted, std::string& seen, std::string& error);
// The operator words compareStateValue knows.
bool isCompareOperator(std::string_view op);

// "1.5" -> 1.5 seconds; for wait and the time limits (0 to 3600). Nothing if not a number in range.
std::optional<double> parseScriptSeconds(std::string_view text);

} // namespace minidaw
