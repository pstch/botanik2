import sys
from pathlib import Path

# The harness is the kit's; ctest puts it on PYTHONPATH, and a bare pytest in this
# tree finds it beside.
sys.path.append(str(Path(__file__).resolve().parents[1] / "kit" / "pyte"))

from harness import assert_screen, pytest_addoption, spawn, spawn_module  # noqa: E402,F401
