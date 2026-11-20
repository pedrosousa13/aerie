"""The release preset of `./aerie build --release` (docs/release-build.md).

An official build with PGO, ThinLTO and V8 builtins PGO, in out/Release. The PGO profile is the one in
Chromium's own prepared tarball, pinned by name and sha256 in helium.lock ([pgo.linux]).
"""

from __future__ import annotations

from pathlib import Path

from . import lock

PROFILE_DIR = Path("chrome") / "build" / "pgo_profiles"
PROFILE_LIST = Path("chrome") / "build" / "linux.pgo.txt"


class ProfileError(RuntimeError):
    """The PGO profile on disk is not the one helium.lock pins."""


def verify_profile(tree: Path, pin: dict) -> Path:
    """The pinned profile's path, after checking it is on disk, matches the pin and is the one Chromium names."""
    name, digest = pin.get("name"), pin.get("sha256")
    if not name or not digest:
        raise ProfileError("helium.lock has no [pgo.linux] name and sha256; the release build needs a pinned profile")
    listed = (tree / PROFILE_LIST).read_text(encoding="utf-8").strip() if (tree / PROFILE_LIST).is_file() else None
    if listed != name:
        raise ProfileError(f"{PROFILE_LIST} names {listed!r} but helium.lock pins {name!r}; review the new profile and re-pin it")
    path = tree / PROFILE_DIR / name
    if not path.is_file():
        raise ProfileError(f"the pinned PGO profile is missing: {path}")
    actual = lock.sha256_file(path)
    if actual != digest.lower():
        raise ProfileError(f"sha256 mismatch for the PGO profile {name}: helium.lock pins {digest.lower()}, the file has {actual}")
    return path


def gn_args(tree: Path, pin: dict) -> str:
    """The GN arguments the release build appends to Helium's, so that they win (docs/release-build.md lists them)."""
    profile = tree / PROFILE_DIR / pin["name"]
    bad = [c for c in '"\\$' if c in str(profile)]
    if bad:
        raise ProfileError(f"the path {profile} has {' '.join(bad)} in it, which would break the quoted GN string "
                           "of pgo_data_path; build in a path without double quotes, backslashes and dollar signs")
    return "\n".join([
        "is_official_build = true",
        "chrome_pgo_phase = 2",
        f'pgo_data_path = "{profile}"',
        "use_thin_lto = true",
        "thin_lto_enable_optimizations = true",
        "v8_enable_builtins_optimization = true",
    ]) + "\n"
