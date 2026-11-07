"""Unit tests for the Aerie build tooling.

Run from the repo root: python3 -m unittest discover -s tests -t . -v
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
