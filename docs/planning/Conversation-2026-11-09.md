# Conversation log 2026-11-09

Continues `Conversation-2026-11-08.md` (PL1, crosscheck #15, PL2).

## 1. CI of `f8cf624`, crosscheck #16
> pushed it, check the CI run and do the crosscheck
- Run 42532989769: Linux 187/187, licence green, **Windows 187/188**: T-VST-35 failed — the test made `café.vstpreset` from a narrow string literal, which MSVC converts with the ANSI code page (a test defect; mini-daw itself converts with `pathFromUtf8`). No new build in `dist/`; it still holds run 42529973283 (`8008ffc`). (Copying the new build had emptied `dist/` before the Windows download turned out missing; the previous build was put back from the scratchpad. From now on the downloads are checked before `dist/` is emptied.)
- Crosscheck #16 (`Crosscheck-2026-11-09.md`; Spec v0.43, code at `f8cf624`, independent reviewer): X0 the Windows test failure; X1 a preset's state is set while the audio thread processes the instrument; X2–X11 small (same label twice, saved label, Enter in Save, program-list variants, recording rule, controller results / hex check, device names, cache, Save window, stepping start); X12 tests; X13 docs.
