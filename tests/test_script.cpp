// Script files: reading and checking them, state values (P25a, SPEC-RUN-02).

#include "minidaw/script.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace minidaw;
using nlohmann::json;

TEST_CASE("T-RUN-03: script words: spaces, quotes, backslashes, comments", "[run][script][T-RUN-03]")
{
    std::vector<std::string> w;
    std::string err;
    REQUIRE(splitScriptWords("  instrument 3   \"DX7 V\"  # the Arturia one", w, err));
    CHECK(w == std::vector<std::string>{"instrument", "3", "DX7 V"});
    REQUIRE(splitScriptWords(R"(open "E:\test\projects\zz")", w, err));
    CHECK(w == std::vector<std::string>{"open", R"(E:\test\projects\zz)"});  // backslashes kept
    REQUIRE(splitScriptWords(R"(echo "say \"hi\" \\ done")", w, err));
    CHECK(w == std::vector<std::string>{"echo", R"(say "hi" \ done)"});
    REQUIRE(splitScriptWords("echo a#b \"#x\"", w, err));
    CHECK(w == std::vector<std::string>{"echo", "a#b", "#x"});  // # only at the start of a word, outside quotes
    REQUIRE(splitScriptWords("\t# only a comment\r", w, err));
    CHECK(w.empty());
    REQUIRE(splitScriptWords("echo \"\"", w, err));
    CHECK(w == std::vector<std::string>{"echo", ""});
    CHECK_FALSE(splitScriptWords("open \"E:\\no end", w, err));
    CHECK(err == "a quote is not closed");

    // Which words were quoted (AA6); quotes inside a word are refused (AA7).
    std::vector<bool> q;
    REQUIRE(splitScriptWords("expect tracks.1.name \"true\"", w, err, &q));
    CHECK(q == std::vector<bool>{false, false, true});
    CHECK_FALSE(splitScriptWords("echo \"abc\"def", w, err));
    CHECK(err == "text right after a closing quote (put a space between words)");
    CHECK_FALSE(splitScriptWords("echo a\"b c\"", w, err));
    CHECK(err == "a quote inside a word (put the whole word in quotes)");
}

TEST_CASE("T-RUN-03: a script is checked as a whole before it runs", "[run][script][T-RUN-03]")
{
    const auto ok = parseScript("\xEF\xBB\xBF# test\r\n"
                                "OPEN \"E:\\test\\zz\"\r\n"
                                "\r\n"
                                "instrument 3 \"DX7 V\"\n"
                                "wait 1.5\n"
                                "wait-for 10 tracks.3.loaded true\n"
                                "expect log.errors == 0\n"
                                "add-track midi\n"
                                "arm 1 on\n"
                                "quit");
    CHECK(ok.errors.empty());
    REQUIRE(ok.lines.size() == 8);
    CHECK(ok.lines[0].number == 2);
    CHECK(ok.lines[0].words[0] == "open");  // the command in lower case, the rest as written
    CHECK(ok.lines[0].words[1] == "E:\\test\\zz");
    CHECK(ok.lines[1].number == 4);
    CHECK(ok.lines[1].text == "instrument 3 \"DX7 V\"");
    CHECK(ok.lines[7].words[0] == "quit");  // the last line had no line end
    CHECK(ok.lines[7].number == 10);

    // Fixed words in any case (AA5); a UTF-16 file (AA7).
    const auto anyCase = parseScript("add-track MIDI\narm last On\nstop-on-failure OFF\n");
    CHECK(anyCase.errors.empty());
    REQUIRE(anyCase.lines.size() == 3);
    CHECK(anyCase.lines[0].words[1] == "midi");
    CHECK(anyCase.lines[1].words[2] == "on");
    CHECK(anyCase.lines[1].words[1] == "last");
    CHECK(anyCase.lines[2].words[1] == "off");
    const auto utf16 = parseScript(std::string("\xFF\xFEo\0p\0e\0n\0", 10));
    REQUIRE(utf16.errors.size() == 1);
    CHECK(utf16.errors[0] == "line 1: the script is saved as UTF-16 (\"Unicode\"): save it as UTF-8");

    const auto bad = parseScript("plya\n"
                                 "instrument 3\n"
                                 "wait soon\n"
                                 "wait 4000\n"
                                 "expect playing ~ true\n"
                                 "add-track drums\n"
                                 "arm 1 yes\n"
                                 "echo \"open\n"
                                 "stop\n");
    CHECK(bad.lines.size() == 1);  // only stop
    REQUIRE(bad.errors.size() == 8);
    CHECK(bad.errors[0] == "line 1: unknown command \"plya\"");
    CHECK(bad.errors[1] == "line 2: use: instrument <track> <name or class ID>");
    CHECK(bad.errors[2] == "line 3: \"soon\" is not a number of seconds from 0 to 3600");
    CHECK(bad.errors[3] == "line 4: \"4000\" is not a number of seconds from 0 to 3600");
    CHECK(bad.errors[4] == "line 5: \"~\" is not one of == != < <= > >=");
    CHECK(bad.errors[5] == "line 6: use: add-track midi|audio");
    CHECK(bad.errors[6] == "line 7: use: arm <track> on|off");
    CHECK(bad.errors[7] == "line 8: a quote is not closed");

    // Every command has a usage that starts with its name and a text.
    for (const ScriptCommand& c : scriptCommands()) {
        INFO(c.name);
        CHECK(c.usage.substr(0, c.name.size()) == c.name);
        CHECK(!c.text.empty());
        CHECK(findScriptCommand(c.name) == &c);
    }
    CHECK(findScriptCommand("nope") == nullptr);
}

TEST_CASE("T-RUN-03: state values and expect", "[run][script][T-RUN-03]")
{
    const json state = {{"playing", false},
                        {"busy", nullptr},
                        {"position", 1.25},
                        {"tracks", {{"3", {{"instrument", "DX7 V"}, {"loaded", true}}}}},
                        {"log", {{"errors", 0}, {"warnings", 2}}},
                        {"list", {10, 20}}};
    CHECK(stateValue(state, "tracks.3.instrument") == json("DX7 V"));
    CHECK(stateValue(state, "list.1") == json(20));
    CHECK_FALSE(stateValue(state, "list.2"));
    CHECK_FALSE(stateValue(state, "tracks.4.instrument"));
    CHECK_FALSE(stateValue(state, "playing.x"));
    CHECK_FALSE(stateValue(state, "tracks..3"));
    CHECK_FALSE(stateValue(state, ""));

    std::string seen, err;
    auto holds = [&](const char* path, const char* op, const char* wanted) {
        return compareStateValue(state, path, op, wanted, seen, err);
    };
    CHECK(holds("tracks.3.instrument", "==", "DX7 V"));
    CHECK(seen == "\"DX7 V\"");
    CHECK_FALSE(holds("tracks.3.instrument", "==", "dx7 v"));  // text exactly
    CHECK(holds("tracks.3.instrument", "!=", "DCO-106"));
    CHECK(holds("tracks.3.loaded", "==", "true"));
    CHECK_FALSE(holds("tracks.3.loaded", "==", "1"));  // a bool is not a number
    CHECK(holds("playing", "==", "false"));
    CHECK(holds("busy", "==", "null"));
    CHECK(holds("position", "==", "1.25"));
    CHECK(holds("position", ">", "1"));
    CHECK(holds("log.warnings", "<=", "2"));
    CHECK_FALSE(holds("log.warnings", "<", "2"));
    CHECK(holds("log.errors", "==", "0"));
    CHECK_FALSE(holds("tracks.9.loaded", "==", "true"));
    CHECK(seen == "(none)");
    CHECK(holds("tracks.9.loaded", "!=", "true"));  // nothing there is not equal
    CHECK_FALSE(holds("tracks.3.instrument", ">", "1"));  // text is not ordered
    CHECK(err.empty());
    CHECK_FALSE(holds("position", ">", "much"));
    CHECK(err == "> needs a number, not \"much\"");

    // A word written in quotes is always text (AA6).
    const json named = {{"preset", "true"}, {"count", "12"}, {"flag", true}};
    CHECK(compareStateValue(named, "preset", "==", "true", seen, err, true));
    CHECK_FALSE(compareStateValue(named, "preset", "==", "true", seen, err, false));
    CHECK(compareStateValue(named, "count", "==", "12", seen, err, true));
    CHECK_FALSE(compareStateValue(named, "flag", "==", "true", seen, err, true));
    CHECK(compareStateValue(named, "flag", "==", "true", seen, err, false));

    // Text that is not UTF-8 (an odd device name) does not throw (AA1).
    const json odd = {{"output", std::string("Roland \xDCnit")}};
    CHECK_NOTHROW(stateText(odd, 2));
    CHECK(stateText(odd) == "{\"output\":\"Roland \xEF\xBF\xBDnit\"}");
    CHECK_NOTHROW(compareStateValue(odd, "output", "==", "x", seen, err));
    CHECK(seen == "\"Roland \xEF\xBF\xBDnit\"");

    CHECK(parseScriptSeconds("0") == 0.0);
    CHECK(parseScriptSeconds("2.5") == 2.5);
    CHECK_FALSE(parseScriptSeconds("-1"));
    CHECK_FALSE(parseScriptSeconds("3601"));
    CHECK_FALSE(parseScriptSeconds("1s"));
}
