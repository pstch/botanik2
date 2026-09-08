#!/usr/bin/env python3
"""The Jinja environment the generators emit through.

Templates live in templates/ at the project root, one per generated file, named
for what they produce. The settings below are what keep the output BYTE-EXACT:

  trim_blocks / lstrip_blocks   a control tag on its own line contributes no
                                newline and no indent, so a template can be
                                indented for reading without that reaching the
                                output
  keep_trailing_newline         a template's final newline is part of the file
  StrictUndefined               a name the context does not carry is an error,
                                not a silent blank - a generated file is a
                                contract and a hole in one is a defect

`make check-generated` is what holds all of that: it regenerates and diffs.
"""

import os

from jinja2 import Environment, FileSystemLoader, StrictUndefined

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEMPLATES = os.path.join(ROOT, 'templates')

_env = Environment(loader=FileSystemLoader(TEMPLATES),
                   undefined=StrictUndefined,
                   keep_trailing_newline=True,
                   trim_blocks=True,
                   lstrip_blocks=True)


def render(name, **ctx):
    """The template @p name, rendered against @p ctx."""
    return _env.get_template(name).render(**ctx)


def write(path, name, **ctx):
    """Render, and write it where it belongs, creating the directory."""
    out = render(name, **ctx)
    d = os.path.dirname(path)
    if d:
        os.makedirs(d, exist_ok=True)
    open(path, 'w').write(out)
    return out
