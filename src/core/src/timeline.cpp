#include "minidaw/timeline.hpp"

#include "minidaw/path_util.hpp"
#include "minidaw/wav_reader.hpp"

#include <algorithm>
#include <cstdio>

namespace minidaw {

namespace {

bool digits(const std::string& s, std::size_t at, std::size_t n, int& value)
{
    if (at + n > s.size()) return false;
    value = 0;
    for (std::size_t i = at; i < at + n; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        value = value * 10 + (s[i] - '0');
    }
    return true;
}

// Days since 1970-01-01 for a date (proleptic Gregorian).
std::int64_t daysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = static_cast<int>(y - era * 400);
    const int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

} // namespace

bool recordingTime(const std::string& name, bool autoPart, std::int64_t& seconds)
{
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    std::size_t end = 0;
    if (autoPart) {  // "YYYY-MM-DD HH-MM-SS"
        if (!(digits(name, 0, 4, y) && name.size() > 4 && name[4] == '-' && digits(name, 5, 2, mo) &&
              name.size() > 7 && name[7] == '-' && digits(name, 8, 2, d) && name.size() > 10 && name[10] == ' ' &&
              digits(name, 11, 2, h) && name.size() > 13 && name[13] == '-' && digits(name, 14, 2, mi) &&
              name.size() > 16 && name[16] == '-' && digits(name, 17, 2, s)))
            return false;
        end = 19;
    } else {  // "take-YYYYMMDD-HHMMSS"
        if (name.rfind("take-", 0) != 0) return false;
        if (!(digits(name, 5, 4, y) && digits(name, 9, 2, mo) && digits(name, 11, 2, d) && name.size() > 13 &&
              name[13] == '-' && digits(name, 14, 2, h) && digits(name, 16, 2, mi) && digits(name, 18, 2, s)))
            return false;
        end = 20;
    }
    if (end < name.size()) {  // "-N" for a second folder in the same second
        int n = 0;
        if (name[end] != '-' || !digits(name, end + 1, name.size() - end - 1, n)) return false;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) return false;
    seconds = daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
    return true;
}

namespace {

void addFolder(std::vector<RecordingInfo>& out, const std::filesystem::path& folder, bool autoPart)
{
    RecordingInfo r;
    r.folder = folder;
    r.autoPart = autoPart;
    if (!recordingTime(pathToUtf8(folder.filename()), autoPart, r.start)) return;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = pathToUtf8(it->path().extension());
        Track t;
        if (ext == ".wav" || ext == ".WAV") {
            t.kind = TrackKind::Audio;
        } else if (ext == ".mid" || ext == ".MID") {
            t.kind = TrackKind::Midi;
        } else {
            continue;
        }
        t.take = it->path();
        r.files.push_back(it->path());
        constexpr unsigned int rate = 48000;  // WAV: its own rate below; MIDI: any rate gives seconds
        if (t.kind == TrackKind::Audio) {
            WavReader w;
            std::string err;
            if (w.open(it->path(), err) && w.sampleRate() > 0) {
                r.seconds = std::max(r.seconds, static_cast<double>(w.frames()) / w.sampleRate());
            }
        } else {
            r.seconds = std::max(r.seconds, static_cast<double>(takeLengthFrames(t, rate)) / rate);
        }
    }
    std::sort(r.files.begin(), r.files.end());
    out.push_back(std::move(r));
}

} // namespace

std::vector<RecordingInfo> listRecordings(const std::filesystem::path& base)
{
    std::vector<RecordingInfo> out;
    for (const auto& f : listTakeFolders(base)) addFolder(out, f, false);
    std::error_code ec;
    for (std::filesystem::directory_iterator it(base / "auto", ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec)) addFolder(out, it->path(), true);
    }
    std::stable_sort(out.begin(), out.end(), [](const RecordingInfo& a, const RecordingInfo& b) {
        return a.start != b.start ? a.start < b.start : a.folder < b.folder;
    });
    return out;
}

std::string gapLabel(double seconds)
{
    char buf[48];
    const auto s = static_cast<long long>(seconds + 0.5);
    if (s < 60) {
        std::snprintf(buf, sizeof buf, "+%lld s", s);
    } else if (s < 3600) {
        std::snprintf(buf, sizeof buf, "+%lld min", (s + 30) / 60);
    } else if (s < 86400) {
        const long long m = (s + 30) / 60;
        std::snprintf(buf, sizeof buf, "+%lld h %02lld min", m / 60, m % 60);
    } else {
        const long long dd = (s + 43200) / 86400;
        std::snprintf(buf, sizeof buf, "+%lld day%s", dd, dd == 1 ? "" : "s");
    }
    return buf;
}

std::string clockLabel(std::int64_t seconds, bool withDate)
{
    const std::int64_t days = seconds >= 0 ? seconds / 86400 : (seconds - 86399) / 86400;
    const std::int64_t t = seconds - days * 86400;
    char buf[48];
    if (!withDate) {
        std::snprintf(buf, sizeof buf, "%02d:%02d", static_cast<int>(t / 3600), static_cast<int>(t / 60 % 60));
        return buf;
    }
    // Civil date from days (inverse of daysFromCivil).
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    std::snprintf(buf, sizeof buf, "%04lld-%02u-%02u %02d:%02d", static_cast<long long>(y + (m <= 2)), m, d,
                  static_cast<int>(t / 3600), static_cast<int>(t / 60 % 60));
    return buf;
}

TimelineLayout layoutTimeline(const std::vector<RecordingInfo>& recs, double pps, double maxRealGap, double gapWidth,
                              double minWidth)
{
    TimelineLayout l;
    double x = 0.0;
    double prevEnd = 0.0;  // seconds (as RecordingInfo::start) where the recordings so far end
    double lastX = 0.0;    // where the last block starts
    for (std::size_t i = 0; i < recs.size(); ++i) {
        const RecordingInfo& r = recs[i];
        const auto start = static_cast<double>(r.start);
        if (i > 0) {
            const double gap = start - prevEnd;
            if (gap > maxRealGap) {
                l.gaps.push_back({x, gapWidth, gapLabel(gap)});
                x += gapWidth;
            } else if (gap > 0) {
                x += gap * pps;
            } else {
                // Overlaps the one before (another row, or the same second): from its own start.
                x = std::max(lastX + (start - static_cast<double>(recs[i - 1].start)) * pps, lastX);
            }
        }
        const double w = std::max(minWidth, r.seconds * pps);
        l.blocks.push_back({i, x, w});
        lastX = x;
        x += w;
        prevEnd = std::max(prevEnd, start + r.seconds);  // the real end: gap labels give the real pause
        l.width = std::max(l.width, x);
    }
    return l;
}

int assignTakeFolder(std::vector<Track>& tracks, const std::filesystem::path& folder, std::string& error)
{
    std::vector<Track> files = tracksFromTakeFolder(folder, error);
    if (files.empty()) return 0;
    int placed = 0;
    for (Track& f : files) {
        const auto it = std::find_if(tracks.begin(), tracks.end(),
                                     [&](const Track& t) { return t.number == f.number && t.kind == f.kind; });
        if (it != tracks.end()) {
            it->take = f.take;
            it->takeStart = 0;
            ++placed;
            continue;
        }
        if (std::any_of(tracks.begin(), tracks.end(), [&](const Track& t) { return t.number == f.number; })) {
            f.number = nextTrackNumber(tracks);  // the number is taken by a track of the other kind
        }
        tracks.push_back(f);
        ++placed;
    }
    return placed;
}

} // namespace minidaw
