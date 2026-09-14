import sys
from pathlib import Path

# The harness is the library's: ctest puts it on PYTHONPATH, a bare pytest
# finds it through the submodule.
sys.path.append(str(Path(__file__).resolve().parents[2] / "extern" / "botanik" / "tests" / "pyte"))

from harness import assert_screen, pytest_addoption, spawn, spawn_module  # noqa: E402,F401
