#include "minidaw/remote.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <cstdio>
#include <random>

namespace minidaw {

namespace {

std::string_view trimView(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

// "192.168.1.20" -> the address; strict: four numbers 0..255, no leading zeros, nothing else.
std::optional<std::uint32_t> parseIpv4(std::string_view s)
{
    std::uint32_t out = 0;
    for (int part = 0; part < 4; ++part) {
        if (part > 0) {
            if (s.empty() || s.front() != '.') return std::nullopt;
            s.remove_prefix(1);
        }
        std::size_t n = 0;
        unsigned int v = 0;
        while (n < s.size() && n < 4 && s[n] >= '0' && s[n] <= '9') {
            v = v * 10 + static_cast<unsigned int>(s[n] - '0');
            ++n;
        }
        if (n == 0 || n > 3 || v > 255 || (n > 1 && s[0] == '0')) return std::nullopt;
        out = (out << 8) | v;
        s.remove_prefix(n);
    }
    if (!s.empty()) return std::nullopt;
    return out;
}

std::string lowerText(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

std::optional<IpNetwork> parseIpNetwork(std::string_view text)
{
    text = trimView(text);
    int prefix = 32;
    std::string_view addr = text;
    if (const std::size_t slash = text.find('/'); slash != std::string_view::npos) {
        addr = text.substr(0, slash);
        const std::string_view bits = text.substr(slash + 1);
        if (bits.empty() || bits.size() > 2) return std::nullopt;
        prefix = 0;
        for (char c : bits) {
            if (c < '0' || c > '9') return std::nullopt;
            prefix = prefix * 10 + (c - '0');
        }
        if (prefix > 32) return std::nullopt;
    }
    const auto a = parseIpv4(addr);
    if (!a) return std::nullopt;
    IpNetwork n;
    n.mask = prefix == 0 ? 0u : ~0u << (32 - prefix);
    n.address = *a & n.mask;
    n.text = std::string(text);
    return n;
}

std::vector<IpNetwork> parseIpNetworks(std::string_view list, std::vector<std::string>& bad)
{
    std::vector<IpNetwork> out;
    std::string word;
    auto flush = [&] {
        if (word.empty()) return;
        if (auto n = parseIpNetwork(word)) {
            out.push_back(*n);
        } else {
            bad.push_back(word);
        }
        word.clear();
    };
    for (char c : list) {
        if (c == ',' || std::isspace(static_cast<unsigned char>(c))) {
            flush();
        } else {
            word += c;
        }
    }
    flush();
    return out;
}

std::optional<std::uint32_t> clientIpv4(std::string_view address)
{
    address = trimView(address);
    const std::string lower = lowerText(address);
    if (lower.rfind("::ffff:", 0) == 0) return parseIpv4(std::string_view(lower).substr(7));
    return parseIpv4(address);
}

bool isLoopbackAddress(std::string_view address)
{
    const std::string lower = lowerText(trimView(address));
    if (lower == "::1" || lower == "0:0:0:0:0:0:0:1") return true;
    const auto v4 = clientIpv4(lower);
    return v4 && (*v4 >> 24) == 127;
}

bool inNetworks(std::string_view address, const std::vector<IpNetwork>& networks)
{
    const auto v4 = clientIpv4(address);
    if (!v4) return false;
    for (const IpNetwork& n : networks) {
        if ((*v4 & n.mask) == n.address) return true;
    }
    return false;
}

bool remoteHostHeaderOk(std::string_view host)
{
    host = trimView(host);
    if (host.empty()) return false;
    std::string_view name = host;
    if (host.front() == '[') {  // [::1]:7800
        const std::size_t close = host.find(']');
        if (close == std::string_view::npos) return false;
        const std::string_view rest = host.substr(close + 1);
        if (!rest.empty() && rest.front() != ':') return false;
        const std::string_view inside = host.substr(1, close - 1);
        if (inside.empty()) return false;
        for (char c : inside) {
            if (!(std::isxdigit(static_cast<unsigned char>(c)) || c == ':' || c == '.')) return false;
        }
        return true;
    }
    if (const std::size_t colon = host.rfind(':'); colon != std::string_view::npos) {
        const std::string_view port = host.substr(colon + 1);
        if (port.empty()) return false;
        for (char c : port) {
            if (c < '0' || c > '9') return false;
        }
        name = host.substr(0, colon);
    }
    return lowerText(name) == "localhost" || parseIpv4(name).has_value();
}

bool remoteTokenMatches(std::string_view given, std::string_view token)
{
    if (token.empty()) return false;
    unsigned int diff = given.size() == token.size() ? 0u : 1u;
    for (std::size_t i = 0; i < token.size(); ++i) {
        const unsigned char a = i < given.size() ? static_cast<unsigned char>(given[i]) : 0;
        diff |= static_cast<unsigned int>(a ^ static_cast<unsigned char>(token[i]));
    }
    return diff == 0;
}

std::string makeRemoteToken()
{
    std::random_device rd;
    std::string out;
    for (int i = 0; i < 4; ++i) {
        char part[9];
        std::snprintf(part, sizeof part, "%08x", static_cast<unsigned int>(rd()));
        out += part;
    }
    return out;
}

RemoteAccess checkRemoteAccess(const RemoteRequest& request, const RemotePolicy& policy)
{
    RemoteAccess a;
    a.loopback = isLoopbackAddress(request.clientAddress);
    if (request.hasOrigin) {
        a.status = 403;
        a.why = "requests from web pages are not accepted";
        return a;
    }
    if (!remoteHostHeaderOk(request.host)) {
        a.status = 403;
        a.why = "the Host header must be an IP address or localhost";
        return a;
    }
    if (a.loopback) return a;
    if (!inNetworks(request.clientAddress, policy.networks)) {
        a.status = 403;
        a.why = request.clientAddress + " is not in the allowed networks";
        return a;
    }
    std::string_view auth = trimView(request.authorization);
    constexpr std::string_view kBearer = "bearer ";
    if (auth.size() < kBearer.size() || lowerText(auth.substr(0, kBearer.size())) != kBearer ||
        !remoteTokenMatches(trimView(auth.substr(kBearer.size())), policy.token)) {
        a.status = 401;
        a.why = "a token is needed from another computer (Authorization: Bearer <token>)";
        return a;
    }
    return a;
}

bool remoteCommandAllowed(std::string_view command, bool loopback, std::string& why)
{
    if (command == "stay" || command == "stop-on-failure") {
        why = "\"" + std::string(command) + "\" is for script files only";
        return false;
    }
    if (!loopback && command == "export") {
        why = "export can replace a file: only from this computer";
        return false;
    }
    return true;
}

bool parseRemoteCommandBody(std::string_view body, std::string& line, std::string& why)
{
    const nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        why = "the body must be JSON: {\"line\": \"<command>\"}";
        return false;
    }
    const auto it = j.find("line");
    if (it == j.end() || !it->is_string()) {
        why = "the body has no \"line\" text";
        return false;
    }
    line = it->get<std::string>();
    if (line.find('\n') != std::string::npos || line.find('\r') != std::string::npos) {
        why = "one command per request";
        return false;
    }
    return true;
}

} // namespace minidaw
