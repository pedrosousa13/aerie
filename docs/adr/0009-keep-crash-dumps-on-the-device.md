# 9. Keep crash dumps on the device, never upload them

Status: accepted (issue #262). Built in the same issue: `overlay/components/aerie_crash_reports`, `patches/crash/0002` and `0003`, the Crash reports page of Settings, and the crash step of the Bench's `zero_traffic` check.

## Context

Helium had a crash reporting mode (Disabled, Ask, Automatic). With the mode Disabled, it deleted every pending crash report at startup and on every mode change. `patches/crash/0001` already removed the upload address and forced the mode to Disabled, so after it Aerie had no crash reports at all: the handler wrote a dump, and the next start deleted it.

That makes a crash hard to debug (issue #247) and leaves the user with nothing to attach to a bug report. The project's promise is that the browser never phones home and proves it. A local dump does not break that promise. An upload would.

## Decision

1. **Keep the dumps.** The startup code no longer deletes pending reports. It keeps the newest 20 and deletes the older ones (`CrashReportStore::Prune`, at startup and on every list in Settings). The cap holds at those two points, not between them.
2. **Never upload.** The upload address stays empty (`crash/0001`), the reporting mode stays Disabled, the database's upload switch is only ever set to off, and a request to upload one report does nothing (`crash/0002`). No preference turns any of this on. Source gates (`check_crash_reporting`) and the browser tests pin it, and the `zero_traffic` check crashes a real browser and fails on any crash request.
3. **The user can see and delete them.** Settings > Crash reports lists the time, the process type and the version of each report, with Delete, Delete all and Open folder. The page shows nothing else from a dump.

## Consequences

**Good**

- A crash leaves something to debug and to attach to an issue, and the user decides what to do with it.
- Nothing is sent. The proof is in the Bench, not only in the code.

**Bad, and how it is limited**

- A dump can hold page memory: parts of pages the user had open. It stays on the disk. So the files are mode 0600 and the folders 0700 (repaired when something loosened them, never through a symbolic link), there are at most 20, the Settings page reads only a fixed set of plain-text facts from a dump (bounded reads, a token filter), and the user can delete any of them or all of them.
- This reverses Helium's choice to discard reports when reporting is off. A user who wants none has Delete all, and the dumps are in one folder. There is no switch to stop the handler from writing them: the decision in the issue is to keep them, and deletion is the user's control.

## Alternatives considered

- **Keep deleting reports.** Safe, but it leaves no way to debug a crash and nothing to attach.
- **Upload with consent.** Breaks the promise that the browser never phones home.
- **A hardening switch to turn dumps off.** Not done: the issue's decision is to keep them. It can be added later if asked for.
