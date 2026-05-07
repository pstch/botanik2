# Keyboard Bindings

This document lists all key bindings and the layer that handles each one.
See [EVENT_DISPATCH.md](EVENT_DISPATCH.md) for the full dispatch architecture.

## Binding table

| Key(s)               | Layer          | Action                                              |
|----------------------|----------------|-----------------------------------------------------|
| Printable text       | AnsiTerminal   | `page→handle_text(c)` for each char                |
| **Navigation**       |                |                                                     |
| Ctrl+B / LEFT        | ShellPage      | `editor.move_left()`                                |
| Ctrl+F / RIGHT       | ShellPage      | `editor.move_right()`                               |
| Ctrl+A               | ShellPage      | `editor.move_to_line_start()`                       |
| Ctrl+E / END         | ShellPage      | `editor.move_to_line_end()`                         |
| Alt+B                | ShellPage      | `editor.move_word_left()`                           |
| Alt+F                | ShellPage      | `editor.move_word_right()`                          |
| Ctrl+P / UP          | ShellPage      | history previous — stub, not yet implemented        |
| Ctrl+N / DOWN        | ShellPage      | history next — stub, not yet implemented            |
| **Editing**          |                |                                                     |
| ENTER                | ShellPage      | `shell.submit()`                                    |
| BACKSPACE            | ShellPage      | `editor.backspace()` (selection-aware)              |
| Ctrl+D / DELETE      | ShellPage      | `editor.delete_forward()` (selection-aware)         |
| Ctrl+K               | ShellPage      | kill to end of line — stub, not yet implemented     |
| Ctrl+W               | ShellPage      | kill previous word — stub, not yet implemented      |
| Ctrl+Y               | ShellPage      | yank from kill ring — stub, not yet implemented     |
| Ctrl+U               | ShellPage      | `editor.reset()` (clear line)                       |
| Ctrl+C               | ShellPage      | `editor.reset()` (cancel command)                   |
| **Selection / Mark** |                |                                                     |
| Ctrl+Space           | ShellPage      | toggle mark: set if inactive, deselect if active    |
| Ctrl+G / ESC         | ShellPage      | `editor.deselect()`                                 |
| **Page control**     |                |                                                     |
| Alt+1 … Alt+9        | AnsiTerminal   | `switch_to(0)` … `switch_to(8)`                     |
| Alt+0                | AnsiTerminal   | `switch_to(9)` (if registered)                      |
| HOME                 | AnsiTerminal   | `switch_to(0)` (go to home page)                    |
| Ctrl+L               | AnsiTerminal   | `page→focus()` (full screen redraw)                 |
| PAGE_UP              | LogsPage       | scroll back — stub, not yet implemented             |
| PAGE_DOWN            | LogsPage       | scroll forward — stub, not yet implemented          |
| ENTER (non-shell pg) | other pages    | `page→focus()` (refresh display)                   |
| **Stubs**            |                |                                                     |
| Ctrl+R               | ShellPage      | history search — no-op                              |
| TAB                  | ShellPage      | auto-completion — no-op                             |

## Key aliases

| Key      | Treated as |
|----------|------------|
| `END`    | Ctrl+E     |
| `ESC`    | Ctrl+G     |
| `DELETE` | Ctrl+D     |
| `ALT_0`  | switch to page 10 (no-op if not registered) |

## Notes

- **Ctrl+Space** toggles the mark: first press sets the mark at the current
  cursor position; subsequent cursor movement extends the selection from that
  anchor. A second Ctrl+Space deselects.
- **Ctrl+G / ESC** cancels any active mark or selection without altering the
  buffer contents.
- **ALT_1…ALT_9 / HOME** are intercepted by `AnsiTerminal` and never reach the
  active page's `handle_command()`.
