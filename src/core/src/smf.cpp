#include "minidaw/smf.hpp"
#include "minidaw/path_util.hpp"

#include "file_util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <system_error>

namespace minidaw {

bool parseMidiTiming(const std::string& text, MidiTiming& out)
{
    std::string v;
    for (char c : text) v += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (v == "ppq") { out = MidiTiming::Ppq; return true; }
    if (v == "smpte") { out = MidiTiming::Smpte; return true; }
    return false;
}

const char* midiTimingName(MidiTiming timing)
{
    return timing == MidiTiming::Smpte ? "smpte" : "ppq";
}

std::uint32_t framesToTicks(std::uint64_t frame, unsigned int sampleRate, MidiTiming timing)
{
    if (sampleRate == 0) return 0;
    const double ticksPerSecond = timing == MidiTiming::Smpte
                                      ? double(kSmfSmpteFps) * kSmfSmpteSubframes
                                      : double(kSmfPpq) * 1e6 / kSmfTempoUsPerQuarter;
    const double ticks = std::round(static_cast<double>(frame) * ticksPerSecond / sampleRate);
    return static_cast<std::uint32_t>(std::min(ticks, 4294967295.0));
}

std::uint8_t channelMessageSize(std::uint8_t status)
{
    switch (status & 0xF0) {
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 3;
    case 0xC0: case 0xD0: return 2;
    default: return 0;  // system messages (SysEx, clock, ...) are not recorded
    }
}

std::vector<MidiEvent> buildMidiTake(const std::vector<TimedMidi>& input, int channel, std::uint64_t startFrame,
                                     std::uint64_t endFrame)
{
    // The session keeps its list sorted; only copy and sort if it is not.
    const auto byFrame = [](const TimedMidi& a, const TimedMidi& b) { return a.frame < b.frame; };
    std::vector<TimedMidi> copy;
    const std::vector<TimedMidi>* events = &input;
    if (!std::is_sorted(input.begin(), input.end(), byFrame)) {
        copy = input;
        std::stable_sort(copy.begin(), copy.end(), byFrame);
        events = &copy;
    }
    const std::vector<TimedMidi>& sorted = *events;

    std::vector<MidiEvent> out;
    std::array<std::array<std::uint16_t, 128>, 16> held{};  // note-on count per channel/note
    for (const TimedMidi& m : sorted) {
        if (m.size == 0) continue;
        const std::uint8_t status = m.data[0];
        const std::uint8_t len = channelMessageSize(status);
        if (len == 0 || m.size < len) continue;
        const int ch = (status & 0x0F) + 1;
        if (channel != 0 && ch != channel) continue;
        if (m.frame < startFrame || m.frame >= endFrame) continue;

        MidiEvent e;
        e.frame = m.frame - startFrame;
        e.data = m.data;
        e.size = len;
        out.push_back(e);

        const std::uint8_t kind = status & 0xF0;
        const std::uint8_t note = m.data[1] & 0x7F;
        auto& count = held[status & 0x0F][note];
        if (kind == 0x90 && m.data[2] != 0) {
            ++count;
        } else if ((kind == 0x80 || kind == 0x90) && count > 0) {
            --count;
        }
    }
    const std::uint64_t end = endFrame > startFrame ? endFrame - startFrame : 0;
    for (int ch = 0; ch < 16; ++ch) {
        for (int note = 0; note < 128; ++note) {
            for (std::uint16_t n = held[ch][note]; n > 0; --n) {
                MidiEvent e;
                e.frame = end;
                e.data = {static_cast<std::uint8_t>(0x80 | ch), static_cast<std::uint8_t>(note), 0};
                e.size = 3;
                out.push_back(e);
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------
namespace {

void put16be(std::vector<std::uint8_t>& b, std::uint16_t v)
{
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v));
}

void put32be(std::vector<std::uint8_t>& b, std::uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<std::uint8_t>(v >> s));
}

void putVlq(std::vector<std::uint8_t>& b, std::uint32_t v)
{
    std::uint8_t tmp[5];
    int n = 0;
    tmp[n++] = static_cast<std::uint8_t>(v & 0x7F);
    while ((v >>= 7) != 0) tmp[n++] = static_cast<std::uint8_t>(0x80 | (v & 0x7F));
    while (n > 0) b.push_back(tmp[--n]);
}

void putMeta(std::vector<std::uint8_t>& b, std::uint32_t delta, std::uint8_t type, const std::uint8_t* data,
             std::uint32_t len)
{
    putVlq(b, delta);
    b.push_back(0xFF);
    b.push_back(type);
    putVlq(b, len);
    b.insert(b.end(), data, data + len);
}

void putTrackName(std::vector<std::uint8_t>& b, const std::string& name)
{
    putMeta(b, 0, 0x03, reinterpret_cast<const std::uint8_t*>(name.data()), static_cast<std::uint32_t>(name.size()));
}

void putChunk(std::vector<std::uint8_t>& file, const std::vector<std::uint8_t>& track)
{
    file.insert(file.end(), {'M', 'T', 'r', 'k'});
    put32be(file, static_cast<std::uint32_t>(track.size()));
    file.insert(file.end(), track.begin(), track.end());
}

} // namespace

bool writeSmf(const std::filesystem::path& path, const std::string& takeName, const std::vector<SmfTrack>& tracks,
              unsigned int sampleRate, MidiTiming timing, std::uint64_t endFrame, bool syncToDisk,
              std::string& error)
{
    std::vector<std::uint8_t> file;
    file.insert(file.end(), {'M', 'T', 'h', 'd'});
    put32be(file, 6);
    put16be(file, 1);                                             // type 1
    put16be(file, static_cast<std::uint16_t>(tracks.size() + 1));
    if (timing == MidiTiming::Smpte) {
        const auto fps = static_cast<std::uint8_t>(static_cast<std::int8_t>(-kSmfSmpteFps));
        file.push_back(fps);
        file.push_back(static_cast<std::uint8_t>(kSmfSmpteSubframes));
    } else {
        put16be(file, kSmfPpq);
    }
    const std::uint32_t endTick = framesToTicks(endFrame, sampleRate, timing);

    // Track 0: take name, tempo (PPQ only), end of track.
    std::vector<std::uint8_t> t0;
    putTrackName(t0, takeName);
    if (timing == MidiTiming::Ppq) {
        const std::uint8_t tempo[3] = {static_cast<std::uint8_t>(kSmfTempoUsPerQuarter >> 16),
                                       static_cast<std::uint8_t>(kSmfTempoUsPerQuarter >> 8),
                                       static_cast<std::uint8_t>(kSmfTempoUsPerQuarter)};
        putMeta(t0, 0, 0x51, tempo, 3);
    }
    putMeta(t0, endTick, 0x2F, nullptr, 0);
    putChunk(file, t0);

    for (const SmfTrack& tr : tracks) {
        std::vector<std::uint8_t> b;
        putTrackName(b, tr.name);
        std::uint32_t last = 0;
        for (const MidiEvent& e : tr.events) {
            const std::uint32_t tick = std::max(last, framesToTicks(e.frame, sampleRate, timing));
            putVlq(b, tick - last);
            last = tick;
            b.insert(b.end(), e.data.begin(), e.data.begin() + e.size);
        }
        putMeta(b, std::max(endTick, last) - last, 0x2F, nullptr, 0);
        putChunk(file, b);
    }

    std::filesystem::path tmp = path;
    tmp += ".tmp";
    std::FILE* f = detail::openForWriting(tmp, false);
    if (f == nullptr) {
        error = "Creating " + pathToUtf8(tmp) + " failed.";
        return false;
    }
    bool ok = std::fwrite(file.data(), 1, file.size(), f) == file.size();
    ok = ok && (syncToDisk ? detail::syncToDisk(f) : std::fflush(f) == 0);
    ok = (std::fclose(f) == 0) && ok;
    std::error_code ec;
    if (!ok) {
        std::filesystem::remove(tmp, ec);
        error = "Writing " + pathToUtf8(path) + " failed (disk full or no permission?).";
        return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        error = "Replacing " + pathToUtf8(path) + " failed: " + ec.message();
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------
namespace {

struct Cursor {
    const std::uint8_t* p;
    const std::uint8_t* end;
    bool ok = true;
    std::uint8_t byte()
    {
        if (p >= end) { ok = false; return 0; }
        return *p++;
    }
    std::uint32_t vlq()
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const std::uint8_t b = byte();
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) return v;
        }
        ok = false;
        return v;
    }
};

std::uint32_t get32be(const std::uint8_t* p)
{
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

} // namespace

bool readSmf(const std::filesystem::path& path, SmfData& data, std::string& error)
{
    data = {};
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "Cannot open " + pathToUtf8(path) + ".";
        return false;
    }
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (bytes.size() < 14 || std::memcmp(bytes.data(), "MThd", 4) != 0 || get32be(&bytes[4]) < 6) {
        error = pathToUtf8(path) + " is not a MIDI file.";
        return false;
    }
    data.format = static_cast<std::uint16_t>((bytes[8] << 8) | bytes[9]);
    const unsigned int ntracks = (bytes[10] << 8) | bytes[11];
    data.division = static_cast<std::int16_t>((bytes[12] << 8) | bytes[13]);

    std::size_t pos = 8 + get32be(&bytes[4]);
    while (data.tracks.size() < ntracks && pos + 8 <= bytes.size()) {
        const std::uint32_t len = get32be(&bytes[pos + 4]);
        const bool isTrack = std::memcmp(&bytes[pos], "MTrk", 4) == 0;
        pos += 8;
        if (pos + len > bytes.size()) break;
        if (isTrack) {
            Cursor c{&bytes[pos], &bytes[pos] + len};
            std::vector<SmfReadEvent> events;
            std::uint32_t tick = 0;
            std::uint8_t running = 0;
            while (c.ok && c.p < c.end) {
                tick += c.vlq();
                std::uint8_t status = c.byte();
                SmfReadEvent ev;
                ev.tick = tick;
                if (status == 0xFF) {
                    ev.meta = true;
                    ev.metaType = c.byte();
                    const std::uint32_t n = c.vlq();
                    for (std::uint32_t i = 0; i < n && c.ok; ++i) ev.bytes.push_back(c.byte());
                    events.push_back(ev);
                    if (ev.metaType == 0x2F) break;
                    continue;
                }
                if (status == 0xF0 || status == 0xF7) {
                    const std::uint32_t n = c.vlq();
                    for (std::uint32_t i = 0; i < n && c.ok; ++i) c.byte();
                    continue;
                }
                std::uint8_t first;
                if (status < 0x80) {  // running status
                    first = status;
                    status = running;
                } else {
                    running = status;
                    first = c.byte();
                }
                const std::uint8_t size = channelMessageSize(status);
                if (size == 0) { c.ok = false; break; }
                ev.bytes = {status, first};
                if (size == 3) ev.bytes.push_back(c.byte());
                events.push_back(ev);
            }
            if (!c.ok) {
                error = pathToUtf8(path) + ": damaged track.";
                return false;
            }
            data.tracks.push_back(std::move(events));
        }
        pos += len;
    }
    if (data.tracks.size() != ntracks) {
        error = pathToUtf8(path) + ": missing tracks.";
        return false;
    }
    return true;
}

} // namespace minidaw
