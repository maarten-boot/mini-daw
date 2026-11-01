#include "console_capture.hpp"

#if defined(__LINUX_ALSA__)
#include <alsa/asoundlib.h>
#endif

#include <mutex>

namespace minidaw::detail {

#if defined(__LINUX_ALSA__)
namespace {
void silentAlsaHandler(const char* /*file*/, int /*line*/, const char* /*function*/, int /*err*/,
                       const char* /*fmt*/, ...)
{
}
} // namespace
#endif

void silenceAlsaMessages()
{
#if defined(__LINUX_ALSA__)
    static std::once_flag once;
    std::call_once(once, [] { snd_lib_error_set_handler(&silentAlsaHandler); });
#endif
}

} // namespace minidaw::detail
