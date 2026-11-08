"""Pluggable checks.

A check is a class with a ``name`` and either or both of:

* ``run(session) -> CheckResult``: while the browser is live (drive pages here).
* ``finalize(session, result) -> CheckResult``: after the browser has exited and
  the NetLog is parsed (``session.netlog``, ``session.exit`` are filled in).

Set ``seeds_helium_services = True`` to have the Bench seed the throwaway
profile's Helium services prefs with a localhost fixture override before launch.
A check can also define ``profile_prefs(server) -> dict``: preferences the Bench
merges into the profile's ``Default/Preferences`` before launch, and
``browser_flags(server) -> tuple[str, ...]``: command-line flags for the main browser, asked
for once the fixture server is up (a check that needs its TLS side starts it there).
A result status is ``pass``, ``fail``, ``error``, ``skip``, or, for a check written before the
feature it tests, ``xfail`` (expected to fail; the run stays green) and ``xpass`` (it passed
while still marked expected to fail; the run fails until the marker is removed).
Register a check with ``@register``. Later issues add checks here without
touching the runner.
"""

from __future__ import annotations

REGISTRY: dict[str, type] = {}


def register(cls: type) -> type:
    if cls.name in REGISTRY:
        raise ValueError(f"duplicate check name {cls.name!r}")
    REGISTRY[cls.name] = cls
    return cls


def get(name: str) -> type:
    if name not in REGISTRY:
        raise KeyError(f"unknown check {name!r}; available: {', '.join(sorted(REGISTRY))}")
    return REGISTRY[name]


from . import ad_blocking, branding, browser_requests, cosmetic_filtering, filter_lists, hardening, mv2_extension, phishing_protection, scenario_steps, smoke, widevine_opt_in, workspace_isolation, zero_traffic  # noqa: E402,F401  (register built-ins)
