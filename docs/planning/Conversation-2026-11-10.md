# Conversation log 2026-11-10

Continues `Conversation-2026-11-09.md` (P23 Terms tab, P24 Linux editor, crosscheck #18).

## 1. Crosscheck #18 fixes (P24a)

Claude: "Shall I fix Z1–Z9, starting with Z2?" User: "yes".

- Z2: the run loop and the editor's frame release handlers (and with them plugin objects) only after their lists are consistent, so a destructor that unregisters is safe.
- Z1: a reader whose descriptor was closed is dropped without a call; the app logs a warning.
- Z3: Edit on Linux only when GLFW runs on X11; an error instead of a crash otherwise.
- Z4: test synth "Editor test mode" (register in setFrame, close the pipe without unregistering, unregister in the destructor), tested in T-VST-38; Z1 / Z2 unit tests in T-VST-37.
- Z5: unregister matches reader / timer. Z6: T-VST-40 step for typing in a plugin's window.
- Z7 / Z8: Terms mark cleared by a new search; the result list for any word.
- Z9: Plan P10d, CLAUDE.md, Spec (v0.51) updated.
- Linux 209/209 under xvfb-run; run-loop tests clean under AddressSanitizer; MinGW under Wine 204/204.

## 2. CI of `acb4271`

User: "pushed it, check the CI run". Run 42881121369: windows, linux, licence green; Linux 209/209 under `xvfb-run` (T-VST-39 passed), Windows 206/206. Both builds checked (the new texts are in them), then `dist/` refilled; `dist/TESTS.txt` notes the crosscheck #18 fixes and the extra steps in T-VST-40 and T-UI-28.

## 3. Other plugin formats

User: "are different plugin formats common on Mac and Linux, should we (in the future) add other formats besides VST3?" Claude: AU is Apple's own (Logic / GarageBand load only AU; Apple's built-in instruments are AU only), CLAP is new and cross-platform but plugins ship it next to VST3, LV2 is Linux's open format (mostly effects; instruments also VST3 now), VST2 is no longer licensed, AAX is Pro Tools only. Advice: stay with VST3; AU with a Mac version; CLAP on request; a small format layer first. User: "yes add it to the plan" → Plan P12a (later).
