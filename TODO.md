- in the stats page, we should also show the current CPU metrics

- do a wide simplification+loc-removal+readability pass on the WHOLE codebase (in code, tests, harnesses, build scripts, etc)

- **Reduce libc dependency** (review `[TODO-LIBC]`): audit our use of libc
  (`snprintf`/`vsnprintf`, `strncmp`, `strlen`, `memcpy`, `strstr`, `strtol`) and
  shrink the surface where practical - prefer the in-house `utils/text` helpers and
  bounded equivalents, so the firmware leans on as little of the C library as
  possible. (No known bugs; the hand-rolled parsing is already bounds-checked.)
