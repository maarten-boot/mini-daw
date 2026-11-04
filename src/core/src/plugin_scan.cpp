#include "minidaw/plugin_scan.hpp"

#include "console_capture.hpp"
#include "no_error_dialogs.hpp"
#include "minidaw/path_util.hpp"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/moduleinfo/moduleinfoparser.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#endif

namespace minidaw {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

bool hasInstrument(const std::string& subCategories)
{
    // "Instrument", "Instrument|Synth", "Fx|Instrument" ...: one of the '|' parts.
    std::size_t start = 0;
    while (start <= subCategories.size()) {
        const std::size_t end = std::min(subCategories.find('|', start), subCategories.size());
        if (subCategories.compare(start, end - start, "Instrument") == 0) return true;
        start = end + 1;
    }
    return false;
}

PluginClass makeClass(const std::string& module, std::string cid, std::string name, std::string vendor,
                      std::string version, std::string sub)
{
    PluginClass c;
    c.modulePath = module;
    c.classId = std::move(cid);
    c.name = std::move(name);
    c.vendor = std::move(vendor);
    c.version = std::move(version);
    c.subCategories = std::move(sub);
    c.instrument = hasInstrument(c.subCategories);
    return c;
}

std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void finish(ScannedModule& m)
{
    if (m.status == ModuleStatus::Ok &&
        std::none_of(m.classes.begin(), m.classes.end(), [](const PluginClass& c) { return c.instrument; })) {
        m.status = ModuleStatus::NoInstruments;
    }
}

} // namespace

const char* moduleStatusName(ModuleStatus s)
{
    switch (s) {
    case ModuleStatus::Ok:            return "ok";
    case ModuleStatus::NoInstruments: return "no instruments";
    case ModuleStatus::Failed:        return "failed";
    case ModuleStatus::Crashed:       return "crashed";
    }
    return "ok";
}

std::vector<PluginClass> PluginCache::instruments() const
{
    std::vector<PluginClass> out;
    for (const auto& m : modules) {
        for (const auto& c : m.classes) {
            if (c.instrument) out.push_back(c);
        }
    }
    std::sort(out.begin(), out.end(), [](const PluginClass& a, const PluginClass& b) {
        return a.vendor != b.vendor ? a.vendor < b.vendor : a.name < b.name;
    });
    return out;
}

const PluginClass* PluginCache::findClass(const std::string& classId) const
{
    for (const auto& m : modules) {
        for (const auto& c : m.classes) {
            if (c.classId == classId) return &c;
        }
    }
    return nullptr;
}

// --- Cache file --------------------------------------------------------------
std::string pluginCacheToJson(const PluginCache& cache)
{
    json mods = json::array();
    for (const auto& m : cache.modules) {
        json classes = json::array();
        for (const auto& c : m.classes) {
            classes.push_back({{"classId", c.classId},
                               {"name", c.name},
                               {"vendor", c.vendor},
                               {"version", c.version},
                               {"subCategories", c.subCategories}});
        }
        mods.push_back({{"path", m.path},
                        {"stamp", m.stamp},
                        {"status", moduleStatusName(m.status)},
                        {"error", m.error},
                        {"fromModuleInfo", m.fromModuleInfo},
                        {"classes", std::move(classes)}});
    }
    json root = {{"format", "mini-daw plugin cache"}, {"version", 1}, {"modules", std::move(mods)}};
    return root.dump(2, ' ', false, json::error_handler_t::replace) + "\n";
}

bool pluginCacheFromJson(const std::string& text, PluginCache& cache)
{
    cache = {};
    try {
        const json root = json::parse(text, nullptr, false);
        if (root.is_discarded() || !root.is_object() || !root.contains("modules") || !root["modules"].is_array() ||
            !root.contains("format") || !root["format"].is_string() ||
            root["format"].get<std::string>() != "mini-daw plugin cache") {
            return false;
        }
        for (const json& jm : root["modules"]) {
            ScannedModule m;
            m.path = jm.value("path", std::string());
            if (m.path.empty()) continue;
            m.stamp = jm.value("stamp", std::int64_t{0});
            const std::string st = jm.value("status", std::string("failed"));
            m.status = st == "ok"               ? ModuleStatus::Ok
                       : st == "no instruments" ? ModuleStatus::NoInstruments
                       : st == "crashed"        ? ModuleStatus::Crashed
                                                : ModuleStatus::Failed;
            m.error = jm.value("error", std::string());
            m.fromModuleInfo = jm.value("fromModuleInfo", false);
            if (jm.contains("classes") && jm["classes"].is_array()) {
                for (const json& jc : jm["classes"]) {
                    m.classes.push_back(makeClass(m.path, jc.value("classId", std::string()),
                                                  jc.value("name", std::string()), jc.value("vendor", std::string()),
                                                  jc.value("version", std::string()),
                                                  jc.value("subCategories", std::string())));
                }
            }
            cache.modules.push_back(std::move(m));
        }
        return true;
    } catch (const std::exception&) {
        cache = {};
        return false;
    }
}

bool savePluginCache(const fs::path& file, const PluginCache& cache, std::string& error)
{
    std::error_code ec;
    if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
    fs::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << pluginCacheToJson(cache);
        if (!out) {
            error = "Could not write " + pathToUtf8(tmp) + ".";
            return false;
        }
    }
    fs::rename(tmp, file, ec);
    if (ec) {
        error = "Could not replace " + pathToUtf8(file) + ": " + ec.message();
        return false;
    }
    return true;
}

PluginCache loadPluginCache(const fs::path& file)
{
    PluginCache c;
    std::error_code ec;
    if (fs::exists(file, ec)) pluginCacheFromJson(readFile(file), c);
    return c;
}

// --- Folders and modules ---------------------------------------------------------
std::vector<fs::path> standardVst3Folders()
{
    std::vector<fs::path> out;
#if defined(_WIN32)
    auto known = [&](REFKNOWNFOLDERID id) {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) out.push_back(fs::path(p) / "VST3");
        CoTaskMemFree(p);
    };
    known(FOLDERID_ProgramFilesCommon);      // C:\Program Files\Common Files\VST3
    known(FOLDERID_UserProgramFilesCommon);  // %LOCALAPPDATA%\Programs\Common\VST3
#else
    if (const char* home = std::getenv("HOME")) out.push_back(fs::path(home) / ".vst3");
    out.emplace_back("/usr/lib/vst3");
    out.emplace_back("/usr/local/lib/vst3");
#endif
    return out;
}

std::vector<fs::path> findVst3Modules(const std::vector<fs::path>& folders)
{
    std::vector<fs::path> out;
    for (const auto& folder : folders) {
        std::error_code ec;
        if (!fs::is_directory(folder, ec)) continue;
        fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            const fs::path& p = it->path();
            if (p.extension() == ".vst3") {
                out.push_back(p);
                if (it->is_directory(ec)) it.disable_recursion_pending();  // a bundle: not inside
            }
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::int64_t moduleStamp(const fs::path& module)
{
    // The newest change time of the module and its files. (The file clock's
    // epoch is not 1970: libstdc++ counts from 2174, so values can be
    // negative; only equality matters here.)
    std::error_code ec;
    std::int64_t stamp = 0;
    bool any = false;
    auto take = [&](const fs::path& p) {
        const auto t = fs::last_write_time(p, ec);
        if (ec) return;
        const std::int64_t v = t.time_since_epoch().count();
        stamp = any ? std::max(stamp, v) : v;
        any = true;
    };
    take(module);
    if (fs::is_directory(module, ec)) {
        for (fs::recursive_directory_iterator it(module, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_regular_file(ec)) take(it->path());
        }
    }
    return stamp;
}

ScannedModule scanModule(const fs::path& module)
{
    ScannedModule m;
    m.path = pathToUtf8(module);
    m.stamp = moduleStamp(module);
    const std::string pathText = m.path;

    // 1. moduleinfo.json: no loading needed.
    if (const auto info = VST3::Hosting::Module::getModuleInfoPath(pathText)) {
        std::ostringstream err;
        if (const auto parsed = Steinberg::ModuleInfoLib::parseJson(readFile(pathFromUtf8(*info)), &err)) {
            for (const auto& c : parsed->classes) {
                if (c.category != kVstAudioEffectClass) continue;
                std::string sub;
                for (const auto& s : c.subCategories) sub += (sub.empty() ? "" : "|") + s;
                m.classes.push_back(makeClass(m.path, c.cid, c.name, c.vendor, c.version, sub));
            }
            m.fromModuleInfo = true;
            finish(m);
            return m;
        }
        // A damaged moduleinfo.json: fall back to loading the module.
    }

    // 2. Load the module and ask its factory.
    detail::ConsoleCapture capture;  // some modules print while loading
    detail::NoErrorDialogs noDialogs;
    std::string error;
    try {
        auto mod = VST3::Hosting::Module::create(pathText, error);
        if (!mod) {
            m.status = ModuleStatus::Failed;
            m.error = error.empty() ? std::string("The module could not be loaded.") : error;
            while (!m.error.empty() && (m.error.back() == '\n' || m.error.back() == '\r')) m.error.pop_back();
            return m;
        }
        for (const auto& c : mod->getFactory().classInfos()) {
            if (c.category() != kVstAudioEffectClass) continue;
            m.classes.push_back(makeClass(m.path, c.ID().toString(), c.name(), c.vendor(), c.version(),
                                          c.subCategoriesString()));
        }
    } catch (const std::exception& e) {
        m.status = ModuleStatus::Failed;
        m.error = e.what();
        return m;
    }
    finish(m);
    return m;
}

// --- Scanner -------------------------------------------------------------------
void PluginScanner::start(const std::vector<fs::path>& folders, PluginCache previous, fs::path markerFile,
                          bool retryCrashed)
{
    pending_ = findVst3Modules(folders);
    next_ = 0;
    previous_ = std::move(previous);
    result_ = {};
    marker_ = std::move(markerFile);
    retry_ = retryCrashed;
    crashedLastTime_ = marker_.empty() ? std::string() : recordScanCrash(marker_, previous_);
}

std::string recordScanCrash(const fs::path& marker, PluginCache& cache)
{
    std::error_code ec;
    if (!fs::exists(marker, ec)) return {};
    // The previous scan did not finish this module: it crashed mini-daw.
    std::string crashed = readFile(marker);
    while (!crashed.empty() && (crashed.back() == '\n' || crashed.back() == '\r')) crashed.pop_back();
    fs::remove(marker, ec);
    if (crashed.empty()) return {};
    const char* why = "mini-daw stopped while loading this module during a scan.";
    bool known = false;
    for (auto& m : cache.modules) {
        if (m.path == crashed) {
            m.status = ModuleStatus::Crashed;
            m.error = why;
            m.classes.clear();
            known = true;
        }
    }
    if (!known) {
        ScannedModule m;
        m.path = crashed;
        m.stamp = moduleStamp(pathFromUtf8(m.path));
        m.status = ModuleStatus::Crashed;
        m.error = why;
        cache.modules.push_back(std::move(m));
    }
    return crashed;
}

std::string PluginScanner::current() const
{
    return next_ < pending_.size() ? pathToUtf8(pending_[next_]) : std::string();
}

bool PluginScanner::step()
{
    if (next_ >= pending_.size()) return false;
    const fs::path module = pending_[next_++];
    const std::string path = pathToUtf8(module);
    const std::int64_t stamp = moduleStamp(module);
    const auto old = std::find_if(previous_.modules.begin(), previous_.modules.end(),
                                  [&](const ScannedModule& m) { return m.path == path; });
    const bool unchanged = old != previous_.modules.end() && old->stamp == stamp;
    const bool skipBad = old != previous_.modules.end() && !retry_ &&
                         (old->status == ModuleStatus::Crashed || old->status == ModuleStatus::Failed);
    if ((unchanged && old->status != ModuleStatus::Failed && old->status != ModuleStatus::Crashed) || skipBad) {
        result_.modules.push_back(*old);  // reuse (or keep skipping a bad one)
    } else {
        std::error_code ec;
        if (!marker_.empty()) {
            if (marker_.has_parent_path()) fs::create_directories(marker_.parent_path(), ec);
            std::ofstream(marker_, std::ios::binary | std::ios::trunc) << path << "\n";
        }
        result_.modules.push_back(scanModule(module));
        if (!marker_.empty()) fs::remove(marker_, ec);
    }
    return next_ < pending_.size();
}

} // namespace minidaw
