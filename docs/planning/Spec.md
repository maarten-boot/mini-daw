# Spec — mini-daw

Status: draft. The scenario has not been described yet.

## 1. Scope
_To be filled in from the user's scenario._

## 2. Functional requirements
_None yet._

## 3. UI accessibility requirements (apply to any GUI or TUI)
- **SPEC-UI-01** Default theme is a white background with black text.
- **SPEC-UI-02** Every foreground/background colour pair meets a WCAG 2.x contrast ratio of at least 4.5:1 (AA).
- **SPEC-UI-03** The user can raise the minimum contrast interactively while the application runs.
- **SPEC-UI-04** The user can set the minimum contrast in a config file.
- **SPEC-UI-05** Contrast presets: AA (4.5:1), AAA (7:1), Max (21:1, pure black/white), plus a custom minimum ratio. A custom value below 4.5:1 is rejected.
- **SPEC-UI-06** When a minimum is active, any colour pair below it is adjusted until it meets that minimum.

## 4. Open questions
- What is the scenario (audio features, target platform, GUI, TUI or both, language/stack)?
