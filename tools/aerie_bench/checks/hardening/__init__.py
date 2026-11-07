"""Hardening checks (issue #12): one module per control, all registered on import."""

from . import cookies, extension, fingerprint, https, isolation, prefetch, settings, webrtc  # noqa: F401
