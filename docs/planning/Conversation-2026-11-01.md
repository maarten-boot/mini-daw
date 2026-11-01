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
