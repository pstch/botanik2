"""Where the botanik checkout is, and every path inside it the generator reads.

The generator runs from a SITE's repository as readily as from botanik's own,
so nothing here may be relative to the working directory: `src/libbotanik/...`
resolves to the site repo when that is where the command was run.

ROOT is derived from this file's own location, which is the one thing that is
true wherever the command is invoked from. A path that belongs to the SITE
stays relative to the site directory, and a path that belongs to the caller
(the variables page) stays relative to the working directory, so a site repo
writes its own docs rather than the submodule's.
"""

import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def lib(*parts):
    """A path inside the botanik checkout."""
    return os.path.join(ROOT, *parts)
