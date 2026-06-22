# Pages

A *page* is a full-screen view multiplexed by @ref Botanik::Terminal::AnsiTerminal.
Every page implements the @ref Botanik::Pages::Page interface; the terminal holds
up to 8 of them, routes input to the active one (see
[EVENT_DISPATCH.md](EVENT_DISPATCH.md)), and ticks all of them each loop.

## Screen contract

```
 Row 0        ████ status bar ████   ← AnsiTerminal: page tabs + live metrics
 Row 1                                ┐
 …            page content area       ├ rows owned by the active page
 Row N-2                              ┘
 Row N-1      ████ alarm bar ████     ← AnsiTerminal: shown only while alarms are active
```

Both the inverted **status bar** on row 0 (page tabs on the left, live metrics on
the right) and the **alarm bar** on the bottom row belong to the terminal, which
repaints them cooperatively over every page. A page owns the rows between them and
draws only there, and only while active. The alarm bar's row is reserved only while
at least one alarm is active (`_reserved_bottom` = 1); with no alarms the bottom row
reverts to the page, so a page reads its usable row count from the terminal rather
than assuming a fixed height. The terminal clears the content area on every page
switch, so `focus()` always paints onto a blank canvas.

## The Page interface

@ref Botanik::Pages::Page declares the contract:

| Method                | When it is called                                                         |
|-----------------------|---------------------------------------------------------------------------|
| `name()`              | Status-bar tab label (e.g. `"SHELL"`).                                     |
| `handle_text(c)` / `handle_text_batch()` | A printable character / run arrived.                   |
| `handle_command(cmd, count)` | A decoded command key arrived (`count` > 1 for coalesced keys).    |
| `focus()`             | The page just became active (screen already cleared).                     |
| `redraw()`            | In-place repaint preserving input state (Ctrl+L); defaults to `focus()`.  |
| `deactivate()`        | The page is about to be replaced.                                         |
| `tick(active)`        | Every loop, for *every* page; `active` marks the one that owns the screen.|

`tick(active)` running for background pages too is the key to the log path:
an inactive page banks queued entries without drawing (see
[LOG_FLOW.md](LOG_FLOW.md)).

## Hierarchy

\dot "the page class hierarchy (library pages in blue, site pages in green)"
digraph pages {
  stylesheet="graphs.css";
  bgcolor="transparent"; rankdir=TB; nodesep=0.35; ranksep=0.55;
  node [shape=plaintext, fontname="Helvetica", fontsize=9];
  edge [arrowsize=0.8, arrowhead=empty];

  page [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e">
    <tr><td bgcolor="#37474f"><font color="#ffffff"><b>Pages::Page</b></font></td></tr>
    <tr><td bgcolor="#eceff1"><font color="#555555">abstract interface</font></td></tr></table>>];
  shell [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e">
    <tr><td bgcolor="#37474f"><font color="#ffffff"><b>ShellPage</b></font></td></tr>
    <tr><td bgcolor="#d6e4f0" align="left">+ TextEditor, CommandShell</td></tr>
    <tr><td bgcolor="#d6e4f0" align="left">+ ShellWriter / LogWriter</td></tr></table>>];
  logs [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e">
    <tr><td bgcolor="#37474f"><font color="#ffffff"><b>LogsPage</b></font></td></tr>
    <tr><td bgcolor="#d6e4f0" align="left">+ LogWriter</td></tr></table>>];
  metric [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e">
    <tr><td bgcolor="#37474f"><font color="#ffffff"><b>MetricPage</b></font></td></tr>
    <tr><td bgcolor="#d6e4f0"><font color="#555555">diff-rendered base</font></td></tr></table>>];
  home [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e">
    <tr><td bgcolor="#37474f"><font color="#ffffff"><b>HomePage&lt;T&gt;</b></font></td></tr>
    <tr><td bgcolor="#d6e4f0" align="left">+ LogWriter</td></tr></table>>];
  status [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e">
    <tr><td bgcolor="#37474f"><font color="#ffffff"><b>StatusPage&lt;T&gt;</b></font></td></tr>
    <tr><td bgcolor="#d6e4f0"><font color="#555555">runtime / health</font></td></tr></table>>];
  ghome [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e">
    <tr><td bgcolor="#2e7d32"><font color="#ffffff"><b>GreenhouseHomePage</b></font></td></tr>
    <tr><td bgcolor="#d7ecd9"><font color="#555555">site readings</font></td></tr></table>>];
  gstatus [label=<<table border="0" cellborder="1" cellspacing="0" cellpadding="4" color="#9e9e9e">
    <tr><td bgcolor="#2e7d32"><font color="#ffffff"><b>GreenhouseStatusPage</b></font></td></tr>
    <tr><td bgcolor="#d7ecd9"><font color="#555555">site readings</font></td></tr></table>>];

  page -> shell;
  page -> logs;
  page -> metric;
  metric -> home;
  metric -> status;
  home   -> ghome;
  status -> gstatus;
}
\enddot

- @ref Botanik::Pages::ShellPage - interactive shell. Owns a
  @ref Botanik::Terminal::TextEditor on the bottom row and a
  @ref Botanik::Shell::CommandShell; the rows above show a live log tail
  interleaved with command output. Also a `LogWriter` and a `ShellWriter`.
- @ref Botanik::Pages::LogsPage - the full scrolling log. A `LogWriter` that
  replays its @ref Botanik::Pages::LogsScrollback on focus.
- @ref Botanik::Pages::MetricPage - base for read-only metric displays. Diffs
  each value field against the last paint and re-emits only the bytes that
  changed (`watch -d`-style highlighting), so a refresh never saturates the
  low-baud link. Subclasses fill `draw_static()` (labels, once) and
  `draw_dynamic()` (values, periodically).
  - @ref Botanik::Pages::HomePage "HomePage<T>" - landing page: logo, a compact
    summary, and a bottom live-log band (so it is also a `LogWriter`).
  - @ref Botanik::Pages::StatusPage "StatusPage<T>" - full runtime/health
    display.

Both metric pages are templated on the site PLC output payload; the concrete
`GreenhouseHomePage` / `GreenhouseStatusPage` in `botanik_sim/main.cpp` fill in
the site-specific readings.

## Scrollback

Pages that show log lines keep them in a fixed-size circular
@ref Botanik::Pages::Scrollback (aliased per page: `ShellScrollback`,
`LogsScrollback`, `HomeScrollback`, each sized by a `Config::*_SCROLLBACK_LINES`
knob). It is the piece that makes the log UI both **bounded in RAM** and **safe on a
low-baud serial link**: every paint path is incremental, paced per tick, and
back-pressure-aware, so a burst of log lines can never block the main loop or flood
the TX ring.

**Storage.** `Scrollback` extends a @ref Botanik::Pages::LineBuffer "LineBuffer" - a
ring of `L` rows, each up to `Config::SCROLLBACK_LINE_LEN` bytes. Lines are stored
*pre-wrapped* (`store_wrapped` splits a long line into screen-width rows at insert
time), so replay is a straight row-by-row blit with no re-wrapping, and access is
newest-first by distance. Oldest rows fall off the ring as new ones arrive.

It serves three jobs, each gated against the TX ring (`tx_below_cold()`) and capped
per tick so no single beat can overrun the serial output:

- **Live tail** (`ingest` / `note_pending` -> `drain_tail`). While the page is
  active, a new line is banked and marked *pending*; `drain_tail()` paints up to
  `Config::LOG_ROWS_PER_TICK` pending rows per beat, scrolling the window band up
  (`emit_tail`: one IL/DL region scroll) and writing the new rows at the bottom. A
  *background* page skips this - it just banks lines and repaints from scratch on its
  next `focus()`.
- **Focus / page-switch replay** (`begin_redraw` -> `drain_redraw`). On `focus()`,
  `redraw()` (Ctrl+L), or returning to the live bottom, it stages a bottom-aligned
  redraw of the visible window and emits at most `Config::SCROLLBACK_ROWS_PER_TICK`
  rows per beat, stopping early if the next row won't fit TX - so a full-screen
  replay is spread across several ticks instead of flooding the link at once.
- **Scrollback navigation** (`scroll` / `scroll_to_bottom`). The view can move back
  through retained history a key at a time, painting only the newly exposed rows.
  While scrolled up the live tail is **frozen** (`drain_tail` no-ops) so incoming
  lines don't yank the view; `scroll_to_bottom()` releases it with a staged replay of
  the newest window.
