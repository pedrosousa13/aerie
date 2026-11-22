# Comparing other browsers' network traffic

Aerie publishes, with each release, how many requests it makes to trackers or vendors (the privacy report, #253).
This page describes how the same checks run on other browsers (#254). It holds no results. A result is only valid
with the date and version of the browser that was measured, so results live in the privacy report of a release
(the "Other browsers" table), never on this page.

## What is measured

Each browser runs the Bench checks `zero_traffic` and `adblock_score` (`docs/bench.md`) on the same machine:

- a **fresh profile** and the browser's **default settings**. Nothing is turned on, off or signed in. The Helium
  services prefs that `zero_traffic` seeds for Aerie are not seeded;
- first launch plus a quiet period (60 s), 5 minutes idle, then the browse script (three local pages, a click);
- the browser's own NetLog (`--log-net-log`) is the record. It holds requests that DevTools cannot see: updaters,
  component downloads, services calls;
- the blocking score: how many of the pinned ad and tracker test hosts the browser blocks, with its defaults.

The Bench resolves no outside host name (`docs/bench.md`), so no request reaches its host: what is counted is the
browser's attempts, for Aerie and for the others alike. Traffic to this machine (loopback and the Bench's fixture hosts) is left out. Every other request, and every hop of
a redirect, is counted once.

## The four classes

A request is put in the first class that fits:

| Class | What it is | Where the list comes from |
| --- | --- | --- |
| `vendor` | A host of the browser's maker | `VENDORS` in `tools/aerie_bench/comparison.py`; for Helium and Aerie, the `helium` group of the denylist |
| `google` | A Google host | The `google` group of `tools/aerie_bench/denylist/google-helium.txt` (`docs/network-denylist.md`) |
| `tracker` | A tracker or ad host | The hosts of the pinned test list of `adblock_score` (`tools/aerie_bench/adblock_hosts.lock.json`) |
| `other` | Anything else | |

The headline number, "to trackers or vendors", is `vendor` + `google` + `tracker`. The table also shows each class.
Two limits: the browser is known by its `--version` line or file name, and a browser that is not in `VENDORS` has no
`vendor` class (its maker's hosts count as `other`; the table says so). The tracker class is the hosts of the pinned
list only, not every tracker on the web. If `adblock_score` could not get the list, the tracker column says so
instead of showing 0.

The blocking score is taken in the same offline setting. A browser that downloads its filter lists after install
(Brave, for one) is scored without them, so its score shows what it blocks before that download. Read the score with
that in mind.

## Run it

```sh
./aerie bench --measure --binary /path/to/browser --out /path/to/out    # about 7 minutes
./aerie bench --report /path/to/report --compare /path/to/out/measurement.json   # --compare is repeatable
```

`measurement.json` records the browser, its version, the date, the settings, the counts per class, the host names
per class, the blocking score and the command. It holds no local path. The run fails, and writes no measurement,
if the NetLog is not whole or the browser did not exit cleanly.

## Which browsers

The Bench runs on Linux and captures Chromium NetLogs. A browser appears in the table only if it was measured.
The others are listed as not measured, with the reason:

- **Zen**: not measured yet. Gecko-based; the Bench captures Chromium NetLogs only.
- **Dia, Arc**: Mac-only; they cannot run on the Linux machine of the Bench.

Brave, Chrome, Helium and the Linux builds of other Chromium-family browsers can run. A browser is added to the
table when someone has run it with this method, not before.

## Rules for publishing

- Publish only what was measured, with the date, the version and a link to this page.
- A run with a shortened idle or settle time is marked "NOT the full run" in the table. Do not publish it as a
  comparison.
- Do not edit a measurement by hand. Measure again.
