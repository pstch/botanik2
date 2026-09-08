"""The site generator: the tables, the ST half, and the reference page.

`scripts/tables.py` is the command line over it. Everything it emits goes
through a template in templates/, and everything it emits is COMMITTED - see
`make check-generated`, which regenerates into a scratch copy and diffs.

    model      the CSVs read, merged and checked: the model the rest share
    st         the ST half: the site's enumerations, the resource matiec
               compiles, and the roster Plant runs
    reference  docs/<site>/variables.md
    render     the Jinja environment they emit through
"""
