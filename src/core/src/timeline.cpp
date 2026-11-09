#include "minidaw/timeline.hpp"

#include "minidaw/path_util.hpp"
#include "minidaw/wav_reader.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <map>

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

bool leapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int daysInMonth(int y, int m)
{
    static constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && leapYear(y) ? 29 : kDays[m - 1];
}

struct Civil {
    std::int64_t year = 0;
    unsigned month = 0, day = 0;
    int hour = 0, minute = 0, second = 0;
};

// Inverse of daysFromCivil (with the time of day).
Civil civilFromSeconds(std::int64_t seconds)
{
    const std::int64_t days = seconds >= 0 ? seconds / 86400 : (seconds - 86399) / 86400;
    const std::int64_t t = seconds - days * 86400;
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    Civil c;
    c.day = doy - (153 * mp + 2) / 5 + 1;
    c.month = mp < 10 ? mp + 3 : mp - 9;
    c.year = static_cast<std::int64_t>(yoe) + era * 400 + (c.month <= 2);
    c.hour = static_cast<int>(t / 3600);
    c.minute = static_cast<int>(t / 60 % 60);
    c.second = static_cast<int>(t % 60);
    return c;
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
    if (end < name.size()) {  // "-N" for a second folder in the same second: 1-9 digits (T8)
        int n = 0;
        const std::size_t count = name.size() - end - 1;
        if (name[end] != '-' || count < 1 || count > 9 || !digits(name, end + 1, count, n)) return false;
    }
    if (mo < 1 || mo > 12 || d < 1 || d > daysInMonth(y, mo) || h > 23 || mi > 59 || s > 60) return false;
    seconds = daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
    return true;
}

std::int64_t continuousTime(std::int64_t clockSeconds)
{
    const Civil c = civilFromSeconds(clockSeconds);
    std::tm tm{};
    tm.tm_year = static_cast<int>(c.year - 1900);
    tm.tm_mon = static_cast<int>(c.month) - 1;
    tm.tm_mday = static_cast<int>(c.day);
    tm.tm_hour = c.hour;
    tm.tm_min = c.minute;
    tm.tm_sec = c.second;
    tm.tm_isdst = -1;  // the time zone's own rules decide
    const std::time_t t = std::mktime(&tm);
    if (t == static_cast<std::time_t>(-1)) return clockSeconds;
    return static_cast<std::int64_t>(t);
}

namespace {

bool isRecordingFile(const std::filesystem::path& p, TrackKind& kind)
{
    const std::string ext = pathToUtf8(p.extension());
    if (ext == ".wav" || ext == ".WAV") {
        kind = TrackKind::Audio;
        return true;
    }
    if (ext == ".mid" || ext == ".MID") {
        kind = TrackKind::Midi;
        return true;
    }
    return false;
}

void addFolder(std::vector<RecordingInfo>& out, const std::filesystem::path& folder, bool autoPart,
               const std::map<std::filesystem::path, const RecordingInfo*>& previous)
{
    RecordingInfo r;
    r.folder = folder;
    r.autoPart = autoPart;
    if (!recordingTime(pathToUtf8(folder.filename()), autoPart, r.clock)) return;
    r.start = continuousTime(r.clock);
    std::vector<std::pair<std::filesystem::path, std::uintmax_t>> files;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fe;
        TrackKind kind{};
        if (!it->is_regular_file(fe) || !isRecordingFile(it->path(), kind)) continue;
        const std::uintmax_t size = it->file_size(fe);
        files.emplace_back(it->path(), fe ? 0 : size);
    }
    if (files.empty()) return;  // not a recording (T11)
    std::sort(files.begin(), files.end());
    for (const auto& [path, size] : files) {
        r.files.push_back(path);
        r.fileSizes.push_back(size);
    }
    // Name, note and marks: small, read every time (they change without the files changing).
    if (std::string err; !loadRecordingNotes(folder, r.notes, err)) r.notesProblem = err;
    // Read before and unchanged: keep its lengths (T4).
    const auto known = previous.find(folder);
    if (known != previous.end() && known->second->files == r.files && known->second->fileSizes == r.fileSizes) {
        r.seconds = known->second->seconds;
        r.problems = known->second->problems;
        out.push_back(std::move(r));
        return;
    }
    for (const auto& path : r.files) {
        Track t;
        isRecordingFile(path, t.kind);
        t.take = path;
        constexpr unsigned int rate = 48000;  // WAV: its own rate below; MIDI: any rate gives seconds
        if (t.kind == TrackKind::Audio) {
            WavReader w;
            std::string err;
            if (w.open(path, err) && w.sampleRate() > 0) {
                r.seconds = std::max(r.seconds, static_cast<double>(w.frames()) / w.sampleRate());
            } else {
                r.problems.push_back(pathToUtf8(path.filename()) + ": " + (err.empty() ? "no sample rate" : err));
            }
        } else {
            r.seconds = std::max(r.seconds, static_cast<double>(takeLengthFrames(t, rate)) / rate);
        }
    }
    out.push_back(std::move(r));
}

} // namespace

std::vector<RecordingInfo> listRecordings(const std::filesystem::path& base, const std::vector<RecordingInfo>& previous)
{
    std::map<std::filesystem::path, const RecordingInfo*> known;
    for (const auto& p : previous) known[p.folder] = &p;
    std::vector<RecordingInfo> out;
    for (const auto& f : listTakeFolders(base)) addFolder(out, f, false, known);
    std::error_code ec;
    for (std::filesystem::directory_iterator it(base / "auto", ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_directory(ec)) addFolder(out, it->path(), true, known);
    }
    std::stable_sort(out.begin(), out.end(), [](const RecordingInfo& a, const RecordingInfo& b) {
        return a.start != b.start ? a.start < b.start : a.folder < b.folder;
    });
    return out;
}

std::string gapLabel(double seconds)
{
    // The unit is chosen by the rounded value: 3590 s is "+1 h 00 min", not "+60 min" (T9).
    char buf[48];
    const auto s = static_cast<long long>(seconds + 0.5);
    const long long m = (s + 30) / 60;
    if (s < 60) {
        std::snprintf(buf, sizeof buf, "+%lld s", s);
    } else if (m < 60) {
        std::snprintf(buf, sizeof buf, "+%lld min", m);
    } else if (m < 24 * 60) {
        std::snprintf(buf, sizeof buf, "+%lld h %02lld min", m / 60, m % 60);
    } else {
        const long long dd = (s + 43200) / 86400;
        std::snprintf(buf, sizeof buf, "+%lld day%s", dd, dd == 1 ? "" : "s");
    }
    return buf;
}

std::string clockLabel(std::int64_t seconds, bool withDate)
{
    const Civil c = civilFromSeconds(seconds);
    char buf[48];
    if (!withDate) {
        std::snprintf(buf, sizeof buf, "%02d:%02d", c.hour, c.minute);
    } else {
        std::snprintf(buf, sizeof buf, "%04lld-%02u-%02u %02d:%02d", static_cast<long long>(c.year), c.month, c.day,
                      c.hour, c.minute);
    }
    return buf;
}

TimelineLayout layoutTimeline(const std::vector<RecordingInfo>& recs, double pps, double maxRealGap, double gapWidth,
                              double minWidth, const std::vector<double>* minWidths)
{
    TimelineLayout l;
    double x = 0.0;          // the end of what is drawn so far (both rows)
    double prevEnd = 0.0;    // seconds (as RecordingInfo::start) where the recordings so far end
    double lastX = 0.0;      // where the last block starts
    double rowEnd[2] = {0.0, 0.0};  // per row (takes, auto parts): where its last block ends (T10)
    for (std::size_t i = 0; i < recs.size(); ++i) {
        const RecordingInfo& r = recs[i];
        const auto start = static_cast<double>(r.start);
        double bx = x;  // where this block starts
        if (i > 0) {
            const double gap = start - prevEnd;
            if (gap > maxRealGap) {
                l.gaps.push_back({x, gapWidth, gapLabel(gap)});
                bx = x + gapWidth;
            } else if (gap > 0) {
                bx = x + gap * pps;
            } else {
                // Overlaps the one before (another row, or the same second): from its own start.
                bx = std::max(lastX + (start - static_cast<double>(recs[i - 1].start)) * pps, lastX);
            }
        }
        const double least = minWidths != nullptr && i < minWidths->size() ? std::max(minWidth, (*minWidths)[i]) : minWidth;
        const double w = std::max(least, r.seconds * pps);
        double& row = rowEnd[r.autoPart ? 1 : 0];
        bx = std::max(bx, row);  // never on top of the row's block before it (T10)
        l.blocks.push_back({i, bx, w});
        lastX = bx;
        row = bx + w;
        x = std::max(x, bx + w);
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
