#pragma once
// The test synth's editor view on macOS (P34c): an NSView in the host's view;
// a click on its left half lowers the gain, on its right half raises it.
// Plain C++ around the Objective-C side (testsynth_mac.mm).

namespace testsynth::mac {

using ClickFn = void (*)(void* context, bool louder);

// A child view of `parent` (an NSView*) of that size; nullptr on failure.
void* makeView(void* parent, int width, int height, ClickFn click, void* context);
void resizeView(void* view, int width, int height);
// Takes it out of its parent and releases it.
void removeView(void* view);

} // namespace testsynth::mac
