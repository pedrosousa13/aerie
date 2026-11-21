# AGENTS.md

## Agent skills

### Issue tracker

Issues live in the repo itself — GitHub issues on pedrosousa13/aerie, via the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Canonical label names, as repo labels on pedrosousa13/aerie — plus `in-progress` and `P0`–`P3`, labels that stand in for a missing field. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.

## Checks

- Tests: `python3 -m pytest -q tests && ./aerie patches check && ./aerie test-cpp` (run builds and tests in the foreground; `aerie_adblock_unittests` has 6 known #82 failures). For sidebar or UI changes, also `./aerie test-cpp --target aerie_browser_tests` and `env -u WAYLAND_DISPLAY ./aerie test-cpp --target aerie_interactive_ui_tests`.
- Typecheck: none (the C++ compile in `./aerie test-cpp` is the type check).
