#include "folder_chooser.hpp"
#include "state_cues.hpp"

#include "minidaw/folder_browse.hpp"
#include "minidaw/path_util.hpp"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#endif

namespace fs = std::filesystem;

namespace minidaw::app {

struct FolderChooser::Impl {
    enum class Mode { Idle, Native, Browser };
    Mode mode = Mode::Idle;
    std::string note;

    // Native dialog (Windows), on its own thread.
    std::thread thread;
    std::mutex mutex;
    std::atomic<bool> nativeDone{false};
    std::atomic<unsigned long> nativeThreadId{0};  // Windows thread id of the dialog's thread (for cancel)
    bool nativeChosen = false;     // with mutex
    bool nativeFailed = false;     // the dialog could not be made: use the browser
    fs::path nativeResult;         // with mutex
    std::string nativeError;       // with mutex

    // ImGui browser.
    std::string title;
    fs::path current;
    std::string pathInput;
    bool showHidden = false;
    bool listed = false;
    FolderListing listing;
    bool openPopup = false;
    fs::path start;                // where choosing began (also for the browser after a failed dialog)

    void go(const fs::path& folder)
    {
        current = folder;
        pathInput = pathToUtf8(folder);
        listed = false;
    }
};

#ifdef _WIN32
namespace {

// Runs on the dialog's thread. Returns false if the dialog could not be made.
bool runNativeDialog(HWND owner, const std::wstring& title, const fs::path& start, bool& chosen, fs::path& result,
                     std::string& error)
{
    chosen = false;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool made = false;
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                  reinterpret_cast<void**>(&dialog));
    if (SUCCEEDED(hr) && dialog) {
        made = true;
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
        dialog->SetTitle(title.c_str());
        if (!start.empty()) {
            IShellItem* folder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(start.c_str(), nullptr, IID_IShellItem,
                                                      reinterpret_cast<void**>(&folder))) &&
                folder) {
                dialog->SetFolder(folder);
                folder->Release();
            }
        }
        hr = dialog->Show(owner);
        if (SUCCEEDED(hr)) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)) && item) {
                PWSTR text = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &text)) && text) {
                    result = fs::path(text);
                    chosen = true;
                    CoTaskMemFree(text);
                } else {
                    error = "The folder chosen is not on a disk (choose a folder with a drive letter or network path).";
                }
                item->Release();
            }
        } else if (hr != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "The folder dialog failed (0x%08lX).", static_cast<unsigned long>(hr));
            error = buf;
        }
        dialog->Release();
    } else {
        char buf[96];
        std::snprintf(buf, sizeof buf, "The Windows folder dialog is not available (0x%08lX); mini-daw's own is used.",
                      static_cast<unsigned long>(hr));
        error = buf;
    }
    if (SUCCEEDED(init)) CoUninitialize();
    return made;
}

std::wstring widen(const std::string& utf8) { return pathFromUtf8(utf8).wstring(); }

} // namespace
#endif

FolderChooser::FolderChooser() : impl_(std::make_unique<Impl>()) {}

FolderChooser::~FolderChooser()
{
    if (impl_->thread.joinable()) impl_->thread.join();  // the dialog is modal to the main window
}

bool FolderChooser::active() const { return impl_->mode != Impl::Mode::Idle; }

bool FolderChooser::nativeOpen() const { return impl_->mode == Impl::Mode::Native && !impl_->nativeDone; }

#ifdef _WIN32
namespace {
BOOL CALLBACK closeThreadWindow(HWND hwnd, LPARAM)
{
    if (IsWindowVisible(hwnd)) PostMessageW(hwnd, WM_CLOSE, 0, 0);  // the dialog: same as Cancel
    return TRUE;
}
} // namespace
#endif

void FolderChooser::cancel()
{
    Impl& d = *impl_;
    if (d.mode == Impl::Mode::Browser) {
        d.mode = Impl::Mode::Idle;
        return;
    }
#ifdef _WIN32
    if (d.mode == Impl::Mode::Native && !d.nativeDone) {
        if (const unsigned long tid = d.nativeThreadId; tid != 0) EnumThreadWindows(tid, closeThreadWindow, 0);
    }
#endif
}

std::string FolderChooser::takeNote()
{
    std::string n;
    n.swap(impl_->note);
    return n;
}

void FolderChooser::open(GLFWwindow* owner, const std::string& title, const fs::path& start)
{
    Impl& d = *impl_;
    if (d.mode != Impl::Mode::Idle) return;
    d.note.clear();
    d.title = title;
    d.start = start.empty() ? homeFolder() : start;
#ifdef _WIN32
    if (!d.nativeFailed) {  // once it could not be made, the browser is used from then on
        if (d.thread.joinable()) d.thread.join();
        d.nativeDone = false;
        d.nativeChosen = false;
        d.nativeError.clear();
        d.mode = Impl::Mode::Native;
        HWND hwnd = owner ? glfwGetWin32Window(owner) : nullptr;
        d.nativeThreadId = 0;
        d.thread = std::thread([&d, hwnd, t = widen(title), start]() {
            d.nativeThreadId = GetCurrentThreadId();
            bool chosen = false;
            fs::path result;
            std::string error;
            const bool made = runNativeDialog(hwnd, t, start, chosen, result, error);
            {
                std::lock_guard<std::mutex> lock(d.mutex);
                d.nativeChosen = chosen;
                d.nativeResult = result;
                d.nativeError = error;
                d.nativeFailed = !made;
            }
            d.nativeDone = true;
        });
        return;
    }
#else
    (void)owner;
#endif
    d.mode = Impl::Mode::Browser;
    d.go(d.start);
    d.openPopup = true;
}

bool FolderChooser::update(fs::path& chosen)
{
    Impl& d = *impl_;
    if (d.mode == Impl::Mode::Native) {
        if (!d.nativeDone) return false;
        if (d.thread.joinable()) d.thread.join();
        bool ok = false;
        bool failed = false;
        {
            std::lock_guard<std::mutex> lock(d.mutex);
            ok = d.nativeChosen;
            chosen = d.nativeResult;
            d.note = d.nativeError;
            failed = d.nativeFailed;
        }
        d.mode = Impl::Mode::Idle;
        if (failed) {  // fall back to the browser now
            d.mode = Impl::Mode::Browser;
            d.go(d.start);
            d.openPopup = true;
            return false;
        }
        return ok;
    }
    if (d.mode != Impl::Mode::Browser) return false;

    const char* popupId = "Choose a folder###choose-folder";
    if (d.openPopup) {
        ImGui::OpenPopup(popupId);
        d.openPopup = false;
    }
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(44 * em, 30 * em), ImGuiCond_Appearing);
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    bool keepOpen = true;
    bool result = false;
    if (ImGui::BeginPopupModal(popupId, &keepOpen)) {
        if (!d.listed) {
            d.listing = listSubfolders(d.current, d.showHidden);
            d.listed = true;
        }
        ImGui::TextWrapped("%s", d.title.c_str());
        // Up, Home, the drives (Windows), hidden folders.
        const fs::path parent = d.current.parent_path();
        minidaw::app::beginDisabled(parent.empty() || parent == d.current);
        if (ImGui::Button("Up")) d.go(parent);
        minidaw::app::endDisabled();
        ImGui::SameLine();
        const fs::path home = homeFolder();
        minidaw::app::beginDisabled(home.empty());
        if (ImGui::Button("Home")) d.go(home);
        minidaw::app::endDisabled();
        const auto roots = filesystemRoots();
        if (roots.size() > 1) {
            for (const auto& r : roots) {
                ImGui::SameLine();
                std::string label = pathToUtf8(r);
                if (label.size() > 1 && (label.back() == '\\' || label.back() == '/')) label.pop_back();
                if (ImGui::Button(label.c_str())) d.go(r);
            }
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Show hidden folders", &d.showHidden)) d.listed = false;

        // The folder: typed or pasted, Enter goes there.
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##path", &d.pathInput, ImGuiInputTextFlags_EnterReturnsTrue)) {
            const fs::path to = browseStart(d.pathInput, d.current);
            d.go(to);
        }

        // Below the list, at a fixed height (the buttons do not move): a hint and the buttons.
        const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing();
        if (ImGui::BeginChild("folders", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
            if (!d.listing.error.empty()) ImGui::TextWrapped("%s", d.listing.error.c_str());
            if (d.listing.folders.empty() && d.listing.error.empty()) ImGui::TextUnformatted("(no folders inside)");
            // A folder name is shown as text, not used as a label: "##" in a name
            // would hide part of it or clash with another ID (V5).
            fs::path into;
            int row = 0;
            for (const auto& f : d.listing.folders) {
                const std::string name = pathToUtf8(f.filename()) + "/";
                ImGui::PushID(row++);
                const float x = ImGui::GetCursorPosX();
                if (ImGui::Selectable("##folder", false, ImGuiSelectableFlags_AllowOverlap)) into = f;
                ImGui::SameLine(x);
                ImGui::TextUnformatted(name.c_str());
                ImGui::PopID();
            }
            if (!into.empty()) d.go(into);
        }
        ImGui::EndChild();
        ImGui::TextUnformatted("Click a folder to open it. The folder shown above is chosen:");
        std::string chooseLabel = "Choose \"" + pathToUtf8(d.current.filename().empty() ? d.current : d.current.filename()) +
                                  "\"###choose";
        if (ImGui::Button(chooseLabel.c_str())) {
            chosen = d.current;
            result = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") ||
            (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::GetIO().WantTextInput)) {
            ImGui::CloseCurrentPopup();
            keepOpen = false;
        }
        ImGui::EndPopup();
    } else {
        keepOpen = false;  // closed with its X
    }
    if (result || !keepOpen) d.mode = Impl::Mode::Idle;
    return result;
}

} // namespace minidaw::app
