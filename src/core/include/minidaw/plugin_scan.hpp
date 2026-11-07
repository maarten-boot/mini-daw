#pragma once

// Finding VST3 instruments (SPEC-VST-01, P10a).
//
// The standard VST3 folders (and folders the user adds) are searched for
// modules (`*.vst3`, a bundle folder or a file). For each module the classes
// are read from its `moduleinfo.json` when it has one (the module is not
// loaded), else by loading it and asking its factory. Results are kept in a
// cache file, so an unchanged module is not loaded again. Scanning runs one
// module per step, so the GUI stays responsive and can show progress.
//
// A module that crashes mini-daw while it is loaded for scanning: before
// loading, its path is written to a marker file and removed afterwards; if
// the marker is still there at the next scan, that module is marked
// "crashed" and skipped (until the user asks to try it again).
//
// GUI thread only (modules are loaded on the thread that scans).

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace minidaw {

struct PluginClass {
    std::string modulePath;     // UTF-8
    std::string classId;        // 32 hex digits
    std::string name;
    std::string vendor;
    std::string version;
    std::string subCategories;  // e.g. "Instrument|Synth"
    bool instrument = false;    // an audio processor with "Instrument" in its subcategories
};

enum class ModuleStatus { Ok, NoInstruments, Failed, Crashed };
const char* moduleStatusName(ModuleStatus status);

struct ScannedModule {
    std::string path;           // UTF-8
    std::int64_t stamp = 0;     // last change of the module (to rescan only changed ones)
    ModuleStatus status = ModuleStatus::Ok;
    std::string error;          // Failed / Crashed: why
    bool fromModuleInfo = false;
    std::vector<PluginClass> classes;  // all audio processor classes (instruments marked)
};

struct PluginCache {
    std::vector<ScannedModule> modules;
    std::vector<PluginClass> instruments() const;
    const PluginClass* findClass(const std::string& classId) const;
};

std::string pluginCacheToJson(const PluginCache& cache);
bool pluginCacheFromJson(const std::string& text, PluginCache& cache);  // false: damaged (cache stays empty)
bool savePluginCache(const std::filesystem::path& file, const PluginCache& cache, std::string& error);
PluginCache loadPluginCache(const std::filesystem::path& file);         // missing/damaged: empty

// Windows: Common Files\VST3 and the per-user %LOCALAPPDATA%\Programs\Common\VST3;
// Linux: ~/.vst3, /usr/lib/vst3, /usr/local/lib/vst3.
std::vector<std::filesystem::path> standardVst3Folders();

// Checks a folder before it is added to the instrument folders (P19, SPEC-VST-01b):
// returns an error (it does not exist, is not a folder, is the top of a drive
// or file system, is already listed as an added or standard folder (same
// folder, also spelled otherwise), or 8 are added already), else empty. `note`
// gives remarks; the folder can still be added: no `.vst3` module is in it
// (yet), or it was not searched to the end (more than `maxEntries` entries:
// the window must not stall, crosscheck #14, V3), or it holds a standard folder
// (a Scan then searches all of it). `folder` is the cleaned text (cleanPathText).
std::string checkInstrumentFolder(const std::filesystem::path& folder, const std::vector<std::string>& added,
                                  const std::vector<std::filesystem::path>& standard, std::string& note,
                                  std::size_t maxEntries = 20000);

// Cleans `text` (cleanPathText), checks it and adds it to `added` (V10).
// Returns the error (nothing added) or empty; `note` as above.
std::string addInstrumentFolder(std::vector<std::string>& added, std::string_view text,
                                const std::vector<std::filesystem::path>& standard, std::string& note);

// Whether a `.vst3` module is below `folder`, looking at no more than
// `maxEntries` entries; `complete` is false when it stopped at that limit.
bool hasVst3Module(const std::filesystem::path& folder, std::size_t maxEntries, bool& complete);

// Every module below the folders (a `.vst3` folder is not searched inside), sorted.
std::vector<std::filesystem::path> findVst3Modules(const std::vector<std::filesystem::path>& folders);

// Last change of a module (its folder and the binary inside it), for the cache.
std::int64_t moduleStamp(const std::filesystem::path& module);

// The classes of one module: moduleinfo.json, else load it.
ScannedModule scanModule(const std::filesystem::path& module);

// If `marker` exists, the previous scan crashed mini-daw while loading the
// module it names: that module is marked Crashed in `cache` (added if it was
// not there yet, the usual case for a first scan) and the marker is removed.
// Returns the module's path, or empty if there was no marker (crosscheck #9, Q2).
std::string recordScanCrash(const std::filesystem::path& marker, PluginCache& cache);

class PluginScanner {
public:
    // `retryCrashed`: also try modules marked as crashed or failed earlier.
    void start(const std::vector<std::filesystem::path>& folders, PluginCache previous,
               std::filesystem::path markerFile, bool retryCrashed = false);
    // Scans the next module; false when all are done (then result() is complete).
    bool step();
    bool running() const { return next_ < pending_.size(); }
    std::size_t done() const { return next_; }
    std::size_t total() const { return pending_.size(); }
    std::string current() const;   // the module the next step scans
    const PluginCache& result() const { return result_; }
    // A module that crashed the previous scan (from the marker); empty if none.
    const std::string& crashedLastTime() const { return crashedLastTime_; }

private:
    std::vector<std::filesystem::path> pending_;
    std::size_t next_ = 0;
    PluginCache previous_;
    PluginCache result_;
    std::filesystem::path marker_;
    bool retry_ = false;
    std::string crashedLastTime_;
};

} // namespace minidaw
