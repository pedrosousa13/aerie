# Comparing other browsers' network traffic

Aerie publishes, with each release, how many requests it makes to trackers or makers (the privacy report, #253).
This page describes how the same checks run on other browsers (#254). It holds no results. A result is only valid
with the date and version of the browser that was measured, so results live in the privacy report of a release
(the "Other browsers" table), never on this page.

## The same configuration for every row

Every row, Aerie's too, comes from `./aerie bench --measure --binary <browser>`. The report run of Aerie is not
used for the table, because it seeds prefs and runs the crash step. A measurement is:

- a **fresh profile** and the browser's **default settings**: nothing is turned on, off or signed in, and the Helium
  services prefs that the report run seeds are not seeded;
- run with the **Bench launch flags**, taken from the real command line and recorded in `measurement.json` and in the
  table: `--headless=new`, `--host-resolver-rules` (no outside name resolves), `--no-proxy-server`, `--no-first-run`,
  `--no-default-browser-check`, `--password-store=basic`, `--remote-debugging-pipe` and the NetLog flag. The profile and
  NetLog paths are replaced by `<profile>` and `<netlog>`;
- **headless**. A headless browser may start fewer services than a window does (no first-run page, no window-bound
  features). The numbers are for this setup. A headed run (`--headed`) is recorded as headed;
- **offline by design**: the Bench resolves no outside name, so the browser cannot reach any host;
- first launch plus a quiet period (60 s), 5 minutes idle, then the browse script (three local pages, a click);
- recorded by the browser's own NetLog (`--log-net-log`), which holds requests that DevTools cannot see: updaters,
  component downloads, services calls.

## What is counted

Every request that is not to this machine (loopback and the Bench's fixture hosts) is one **attempt**, for every
browser. Each hop of a redirect is one attempt. A retry is another attempt, so the table shows the distinct hosts
beside the attempts ("12 (1 host)"). A request that the browser's own policy refused before it left (net error -22,
the request policy of Aerie) is **not** counted, for any browser. Because nothing resolves, an attempt fails; it is
still counted.

## The four classes

An attempt is put in the first class that fits. The class id `vendor` is the maker of the browser; user-facing text says
"maker".

| Class | What it is | Where the list comes from |
| --- | --- | --- |
| `vendor` | A host of the browser's maker | `VENDORS` in `tools/aerie_bench/comparison.py` (Brave: `brave.com`, which is every Brave host in `tests/fixtures/netlog-brave-real.json`); for Helium and Aerie, the `helium` group of the denylist |
| `google` | A Google host | The `google` group of `tools/aerie_bench/denylist/google-helium.txt` (`docs/network-denylist.md`) |
| `tracker` | A tracker or ad host | The hosts of the pinned test list of `adblock_score` (`tools/aerie_bench/adblock_hosts.lock.json`) |
| `other` | Anything else | |

The headline, "to trackers or vendors", is `vendor` + `google` + `tracker`. Limits:

- the browser is known by its `--version` line or file name. A browser that is not in `VENDORS` has no known maker
  hosts: the table shows "unknown" for the maker column, and the headline says "to Google or trackers; maker hosts not
  known", never a count to makers;
- the tracker class is the hosts of the pinned test list only, so it is a lower bound, not every tracker on the web.
  If `adblock_score` could not get the list, the table says "no tracker list" instead of 0.

## Blocking

`adblock_score` runs in every measurement, and its raw result is kept in `measurement.json` as `adblock_offline`. It
is **not** published as a score for a browser other than Aerie. The Bench is offline, and a browser that downloads its
filter lists at first run (Brave, for one) cannot load them, so its result says nothing about how it blocks. Each such
row reads "not scored: filter lists download at first run; the Bench runs offline".

## Run it

```sh
./aerie bench --measure --binary /path/to/browser --out /path/to/out    # about 7 minutes; do the same for Aerie
./aerie bench --report /path/to/report --compare /path/to/out/measurement.json --compare /path/to/aerie-out/measurement.json
```

The run fails, and writes no measurement, if the NetLog is not whole or the browser did not exit cleanly. If the
report gets no Aerie measurement, the table says Aerie was not measured with the comparison method and shows no Aerie
number.

## Which browsers

The Bench runs on Linux and captures Chromium NetLogs. A browser appears in the table only if it was measured. The
others are listed as not measured, with the reason:

- **Zen**: not measured yet. Gecko-based; the Bench captures Chromium NetLogs only.
- **Dia, Arc**: Mac-only; they cannot run on the Linux machine of the Bench.

Brave, Chrome and Chromium can run. A browser is added to the table when someone has run it with this method.

## Rules for publishing

- Publish only what was measured, with the date, the version and a link to this page.
- A run with a shortened idle or settle time is marked "NOT the full run" in the table. Do not publish it as a
  comparison.
- Do not edit a measurement by hand. Measure again.
