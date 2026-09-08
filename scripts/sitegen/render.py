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


def _width(items, attr=None):
    """The widest of @p items, for a column a template lines up itself.

    Named so a template can say `{% set w = rows | width('name') %}` beside the
    loop that uses it, rather than being handed a number from elsewhere."""
    vals = [i[attr] for i in items] if attr else list(items)
    return max((len(str(v)) for v in vals), default=0)


def _pad(value, width):
    """@p value in a column @p width wide."""
    return str(value).ljust(width)


def _arg(value, width):
    """@p value as one argument of a macro list: a comma, then the padding.

    The comma goes inside the column, so the next argument starts where the
    column ends however long this one is."""
    return (str(value) + ',').ljust(width + 1)


def _cont(loop):
    """The ` \\` that continues a macro body, on every line but the last."""
    return '' if loop.last else ' \\'


def _suffix(value, text):
    """@p text after @p value, or nothing at all where the value is empty.

    The trailing-comment case: a description that is there gets its marker, and
    one that is not leaves no trailing space behind."""
    return text % value if value else ''


_env = Environment(loader=FileSystemLoader(TEMPLATES),
                   undefined=StrictUndefined,
                   keep_trailing_newline=True,
                   trim_blocks=True,
                   lstrip_blocks=True)
_env.filters['width'] = _width
_env.filters['pad'] = _pad
_env.filters['arg'] = _arg
_env.filters['cont'] = _cont
_env.filters['suffix'] = _suffix


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
