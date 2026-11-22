#include "minidaw/mic_access.hpp"

namespace minidaw {

std::string micAccessWarning(MicAccess access)
{
    switch (access) {
    case MicAccess::Refused:
        return "macOS does not allow mini-daw to use the microphone, so audio inputs record silence. "
               "Allow it in System Settings > Privacy & Security > Microphone, then choose Settings > Audio devices... > "
               "Apply (or start mini-daw again).";
    case MicAccess::Restricted:
        return "This Mac does not allow programs to use the microphone (a policy set by its owner), so audio "
               "inputs record silence.";
    case MicAccess::NotAsked:
        return "macOS asks whether mini-daw may use the microphone. Until you allow it, audio inputs record "
               "silence; mini-daw opens the inputs again when you answer.";
    case MicAccess::NotNeeded:
    case MicAccess::Allowed:
        break;
    }
    return {};
}

#if !defined(__APPLE__)
MicAccess micAccess() { return MicAccess::NotNeeded; }
bool askMicAccess() { return false; }
std::uint64_t micAccessAnswers() { return 0; }
#endif

} // namespace minidaw
