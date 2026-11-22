// The microphone permission on macOS (P34b): AVFoundation's authorization
// for audio capture. Built with ARC.

#include "minidaw/mic_access.hpp"

#import <AVFoundation/AVFoundation.h>

#include <atomic>

namespace minidaw {

namespace {
std::atomic<std::uint64_t> answers{0};
std::atomic<bool> asking{false};
} // namespace

MicAccess micAccess()
{
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]) {
    case AVAuthorizationStatusAuthorized: return MicAccess::Allowed;
    case AVAuthorizationStatusNotDetermined: return MicAccess::NotAsked;
    case AVAuthorizationStatusDenied: return MicAccess::Refused;
    case AVAuthorizationStatusRestricted: return MicAccess::Restricted;
    }
    return MicAccess::Refused;
}

bool askMicAccess()
{
    if (micAccess() != MicAccess::NotAsked) return false;
    // macOS ends a program that asks without saying why (the usage text in
    // its Info.plist; mini-daw carries one, see res/macos/Info.plist.in).
    // Without it (e.g. the test program) nothing is asked.
    if ([[NSBundle mainBundle] objectForInfoDictionaryKey:@"NSMicrophoneUsageDescription"] == nil) return false;
    if (asking.exchange(true)) return false;  // one question at a time
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL) {
                                 asking.store(false);
                                 answers.fetch_add(1);
                             }];
    return true;
}

std::uint64_t micAccessAnswers() { return answers.load(); }

} // namespace minidaw
