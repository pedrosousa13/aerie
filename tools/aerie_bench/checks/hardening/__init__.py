"""Hardening checks (issue #12): one module per control, all registered on import."""

from . import cookies, extension, fingerprint, https, isolation, jit, prefetch, settings, webrtc  # noqa: F401
