- in the stats page, we should also show the current CPU metrics

- do a wide simplification+loc-removal+readability pass on the WHOLE codebase (in code, tests, harnesses, build scripts, etc)

- **Shell kill ring** (`shell/shell.h/cpp`, `console/editor.h/cpp`, `pages/shell.cpp`):
  Implement Ctrl+K (`kill_to_end()`), Ctrl+W (`kill_word_backward()`), and Ctrl+Y
  (`paste_kill()`) in `InputEditor` and `CommandShell`. A single-slot kill buffer of
  `Config::SHELL_KILL_SIZE` bytes is sufficient for now.

- **Reduce libc dependency** (review `[TODO-LIBC]`): audit our use of libc
  (`snprintf`/`vsnprintf`, `strncmp`, `strlen`, `memcpy`, `strstr`, `strtol`) and
  shrink the surface where practical - prefer the in-house `utils/text` helpers and
  bounded equivalents, so the firmware leans on as little of the C library as
  possible. (No known bugs; the hand-rolled parsing is already bounds-checked.)

- **Docs still describe the removed PLC interface.** `docs/CONTEXTS.md` (the
  `PlcSyncContext` class + its `SystemContext` edge), `docs/LOG_FLOW.md`
  (`PlcLogReader`, "the `LogTopic` enum in `plcio/vars.h`"), `docs/CONCURRENCY.md`
  (steps 3-4 of the loop order), `docs/library.dox` (the `plcio/` subsystem row
  and the whole `BOTANIK_FEATURE_PLC` section - that flag no longer exists).
