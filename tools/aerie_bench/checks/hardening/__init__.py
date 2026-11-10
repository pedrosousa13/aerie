"""Hardening checks (issue #12): one module per control, all registered on import."""

from . import (cookies, device_memory, extension, fingerprint, fingerprint_fonts, fingerprint_popup, fingerprint_readpixels, fingerprint_site, fingerprint_worker, gpu, https, isolation, jit,  # noqa: F401
               navigator, prefetch, screen, settings, textmetrics, webrtc)
