import sys
from pathlib import Path

# Works as `python3 -m tools.aerie_ci` from the repo root: aerie_build and aerie_bench live in tools/.
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from .cli import main  # noqa: E402

sys.exit(main())
