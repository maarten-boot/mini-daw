#pragma once

// Remote control over HTTP + JSON (P25b, SPEC-RUN-03): the script commands
// (SPEC-RUN-02) sent one at a time by another program, on this computer or,
// when allowed, from the local network.
//
// This part decides who may do what, apart from the network code (which is in
// the app, with cpp-httplib), so it can be tested on its own:
//   - from this computer (127.0.0.0/8, ::1): always, without a token;
//   - from the networks in `[remote] allow` (e.g. 192.168.1.0/24): with the
//     token (`Authorization: Bearer <token>`);
//   - nothing else.
// Web pages cannot use it: a request with an `Origin` header (what a browser
// sends for a page's request to another site) is refused, the Host header must
// be an address or "localhost" (no DNS rebinding), and a command must be sent
// as JSON (a page cannot send that to another site without asking first).
// Commands that delete or overwrite files (export, which replaces a file) are
// only for this computer; script-file settings (stay, stop-on-failure) are not
// commands here.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace minidaw {

inline constexpr int kDefaultRemotePort = 7800;

// An IPv4 network: "192.168.1.0/24", or one address ("192.168.1.20" = /32).
struct IpNetwork {
    std::uint32_t address = 0;  // host order, with the bits after the prefix cleared
    std::uint32_t mask = 0;
    std::string text;           // as written, for messages
};
std::optional<IpNetwork> parseIpNetwork(std::string_view text);
// "a, b c" -> networks; the words that are not one go to `bad`.
std::vector<IpNetwork> parseIpNetworks(std::string_view list, std::vector<std::string>& bad);

// The client's address as the server gives it ("192.168.1.20", "::1",
// "::ffff:192.168.1.20"): its IPv4 address, if it has one.
std::optional<std::uint32_t> clientIpv4(std::string_view address);
// 127.0.0.0/8, ::1 or ::ffff:127.x.x.x.
bool isLoopbackAddress(std::string_view address);
bool inNetworks(std::string_view address, const std::vector<IpNetwork>& networks);

// The Host header: an IP address or "localhost", with or without a port. A
// name would let a web site's own name point at this computer (DNS rebinding).
bool remoteHostHeaderOk(std::string_view host);

// Compares in a time that does not depend on where they differ.
bool remoteTokenMatches(std::string_view given, std::string_view token);
// 32 hex digits from the system's random source.
std::string makeRemoteToken();

// Who may send what, from the config.
struct RemotePolicy {
    std::vector<IpNetwork> networks;  // besides this computer
    std::string token;                // needed from the networks
};

// One request as the server saw it.
struct RemoteRequest {
    std::string clientAddress;
    std::string host;           // Host header
    bool hasOrigin = false;     // an Origin header (sent by browsers)
    std::string authorization;  // Authorization header
};

// The answer to who sent it: status 0 = allowed; else the HTTP status (401,
// 403) and why. `loopback` = from this computer.
struct RemoteAccess {
    int status = 0;
    std::string why;
    bool loopback = false;
};
RemoteAccess checkRemoteAccess(const RemoteRequest& request, const RemotePolicy& policy);

// Whether a command (the first word, lower case) may be sent remotely; false
// with `why` for script-file settings, and for commands that delete or
// overwrite files when the client is not on this computer.
bool remoteCommandAllowed(std::string_view command, bool loopback, std::string& why);

// The body of a command request: {"line": "arm 1 on"} -> the line. False with `why`.
bool parseRemoteCommandBody(std::string_view body, std::string& line, std::string& why);

} // namespace minidaw
