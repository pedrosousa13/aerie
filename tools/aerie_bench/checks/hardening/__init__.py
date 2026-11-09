"""Hardening checks (issue #12): one module per control, all registered on import."""

from . import (cookies, device_memory, extension, fingerprint, fingerprint_popup, fingerprint_site, fingerprint_worker, gpu, https, isolation, jit,  # noqa: F401
               prefetch, screen, settings, webrtc)
