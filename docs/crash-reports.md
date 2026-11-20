# Crash reports

Issue #262. A crash report is kept on the device, never uploaded, and the user can see and delete it.

## What the browser does

- The crash handler (`aerie_crashpad_handler`) starts without an upload address (`crash/0001`). The
  database's upload switch is only ever set to off, and a request to upload one report is a no-op
  (`crash/0002`). The reporting mode is always Disabled; no pref turns it on.
- A crash leaves `<config>/Crash Reports/pending/<id>.dmp`. Files are mode 0600 and folders 0700; the
  list step tightens a file that is looser.
- Helium deleted pending reports at startup. Aerie keeps them and prunes to the newest 20 at startup and
  whenever Settings lists them (`CrashReportStore::Prune`).

## Settings > Crash reports

`chrome://settings/crashReports`. Each row shows the time, the process type (browser, renderer, GPU,
utility) and the version. Buttons: Delete (per row), Delete all, Open folder. Nothing is sent.

The handler (`AerieCrashReportsHandler`) has four messages: `aerieCrashReports.list`, `.delete(id)`,
`.deleteAll`, `.openFolder`. None takes a path; an id must be a UUID of a report in the database. The
answer holds the id, the time, the process type, the version and the limit, never a path or any content
of a dump. Process type and version are read from the dump with bounds checks on every offset
(`dump_facts.cc`); only plain tokens are accepted, so page text cannot reach the page.

## Tests

- `components/aerie_crash_reports` unit tests: dump facts, listing, delete, prune, modes.
- `aerie_browser_tests`: handler messages; upload URL empty; no pref or caller turns uploads on.
- Bench `zero_traffic` crash step: see [bench.md](bench.md).
- Source gates (`check_crash_reporting`) pin the patched code.
