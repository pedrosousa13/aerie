# AGENTS.md

## Agent skills

### Issue tracker

Issues live in the repo itself — GitHub issues on pedrosousa13/aerie, via the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

Canonical label names, as repo labels on pedrosousa13/aerie — plus `in-progress` and `P0`–`P3`, labels that stand in for a missing field. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.

## Checks

- Tests: `python3 -m pytest -q tests && ./aerie patches check && ./aerie locked -- ./aerie test-cpp` (C++ issues also run the browser and interactive UI targets they touch, with `--target aerie_browser_tests` / `--target aerie_interactive_ui_tests`, and `./aerie bench`). Run test runs in the foreground. The `aerie_adblock_unittests` failures tracked in #82 fail on main too; any other failure blocks landing.
- Typecheck: none separate — the compile inside `./aerie test-cpp` is the type check for C++.
