- Show the current CPU metrics on the stats page.

- Simplification pass over the whole tree: line count, readability, dead code.
  Code, tests, harnesses and build scripts alike.

- Reduce the libc surface (see `[TODO-LIBC]`). We use `snprintf`, `vsnprintf`,
  `strncmp`, `strlen`, `memcpy`, `strstr` and `strtol`; prefer the `utils/text`
  helpers and bounded equivalents where practical. No known bugs, the
  hand-rolled parsing is already bounds-checked.
