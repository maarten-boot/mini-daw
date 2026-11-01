# Conversation log — 2026-11-01

## 1. Working rules set by user
- Keep a running conversation log (`Conversation-YYYY-MM-DD.md`, one file per day).
- Maintain `Plan.md` and `Spec.md`; prepare test cases as we go (`Tests.md`, traced to Spec items).
- After significant changes, suggest a crosscheck between Spec.md, Plan.md and code.
- Review user sentences for errors/uncertainties; suggest corrections and confirm before proceeding.
- GUI/TUI: default white background, black text, WCAG contrast ratio ≥ 4.5:1 on all colour pairs; user can raise contrast interactively or via config file.

## 2. Corrections proposed by Claude, confirmed by user ("yes to all")
- Log filename uses 4-digit year: `Conversation-YYYY-MM-DD.md`.
- "Contrast index >4.5" → WCAG contrast ratio ≥ 4.5:1; presets AA 4.5:1, AAA 7:1, Max 21:1, plus custom minimum.
- Test cases kept in `Tests.md`, each traced to a Spec item.
- `git init` with `main` branch; planning docs on branch `docs/planning`.
- Planning docs live in `docs/planning/`.

## 3. Actions
- Initialised git repository (`main`), created branch `docs/planning`.
- Created `Conversation-2026-11-01.md`, `Plan.md`, `Spec.md`, `Tests.md` in `docs/planning/`.
- Waiting for the user's scenario description.

## 4. Scenario described by user
> ok the idea is to make a small program suitable to record both mini input and autio input and later add vst3 instruments so we can also record their audio output. Initially on windows 10 and possibly later also supporting Linux and Mac. Note editing and sound editing is not required implementation is currently not yet decided but most likely C/C++  or Rust as i expect that will be the most straightformard.

- Claude proposed corrections (mini→MIDI, autio→audio, straightformard→straightforward, "is"→"are") and asked about open points (scope of "not required", recording model, audio driver, UI type, file formats, language). Waiting for user confirmation before updating Spec/Plan/Tests.

## 5. User re-sent scenario
- Same text with "mini" corrected to "midi"; "autio", "straightformard" and "is"→"are" still present; open points not answered.
- Claude flagged remaining typos and asked the open points as a multiple-choice question.

## 6. Decisions (user answers)
- Corrected meaning accepted: record **MIDI** and **audio** input; later add VST3 instruments and record their output; Windows 10 first, Linux/macOS later.
- Note and sound editing: **not in v1** (keep the design open for later).
- Features: live monitoring, playback of takes, multiple tracks at once, render MIDI through VST3.
- UI: **GUI**.
- Stack: **C++ with RtAudio/RtMidi**, VST3 SDK used directly, permissive licences only.

## 7. Actions
- Spec.md rewritten as draft v0.2 (TECH, DEV, REC, MON, PLAY, VST, PRJ, UI, NF items). Claude's own defaults are marked *(proposed)*.
- Plan.md: steps P2–P13, milestones v1 (record/playback) and v2 (VST3).
- Tests.md: test cases for all Spec items, marked automated or manual.
- Committed on branch `docs/planning`.

## 8. Licence
> Licencing would be my normal MIT unless we run into obstables, in esscence free to use and modify for all

- Claude proposed corrections (obstables→obstacles, esscence→essence), checked the licences of the chosen dependencies against MIT, and flagged ASIO as the likely obstacle. Waiting for confirmation before updating Spec.

## 9. Licence decisions
- User confirmed: MIT, copyright "Maarten Boot". Pointed to https://www.steinberg.net/developers/asiosdk-open/.
- Claude checked: the ASIO SDK "open" variant is **GPLv3** (the proprietary licence still exists alongside it). The VST3 SDK LICENSE file on GitHub is **MIT**.
- Decision: the code stays MIT. ASIO is an optional build setting, off by default, and its SDK is never in the repository. ASIO builds under GPLv3 are themselves GPLv3, and the README says so.
- Spec v0.3: SPEC-LIC-01..04 added. Tests T-LIC-01..04 added. Plan: P1b done, P11b (ASIO) added.
- `LICENSE` (MIT) committed as the first commit on `main`; `docs/planning` rebased onto it.
