# Pages

A *page* is a full-screen view multiplexed by @ref Botanik::Terminal::Console.
Every page implements the @ref Botanik::Pages::Page interface; the terminal holds
up to 8 of them, routes input to the active one (see
[EVENT_DISPATCH.md](EVENT_DISPATCH.md)), and ticks all of them each loop.

## Screen contract

```
┌────────────────────────────────────────────────────┐
│ Row 0    status bar (page tabs + live metrics)     │ ← Console
├────────────────────────────────────────────────────┤
│          page content area (rows 1 .. N-2)         │ ← active page
├────────────────────────────────────────────────────┤
│ Row N-1  alarm bar (only while alarms active)      │ ← Console
└────────────────────────────────────────────────────┘
```

The inverted **status bar** on row 0 (page tabs left, live metrics right) and the
**alarm bar** on the bottom row both belong to the terminal, which repaints them over
every page. A page owns the rows between them and draws there only while active. The
alarm-bar row is reserved only while at least one alarm is active (`_reserved_bottom`);
with no alarms it reverts to the page, so a page reads its usable row count from the
terminal rather than assuming a fixed height. The terminal clears the content area on
every page switch, so `focus()` always paints onto a blank canvas.

## Status contexts

The status bar, the `stats` command, and the metric pages read one
borrowed-pointer aggregate, @ref Botanik::SystemContext, rather than reaching
into each subsystem - see @ref md_docs_2CONTEXTS "Contexts".

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

\startuml "the page class hierarchy: site pages (Greenhouse*) derive the templated library pages; some pages are also log/shell writers"
hide members
hide circle
Page <|-- ShellPage
Page <|-- LogsPage
Page <|-- MetricPage
MetricPage <|-- "HomePage<T>"
MetricPage <|-- "StatusPage<T>"
"HomePage<T>" <|-- GreenhouseHomePage
"StatusPage<T>" <|-- GreenhouseStatusPage
LogWriter <|-- ShellPage
LogWriter <|-- LogsPage
LogWriter <|-- "HomePage<T>"
ShellWriter <|-- ShellPage
\enduml

- @ref Botanik::Pages::ShellPage - interactive shell; owns a
  @ref Botanik::Terminal::TextEditor (bottom row) and a
  @ref Botanik::Shell::CommandShell, with a live log tail interleaved with command
  output above. Also a `LogWriter` and `ShellWriter`.
- @ref Botanik::Pages::LogsPage - the full scrolling log; a `LogWriter` that replays
  its @ref Botanik::Pages::LogsScrollback on focus.
- @ref Botanik::Pages::MetricPage - base for read-only metric displays. Repaints
  its fields whole; the terminal's back-buffer diff drops whatever did not move, so
  a refresh never saturates the low-baud link. It keeps one value cache of its own,
  and only to spot *which* values changed for the `watch -d`-style highlight - a
  question about the data, which the diff cannot answer (it knows which cells
  differ, not why).
  - @ref Botanik::Pages::HomePage "HomePage<T>" - landing page (also a `LogWriter`).
  - @ref Botanik::Pages::StatusPage "StatusPage<T>" - full runtime/health display.

Both metric pages are templated on the site PLC output payload; the concrete
`GreenhouseHomePage` / `GreenhouseStatusPage` fill in the site-specific readings.

## Scrollback

Pages that show log lines keep them in a fixed-size circular
@ref Botanik::Pages::Scrollback (aliased per page: `ShellScrollback`,
`LogsScrollback`, `HomeScrollback`, each sized by a `Config::*_SCROLLBACK_LINES`
knob). It keeps the log UI both **bounded in RAM** and **safe on a low-baud serial
link**: paint paths are incremental and bounded per beat, and the terminal paces the
resulting bytes, so a burst of log lines can never block the main loop or flood the
TX ring.

`Scrollback` extends a @ref Botanik::Pages::LineBuffer "LineBuffer" - a ring of `L`
rows. Lines are stored *pre-wrapped* (split into screen-width rows at insert time), so
replay is a straight row-by-row blit; oldest rows fall off as new ones arrive.

It serves three jobs. None of them meters the wire itself: they draw into the
terminal's FRONT grid and `present()` paces what that costs (see
@ref md_docs_2SCREEN__BACKBUFFER). What they do bound is how much *work* a beat
does, so a burst never stalls the loop:

- **Live tail.** While the page is active, a new line is banked and `drain_tail()`
  paints pending rows per beat, scrolling the band up (one IL/DL) and writing new rows
  at the bottom. A *background* page skips this, banking lines and repainting on its
  next `focus()`.
- **Focus / page-switch replay.** On `focus()`, Ctrl+L, or returning to the live
  bottom, it stages a bottom-aligned redraw and drains it into the grid; the paced
  diff then spreads the bytes across as many ticks as the link needs.
- **Scrollback navigation.** The view moves back through retained history a key at a
  time, painting only newly exposed rows. While scrolled up the live tail is **frozen**
  so incoming lines don't yank the view; `scroll_to_bottom()` releases it.

## Alarms

@ref Botanik::Pages::AlarmsPage renders the alarm-condition table as a scrollable view
(filter modes: recent / all / active / unmasked). Each row is one
@ref Botanik::Alarms::AlarmCondition - a **static** metadata prefix the site defines by
aggregate-init (the row index is the alarm's bit index) followed by a **dynamic** suffix
the @ref Botanik::Alarms::AlarmModel owns and drives (left zeroed at init). A threshold
of `-1` disables that action; dwell fields are in seconds.

\startuml "AlarmCondition: site-defined static metadata + model-owned dynamic state"
class AlarmCondition {
  code : const char* - short code (<= 8 chars)
  desc : const char* - human description
  masked_by : uint64 - alarms that suppress this one
  on_s : int32 - ON dwell before announce (-1 off)
  off_s : int32 - OFF dwell before announce (-1 off)
  recent_s : int32 - "recent" window (-1 forever)
  off_delay_s : int32 - hold ON after raw clears
  min_on_s : int32 - minimum ON latch from onset
  --
  active : bool - active or latched
  unmasked : bool - active and not masked
  on_sent : bool - ON transition announced
  off_sent : bool - OFF transition announced
  user_ack : bool - acknowledged by the user
  edge_ms : uint64 - since last transition
  started_ms : uint64 - since last ON transition
  off_delay_rem_ms : uint32 - off-delay countdown
  min_on_rem_ms : uint32 - minimum-ON countdown
}
\enduml

The `--` separator splits the site-owned static metadata (above) from the model-owned
dynamic state (below); @ref Botanik::Alarms::AlarmCondition::step "step()" advances the
countdowns and announces due ON/OFF transitions each tick.
