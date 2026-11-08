"""perf: memory after ad-heavy pages, cold and warm startup, Speedometer and JetStream (issue #16).

Writes ``perf.json`` (see perf.py) next to the results. Every metric must be measured: a metric that
cannot be (a benchmark copy missing, a benchmark that errors or times out) fails the check, unless
the run was told to skip the benchmarks (``benchmarks=False``, ``--perf-skip-benchmarks``), which is
recorded in ``skipped``. With a baseline document it fails when a metric regressed past its
threshold; a baseline file that does not exist fails too, unless ``first_run`` is set
(``--perf-first-run``). Not in the default scenario.
"""

from __future__ import annotations

import json
import statistics
import time
from datetime import datetime, timezone
from pathlib import Path

from .. import benchmarks as benchmarks_module
from .. import perf, procmem
from ..session import CheckResult, Session
from . import register

AD_PAGES = ("/perf/ad-heavy-1.html", "/perf/ad-heavy-2.html", "/perf/ad-heavy-3.html")
STARTUP_PAGE = "/index.html"
BENCHMARKS = {"speedometer": "speedometer_score", "jetstream": "jetstream_score"}
FETCH_HINT = "run ./aerie bench --download-benchmarks"


def median_ms(values: list[float]) -> float:
    return round(statistics.median(values), 1)


@register
class Perf:
    name = "perf"
    seeds_helium_services = False

    def __init__(self, baseline: str | None = None, settle_seconds: float = 3.0, startup_runs: int = 3,
                 benchmarks: bool = True, label: str | None = None, first_run: bool = False,
                 drivers: dict | None = None) -> None:
        self.baseline = baseline
        self.settle = settle_seconds
        self.startup_runs = max(1, startup_runs)
        self.benchmarks = benchmarks
        self.label = label
        self.first_run = first_run
        self.drivers = drivers if drivers is not None else benchmarks_module.DRIVERS

    # -- measurements ------------------------------------------------------------------------------

    def memory_mb(self, session: Session) -> float:
        for page in AD_PAGES:
            session.navigate(page)
        time.sleep(self.settle)
        tree = procmem.tree_rss_kb(session.browser_pid) if session.browser_pid else None
        if tree is None:
            raise RuntimeError("cannot read the process tree's memory (needs /proc and a browser pid)")
        return round(tree["total_kb"] / 1024.0, 1)

    def startup_ms(self, session: Session) -> tuple[float, float]:
        """(cold, warm): the median over the runs. Cold is a launch on an empty profile; warm is the
        next launch on that profile. The operating system's file cache is not dropped, so cold
        means an empty profile, not an empty disk cache."""
        cold, warm = [], []
        for _ in range(self.startup_runs):
            with session.new_profile() as profile:
                for bucket in (cold, warm):
                    start = time.monotonic()
                    with session.variant(profile=profile) as browser:
                        browser.navigate(STARTUP_PAGE)
                        bucket.append((time.monotonic() - start) * 1000.0)
        return median_ms(cold), median_ms(warm)

    def run_benchmark(self, session: Session, name: str) -> tuple[float | None, str]:
        """(score, "") or (None, why it could not be measured)."""
        driver = self.drivers[name]
        vendor = session.fixtures.vendor_root
        page = driver["entry"].split("?", 1)[0]
        if vendor is None or not (vendor / name / page).is_file():
            return None, f"no local copy of {name} (expected {vendor / name / page if vendor else 'a vendor root'}); {FETCH_HINT}"
        res = session.navigate_url(f"{session.fixtures.base_url}/vendor/{name}/{driver['entry']}")
        if res["error"]:
            return None, f"{name} did not load: {res['error']}"
        timeout = float(driver.get("timeout_s", 900))
        if not session.wait_for(driver["done"], timeout=timeout, interval=1.0):
            return None, f"{name} did not finish within {timeout:g} s"
        score = session.evaluate(driver["score"])
        if not isinstance(score, (int, float)) or isinstance(score, bool) or score <= 0:
            return None, f"{name} gave no usable score ({score!r}); the run errored"
        return round(float(score), 2), ""

    # -- the check ---------------------------------------------------------------------------------

    def run(self, session: Session) -> CheckResult:
        metrics: dict[str, float] = {}
        skipped: dict[str, str] = {}
        failed: dict[str, str] = {}
        metrics["memory_after_ad_pages_mb"] = self.memory_mb(session)
        metrics["startup_cold_ms"], metrics["startup_warm_ms"] = self.startup_ms(session)
        for bench, metric in BENCHMARKS.items():
            if not self.benchmarks:
                skipped[metric] = "benchmarks switched off (--perf-skip-benchmarks)"
                continue
            value, why = self.run_benchmark(session, bench)
            if value is None:
                failed[metric] = why
            else:
                metrics[metric] = value
        doc = {"schema": perf.SCHEMA, "label": self.label, "binary": {"path": session.binary,
               "version": session.binary_version}, "started": datetime.now(timezone.utc).isoformat(timespec="seconds"),
               "metrics": metrics, "skipped": skipped, "failed": failed}
        (session.out_dir / "perf.json").write_text(json.dumps(doc, indent=1) + "\n")
        session.artifacts.append("perf.json")
        data = {"metrics": metrics, "skipped": skipped, "failed": failed}
        summary = ", ".join(f"{k}={v:g}" for k, v in metrics.items())
        if skipped:
            summary += f"; skipped on request: {', '.join(skipped)}"
        if failed:
            return CheckResult(self.name, "fail", summary + "; could not be measured: " + "; ".join(
                f"{k}: {v}" for k, v in failed.items()), data=data)
        if not self.baseline:
            return CheckResult(self.name, "pass", summary + "; no baseline given, nothing compared", data=data)
        try:
            base = perf.load(Path(self.baseline))
        except FileNotFoundError:
            if self.first_run:
                return CheckResult(self.name, "pass", summary + f"; first run, no baseline at {self.baseline} "
                                   "(--perf-first-run)", data=data)
            return CheckResult(self.name, "fail", summary + f"; no baseline at {self.baseline}, so nothing was "
                               "compared. Record one from a main build with --perf-record DIR, or pass "
                               "--perf-first-run for the very first run", data=data)
        except (OSError, ValueError) as exc:
            return CheckResult(self.name, "error", f"cannot read the baseline: {exc}", data=data)
        verdicts = perf.compare(doc, base)
        data["verdicts"] = verdicts
        bad = perf.failures(verdicts)
        if bad:
            return CheckResult(self.name, "fail", "regressed past threshold: " + "; ".join(
                perf.verdict_lines(bad)), data=data)
        return CheckResult(self.name, "pass", summary + "; within thresholds of the baseline", data=data)
