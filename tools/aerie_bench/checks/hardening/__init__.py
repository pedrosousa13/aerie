"""Hardening checks (issue #12): one module per control, all registered on import."""

from . import cookies, device_memory, extension, fingerprint, fingerprint_site, https, isolation, jit, prefetch, settings, webrtc  # noqa: F401
