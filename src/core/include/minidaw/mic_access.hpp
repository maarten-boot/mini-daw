#pragma once
// The microphone permission on macOS (P34b, SPEC-TECH-07). macOS lets a
// program record only after the user allowed it; until then, or when the
// user said no, CoreAudio delivers silence without an error. mini-daw asks
// when it opens an input, and says so plainly when it may not record.
// Windows and Linux have no such permission: NotNeeded.

#include <cstdint>
#include <string>

namespace minidaw {

enum class MicAccess {
    NotNeeded,   // not macOS
    Allowed,
    NotAsked,    // macOS has not asked the user yet
    Refused,     // the user said no (System Settings can change it)
    Restricted,  // not allowed by a policy (parental controls, management)
};

// What macOS says now. Never shows a question.
MicAccess micAccess();

// Starts macOS's question when it has not been asked (NotAsked); the answer
// comes later, on another thread. True when the question was started. Not
// asked by a program without the microphone usage text in its Info.plist
// (macOS would end it): mini-daw has one, the test program does not.
bool askMicAccess();

// How many answers to askMicAccess() came in so far: a change means the
// permission may differ from when the stream was opened.
std::uint64_t micAccessAnswers();

// The line shown when mini-daw may not record (Refused, Restricted, and
// NotAsked while the question is open); empty otherwise.
std::string micAccessWarning(MicAccess access);

} // namespace minidaw
