# Keyboard Bindings

This page lists every key binding and the function it dispatches to. A binding
resolves in two stages: @ref Botanik::Terminal::InputParser decodes the raw byte
stream into an `InputEvent::Command`, then
@ref Botanik::Terminal::AnsiTerminal::step "AnsiTerminal::step()" routes it - a
handful of commands are intercepted by the terminal, the rest handed to the active
page's `handle_command()`. See [EVENT_DISPATCH.md](EVENT_DISPATCH.md) for the full
dispatch architecture.

## Terminal intercepts

Consumed by @ref Botanik::Terminal::AnsiTerminal::step "AnsiTerminal::step()"
before they can reach the active page.

| Key(s)              | Action                                                                                                  |
|---------------------|---------------------------------------------------------------------------------------------------------|
| Ctrl+L              | @ref Botanik::Pages::Page::redraw "page->redraw()" - clear, redraw the status row, repaint the page      |
| HOME                | @ref Botanik::Terminal::AnsiTerminal::switch_to "switch_to(0)" - jump to the first page                  |
| Alt+1 ... Alt+9       | @ref Botanik::Terminal::AnsiTerminal::switch_to "switch_to(0...8)"                                         |
| Alt+0               | @ref Botanik::Terminal::AnsiTerminal::switch_to "switch_to(9)" (if registered)                           |
| TAB / Ctrl+PageDown | @ref Botanik::Terminal::AnsiTerminal::switch_relative "switch_relative(true)" - next page, wraps         |
| Ctrl+PageUp         | @ref Botanik::Terminal::AnsiTerminal::switch_relative "switch_relative(false)" - previous page, wraps    |
| Ctrl+C              | @ref Botanik::Pages::Page::handle_cancel "page->handle_cancel()" first; if declined, default: quit (simulator) / no-op (hardware) |

## Shell page

Printable text and editing keys dispatched by
@ref Botanik::Pages::ShellPage::handle_command "ShellPage::handle_command()"
(text arrives via @ref Botanik::Pages::ShellPage::handle_text "handle_text()").
Every editing action drives the @ref Botanik::Terminal::TextEditor line editor.

| Key(s)                | Action                                                                                                 |
|-----------------------|--------------------------------------------------------------------------------------------------------|
| Printable text        | @ref Botanik::Terminal::TextEditor::insert_characters "editor.insert_characters()"                      |
| **Navigation**        |                                                                                                        |
| Ctrl+B / LEFT         | @ref Botanik::Terminal::TextEditor::move_left "editor.move_left()"                                      |
| Ctrl+F / RIGHT        | @ref Botanik::Terminal::TextEditor::move_right "editor.move_right()"                                    |
| Ctrl+A                | @ref Botanik::Terminal::TextEditor::move_to_line_start "editor.move_to_line_start()"                    |
| Ctrl+E / END          | @ref Botanik::Terminal::TextEditor::move_to_line_end "editor.move_to_line_end()"                        |
| Alt+B                 | @ref Botanik::Terminal::TextEditor::move_word_left "editor.move_word_left()"                            |
| Alt+F                 | @ref Botanik::Terminal::TextEditor::move_word_right "editor.move_word_right()"                          |
| Ctrl+P / UP           | @ref Botanik::Shell::CommandShell::history_prev "shell.history_prev()" - stub, not yet implemented      |
| Ctrl+N / DOWN         | @ref Botanik::Shell::CommandShell::history_next "shell.history_next()" - stub, not yet implemented      |
| **Editing**           |                                                                                                        |
| ENTER                 | @ref Botanik::Pages::ShellPage::_submit_command "_submit_command()" - echo, advance, run the line       |
| Alt+ENTER             | @ref Botanik::Terminal::TextEditor::insert_characters "editor.insert_characters()" - insert a newline   |
| BACKSPACE             | @ref Botanik::Terminal::TextEditor::backspace "editor.backspace(count)" (Ctrl+H or DEL)                 |
| Ctrl+D / DELETE       | @ref Botanik::Terminal::TextEditor::delete_forward "editor.delete_forward(count)"                       |
| Ctrl+U                | @ref Botanik::Terminal::TextEditor::reset "editor.reset()" - clear the line                              |
| Ctrl+C                | @ref Botanik::Pages::ShellPage::handle_cancel "handle_cancel()" - echo + discard the line (terminal intercept) |
| Ctrl+K / Ctrl+W / Ctrl+Y | kill ring - TODO, no-op                                                                              |
| Ctrl+R                | history search - TODO, no-op                                                                            |
| **Selection / Mark**  |                                                                                                        |
| Ctrl+Space            | @ref Botanik::Terminal::TextEditor::set_mark_toggle "editor.set_mark_toggle()" - stub, selection deferred |
| Ctrl+G / ESC          | @ref Botanik::Terminal::TextEditor::deselect "editor.deselect()" - stub, selection deferred             |

## Logs page

Handled by @ref Botanik::Pages::LogsPage::handle_command "LogsPage::handle_command()"
(SPACE via @ref Botanik::Pages::LogsPage::handle_text "handle_text()").

| Key(s)            | Action                                                                                     |
|-------------------|---------------------------------------------------------------------------------------------|
| ENTER             | @ref Botanik::Pages::LogsPage::focus "focus()" - clear the content area and replay the tail  |
| SPACE             | @ref Botanik::Pages::LogsPage::_emit_mark "_emit_mark()" - stamp a `--- MARK ---` line        |
| PAGE_UP / PAGE_DOWN | scroll back / forward - TODO, no-op                                                        |

## Home / Status pages

Read-only metric displays, handled by the shared
@ref Botanik::Pages::MetricPage::handle_command "MetricPage::handle_command()".

| Key(s)            | Action                                                                                       |
|-------------------|-----------------------------------------------------------------------------------------------|
| ENTER             | @ref Botanik::Pages::MetricPage::draw_dynamic "draw_dynamic()" - force an immediate refresh    |
| Any other key     | ignored (values refresh on their own cadence)                                                 |

## Notes

- **TAB is a terminal intercept** cycling to the next page (Shift+Tab decodes to the
  same command); shell auto-completion is not wired up.
- **Held keys are coalesced.** The parser batches repeated BACKSPACE / Ctrl+D into a
  single command with a `count`, so the editor does one reconcile pass instead of N -
  hence the `count` argument on `backspace()` / `delete_forward()`.
- **Mark / selection are stubs** (`set_mark_toggle()` / `deselect()` exist for parity).
