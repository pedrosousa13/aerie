# The release build

`./aerie build --release` builds the browser the way Chromium builds a shipping browser, in its own
output directory. The default `./aerie build` stays the fast developer build in `out/Default`. Benchmark
numbers (docs/bench.md) are only meaningful on the release build.

## GN arguments

The release build adds these arguments after Helium's own `flags.gn` and `flags.linux.gn` (GN uses the last
assignment). A pytest test (`tests/test_release_build.py`) reads this table and compares it with
`tools/aerie_build/release.py`, so the table and the code cannot drift.

| Argument | Value |
| --- | --- |
| `is_official_build` | `true` |
| `chrome_pgo_phase` | `2` |
| `pgo_data_path` | `"<tree>/chrome/build/pgo_profiles/<profile>"` |
| `use_thin_lto` | `true` |
| `thin_lto_enable_optimizations` | `true` |
| `v8_enable_builtins_optimization` | `true` |

`<tree>` is the source tree and `<profile>` is the file name pinned in `helium.lock` under `[pgo.linux]`.
The output directory is `out/Release` in the same source tree.

## What Helium does with PGO

Helium turns PGO off in its own `flags.gn` (`chrome_pgo_phase=0`), so `./aerie build` does not use the profile.
Helium's `build.sh --pgo` turns it on, but only on its git-clone path, where a gclient hook downloads the profile.
Aerie uses the prepared tarballs instead, and the `chromium-<version>-linux-x64.tar.zst` tarball already holds
the profile Chromium names in `chrome/build/linux.pgo.txt`, in `chrome/build/pgo_profiles/`. That tarball is
pinned by sha256 in `helium.lock`. The release build therefore needs no download of its own, not even Chromium's
`tools/update_pgo_profiles.py` (it would need `gsutil`): `pgo_data_path` points at the file, so that script
is never run.

## The pinned profile

`helium.lock` records the profile file name and its sha256 (`[pgo.linux]`). Before `gn gen`, the release build
refuses to go on when:

- the file is not in `chrome/build/pgo_profiles/`,
- its sha256 differs from the pin, or
- `chrome/build/linux.pgo.txt` names another file than the pin (Chromium would then use a profile that was never reviewed).

When Chromium moves, `./aerie bump` brings a new tarball and a new `linux.pgo.txt`; update `[pgo.linux]` with the
new name and the output of `sha256sum` on the file, in the same change.

## ThinLTO and memory

GN refuses an explicit `concurrent_links` with ThinLTO. Chromium's `get_concurrent_links.py` computes it from
RAM (30 GB per ThinLTO link on Linux, 10 GB reserved) and gives 1 link at a time on a 30 GB machine. The link
itself runs `--thinlto-jobs=all`; there is no GN argument for a lower number.

## Measured on this box

Build machine: 12 cores, 30 GB RAM. Measured on 2026-11-20 at commit 15aa971, on a first `./aerie build --release`
in a tree that already had the source and `out/Default`.

| What | Measured |
| --- | --- |
| Build time | 2h23m (compile 2h22m, 78,446 steps) |
| Memory | peak 16.8 GB used system-wide, sampled every 10 s; one ThinLTO link at a time, no out-of-memory |
| Disk | `out/Release` 21 GB (`out/Default` is 35 GB); the volume grew by 20.7 GB, ThinLTO cache included |

A build from an empty tree needs the 150 GB a developer build needs, plus this. The ThinLTO cache is capped by
Chromium at 40 GB. Only the first release build was timed; a rebuild after a small change was not measured.

### Perf: release against the developer build

One full `./aerie bench --check perf` run on each build, same commit, both with `--no-sandbox` because the AppArmor
profile did not cover `out/Release` yet. These numbers are not comparable to a sandboxed baseline.

| Metric | `out/Default` | `out/Release` | Change |
| --- | ---: | ---: | ---: |
| `speedometer_score` (higher is better) | 27.34 | 34.98 | +27.9% |
| `jetstream_score` (higher is better) | 301.73 | 321.50 | +6.6% |
| `startup_cold_ms` | 496.7 | 482.9 | -2.8% |
| `startup_warm_ms` | 142.3 | 132.5 | -6.9% |
| `memory_after_ad_pages_mb` | 1013.8 | 1131.7 | +11.6% |

A second release run, with the first as the baseline, read 34.77, 325.05 and 1113.6 MB and was within thresholds.

**Open observation: the release build reads about 118 MB (11.6%) more memory after the ad pages.** The cause is not
known and has not been investigated. The two presets have separate baselines, so the gate does not trip on it, but
that makes it easy to miss.

## The sandbox on Ubuntu

The AppArmor profile in the README must name `out/Release` as well as `out/Default`, or the release browser stops
with "No usable sandbox!". The Bench then needs `--no-sandbox` (bench.md, "Sandbox"). Do not record a baseline from
such a run.
