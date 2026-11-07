"""Helium's tree-processing steps that run after its patches.

The build and `./aerie patches check` both run these, so the check sees the
tree the Aerie layers see: domain substitution, name substitution, the i18n
step and the version step, in Helium's order (helium-linux/scripts/build.sh).
The callers differ in how they run a command and whether a step is stamped:
the build stamps each step and never repeats it, the check runs them all on a
throwaway sparse tree.
"""

from __future__ import annotations

from pathlib import Path
from typing import Callable, Sequence

Run = Callable[[Sequence[str | Path]], None]
Stamped = Callable[[str, Callable[[], None]], None]


def filtered_domain_list(core: Path, tree: Path, dest: Path) -> Path:
    """helium-chromium/domain_substitution.list without the files that are not in tree."""
    lines = (core / "domain_substitution.list").read_text(encoding="utf-8").splitlines()
    dest.write_text("".join(f"{line}\n" for line in lines if line.strip() and (tree / line.strip()).is_file()))
    return dest


def post_patch_steps(
    python: Path | str,
    core: Path,
    platform: Path,
    tree: Path,
    run: Run,
    stamped: Stamped,
    domain_list: Path | None = None,
    namesub_backup: Path | None = None,
    i18n: bool = True,
) -> None:
    """Domain substitution, name substitution, i18n and version, each through `stamped`.

    domain_list replaces helium-chromium/domain_substitution.list (a sparse tree
    holds only some of the files). i18n needs every translated string file of
    the real tree, so a sparse tree skips it; i18n only edits .xtb files, which
    Aerie patches may not touch.
    """
    utils = core / "utils"
    stamped("domsub", lambda: run([
        python, utils / "domain_substitution.py", "apply", "-r", core / "domain_regex.list",
        "-f", domain_list or core / "domain_substitution.list", tree]))
    backup = ["--backup-path", namesub_backup] if namesub_backup else []
    stamped("namesub", lambda: run([python, utils / "name_substitution.py", "--sub", "-t", tree, *backup]))
    if i18n:
        stamped("i18n", lambda: run([python, utils / "i18n_apply.py", "-t", tree]))
    stamped("version", lambda: run([
        python, utils / "helium_version.py", "--tree", core, "--platform-tree", platform,
        "--chromium-tree", tree]))
