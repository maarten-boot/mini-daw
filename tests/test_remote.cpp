// T-RUN-06: remote control, who may send what (P25b, SPEC-RUN-03).

#include "minidaw/config.hpp"
#include "minidaw/remote.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

using namespace minidaw;

TEST_CASE("T-RUN-06: networks and addresses", "[remote][T-RUN-06]")
{
    const auto lan = parseIpNetwork("192.168.1.0/24");
    REQUIRE(lan);
    CHECK(lan->mask == 0xFFFFFF00u);
    CHECK(lan->address == 0xC0A80100u);
    CHECK(parseIpNetwork("192.168.1.77/24")->address == 0xC0A80100u);  // host bits cleared
    CHECK(parseIpNetwork("10.1.2.3")->mask == 0xFFFFFFFFu);            // one address
    CHECK(parseIpNetwork("0.0.0.0/0")->mask == 0u);
    for (const char* bad : {"", "192.168.1", "192.168.1.256", "192.168.01.1", "1.2.3.4/33", "1.2.3.4/", "1.2.3.4/x",
                            "a.b.c.d", "1.2.3.4.5", "::1", "192.168.1.0/24x", "+1.2.3.4"}) {
        INFO(bad);
        CHECK_FALSE(parseIpNetwork(bad));
    }
    std::vector<std::string> wrong;
    const auto list = parseIpNetworks(" 192.168.1.0/24, 10.0.0.5  nonsense,,", wrong);
    REQUIRE(list.size() == 2);
    CHECK(wrong == std::vector<std::string>{"nonsense"});

    CHECK(isLoopbackAddress("127.0.0.1"));
    CHECK(isLoopbackAddress("127.12.0.9"));
    CHECK(isLoopbackAddress("::1"));
    CHECK(isLoopbackAddress("::ffff:127.0.0.1"));
    CHECK(isLoopbackAddress("::FFFF:127.0.0.1"));
    CHECK_FALSE(isLoopbackAddress("128.0.0.1"));
    CHECK_FALSE(isLoopbackAddress("192.168.1.20"));
    CHECK_FALSE(isLoopbackAddress("::2"));
    CHECK_FALSE(isLoopbackAddress(""));

    CHECK(inNetworks("192.168.1.20", list));
    CHECK(inNetworks("::ffff:192.168.1.20", list));  // IPv4 through an IPv6 socket
    CHECK(inNetworks("10.0.0.5", list));
    CHECK_FALSE(inNetworks("10.0.0.6", list));
    CHECK_FALSE(inNetworks("192.168.2.20", list));
    CHECK_FALSE(inNetworks("fe80::1", list));
}

TEST_CASE("T-RUN-06: Host header, token", "[remote][T-RUN-06]")
{
    for (const char* ok : {"127.0.0.1:7800", "127.0.0.1", "localhost:7800", "LOCALHOST", "192.168.1.5:7800", "[::1]:7800",
                           "[::1]"}) {
        INFO(ok);
        CHECK(remoteHostHeaderOk(ok));
    }
    // A name could be a web site's own, pointed at this computer (DNS rebinding).
    for (const char* bad : {"", "evil.example:7800", "evil.example", "127.0.0.1.evil.example", "localhost.evil:7800",
                            "127.0.0.1:", "127.0.0.1:x", "[::1", "[::1]x", "[evil]"}) {
        INFO(bad);
        CHECK_FALSE(remoteHostHeaderOk(bad));
    }
    CHECK(remoteTokenMatches("abc123", "abc123"));
    CHECK_FALSE(remoteTokenMatches("abc12", "abc123"));
    CHECK_FALSE(remoteTokenMatches("abc1234", "abc123"));
    CHECK_FALSE(remoteTokenMatches("abd123", "abc123"));
    CHECK_FALSE(remoteTokenMatches("", ""));  // no token set: never a match
    const std::string t = makeRemoteToken();
    CHECK(t.size() == 32);
    CHECK(t.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(makeRemoteToken() != t);
}

TEST_CASE("T-RUN-06: who may send what", "[remote][T-RUN-06]")
{
    std::vector<std::string> wrong;
    RemotePolicy policy;
    policy.networks = parseIpNetworks("192.168.1.0/24", wrong);
    policy.token = "0123456789abcdef0123456789abcdef";
    auto req = [](const char* addr, const char* auth = "", const char* host = "127.0.0.1:7800", bool origin = false) {
        RemoteRequest r;
        r.clientAddress = addr;
        r.authorization = auth;
        r.host = host;
        r.hasOrigin = origin;
        return r;
    };
    // This computer: no token needed.
    RemoteAccess a = checkRemoteAccess(req("127.0.0.1"), policy);
    CHECK(a.status == 0);
    CHECK(a.loopback);
    CHECK(checkRemoteAccess(req("::1", "", "[::1]:7800"), policy).status == 0);
    // Web pages: not even from this computer.
    CHECK(checkRemoteAccess(req("127.0.0.1", "", "127.0.0.1:7800", true), policy).status == 403);
    CHECK(checkRemoteAccess(req("127.0.0.1", "", "evil.example:7800"), policy).status == 403);
    // The allowed network: with the token only.
    CHECK(checkRemoteAccess(req("192.168.1.20", "", "192.168.1.10:7800"), policy).status == 401);
    CHECK(checkRemoteAccess(req("192.168.1.20", "Bearer wrong", "192.168.1.10:7800"), policy).status == 401);
    CHECK(checkRemoteAccess(req("192.168.1.20", "0123456789abcdef0123456789abcdef", "192.168.1.10:7800"), policy).status ==
          401);  // without "Bearer"
    a = checkRemoteAccess(req("192.168.1.20", "Bearer 0123456789abcdef0123456789abcdef", "192.168.1.10:7800"), policy);
    CHECK(a.status == 0);
    CHECK_FALSE(a.loopback);
    CHECK(checkRemoteAccess(req("192.168.1.20", "bearer  0123456789abcdef0123456789abcdef ", "192.168.1.10:7800"), policy)
              .status == 0);
    // Other networks: never, also with the token.
    CHECK(checkRemoteAccess(req("192.168.2.20", "Bearer 0123456789abcdef0123456789abcdef", "192.168.1.10:7800"), policy)
              .status == 403);
    // No networks allowed: only this computer.
    RemotePolicy local;
    CHECK(checkRemoteAccess(req("192.168.1.20", "Bearer x", "192.168.1.10:7800"), local).status == 403);

    // Commands: script-file settings never; what can replace a file only from this computer.
    std::string why;
    CHECK(remoteCommandAllowed("arm", false, why));
    CHECK(remoteCommandAllowed("export", true, why));
    CHECK_FALSE(remoteCommandAllowed("export", false, why));
    CHECK(why.find("only from this computer") != std::string::npos);
    CHECK_FALSE(remoteCommandAllowed("stay", true, why));
    CHECK_FALSE(remoteCommandAllowed("stop-on-failure", true, why));
    CHECK(remoteCommandAllowed("quit", false, why));

    std::string line;
    CHECK(parseRemoteCommandBody(R"({"line": "arm 1 on"})", line, why));
    CHECK(line == "arm 1 on");
    CHECK_FALSE(parseRemoteCommandBody("arm 1 on", line, why));
    CHECK_FALSE(parseRemoteCommandBody(R"(["arm"])", line, why));
    CHECK_FALSE(parseRemoteCommandBody(R"({"line": 5})", line, why));
    CHECK_FALSE(parseRemoteCommandBody(R"({"line": "play\nrecord"})", line, why));
    CHECK(why == "one command per request");
}

TEST_CASE("T-RUN-06: [remote] in the config", "[remote][config][T-RUN-06]")
{
    const ConfigLoadResult d = parseConfig(defaultConfigText());
    CHECK(d.warnings.empty());
    CHECK_FALSE(d.config.remote.enabled);
    CHECK(d.config.remote.port == 7800);
    CHECK(d.config.remote.allow.empty());
    CHECK(d.config.remote.token.empty());
    const ConfigLoadResult r =
        parseConfig("[remote]\nenabled = true\nport = 9000\nallow = 192.168.1.0/24, 10.0.0.5\ntoken = \"abc\"\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.remote.enabled);
    CHECK(r.config.remote.port == 9000);
    CHECK(r.config.remote.allow == "192.168.1.0/24, 10.0.0.5");
    CHECK(r.config.remote.token == "abc");
    const ConfigLoadResult bad = parseConfig("[remote]\nenabled = maybe\nport = 80\n");
    CHECK(bad.warnings.size() == 2);
    CHECK_FALSE(bad.config.remote.enabled);
    CHECK(bad.config.remote.port == 7800);
}
