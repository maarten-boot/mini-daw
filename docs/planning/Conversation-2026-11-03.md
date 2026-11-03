# Conversation log — 2026-11-03

Continues `Conversation-2026-11-02.md` (P7, P7b, P8, P9 and crosschecks #5 and #6 were done there).

## 1. CI after P9
> pushed

- Run 41540018781 (`6536815`, P9): every step of the linux, windows and licence jobs passed via the API; nlohmann/json fetched and licence-checked. The job logs could not be downloaded here (`http2: frame too large`), so the test count and the MSVC warning count are not confirmed this time.

## 2. Crosscheck #7
> do the crosscheck #7

- Written up in `Crosscheck-2026-11-02.md` (Claude + independent reviewer; N1 and N2 reproduced with a small program).
- Two high findings: N1 a wrong-typed `format`/`version` in project.json throws (a crash at every start, since the last project is reopened); N2 a non-UTF-8 name makes autosave throw. Medium: N3 name collisions when copying takes in, N4 a failed copy still switches the project, N5 open failures not shown, N6 repairs not saved (skipped tracks lost on the next save). Low: N7–N9; docs N10; tests N11; notes N12.
- Waiting for the user's go.

## 3. Crosscheck #7 fixes
> yes fix N1–N11, N1 and N2 first

- N1: type checks + catch-all around project parsing; N2: UTF-8-safe saving. Then N3–N11 (see `Crosscheck-2026-11-02.md`).
- `importTakes` now returns copied / missing / failed; collisions get `-2`, `-3`; Save as only switches when all copies worked.
- Repaired projects keep `project.json.bak`; open failures shown in the status line; Recent marks missing projects.
- Spec v0.21; tests T-PRJ-07, T-PRJ-08, T-PRJ-03/04 extended. Linux 119/119, Wine 118/118; N1/N5/N6 checked live.
- A safety check refused my `rm -f $O/*` (would be `/*` if `$O` were empty); rerun with `"${O:?}"/*`.
