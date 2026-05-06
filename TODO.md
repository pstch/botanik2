# TODO

## Terminal editor — deferred screen-update work

- `editor.cpp` `replace_characters()`: after updating `_buf`, reflect the change on screen (currently only updates the buffer)
- `editor.cpp` `append_characters()`: same — screen update missing after buffer write
- `editor.cpp` `select_characters()`: implementation pending; should set `_sel_anchor`/`_sel_len` and highlight the selection on screen
- `editor.h` `set_contents()`: implementation pending; should replace the full buffer contents and redraw
- `editor.cpp` `redraw()`: implement full prompt redraw (rewrite all rows from origin, handle cursor repositioning)

## Logging

- `logger.cpp` `fill_buckets()`: when a bucket's `suppressed` counter is non-zero on refill, emit a synthetic log entry reporting the drop count, then reset the counter

## Clang-tidy — expand checks

- `.clang-tidy` currently enables only `bugprone-*`; add `modernize-*` and `readability-*` once the
  existing violations are resolved and the CI baseline is stable

## BotanikApp

- `libbotanik.h` / `libbotanik.cpp`: add class-level doc blocks for `BotanikLoop` and `BotanikApp`
- `libbotanik.cpp` `BotanikApp`: add a proper constructor with member init list; `setup()` currently
  assigns members in the body via copy-construction, which requires default constructors that
  some members (e.g. `BufferedSerial`) do not have — so `BotanikApp` cannot be instantiated yet
