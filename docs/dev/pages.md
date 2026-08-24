# The page framework {#dev_pages}

A *page* is a full-screen view multiplexed by @ref Botanik::Terminal::Console. Every
page implements the @ref Botanik::Pages::Page interface; the console holds up to 8 of
them, routes input to the active one (see @ref dev_input), and ticks all of them each
loop.

What each shipped page shows and how an operator drives it is in @ref user_console;
this page is the framework behind them.

## Screen contract

```
┌────────────────────────────────────────────────────┐
│ Row 0    status bar (page tabs + live metrics)     │ ← Console
├────────────────────────────────────────────────────┤
│          page content area (rows 1 .. N-2)         │ ← active page
├────────────────────────────────────────────────────┤
│ Row N-1  alarm bar (reserved when alarms are wired)│ ← Console
└────────────────────────────────────────────────────┘
```

The inverted **status bar** on row 0 (page tabs left, live metrics right) and the
**alarm bar** on the bottom row both belong to the terminal, which repaints them over
every page. A page owns the rows between them and draws there only while active. The
alarm-bar row is reserved when an `AlarmController` was wired at construction
(`_reserved_bottom`) - a static property of the build, not something that comes and goes
with active alarms. A page still reads its usable row count from the terminal rather than
assuming a fixed height. The terminal clears the content area on
every page switch, so `focus()` always paints onto a blank canvas.

The status bar, the `stats` command and the metric pages read one borrowed-pointer
aggregate, @ref Botanik::SystemContext, rather than reaching into each subsystem -
see @ref dev_architecture.

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
@ref dev_logging).

## Testing: page behaviour

Each page has a behaviour test under `tests/pyte/` - what pressing a key does,
what a page reports - driven against the real firmware over a PTY.

Full-screen goldens are now the exception rather than the rule: `page_home` is
the only one left (`test_page_behaviour.py`). A `test_pages.py` used to hold one
per page, plus a tab-strip check that failed when a page was registered without
one; it was removed because it cost ~10 s of the gate. Adding a page therefore
means adding a BEHAVIOUR test - nothing fails automatically if you do not.

A golden pins *layout* - rows, order, labels, columns, rules, chrome - and not
*values*: `tests/pyte/normalize.py` masks every cell the greenhouse moves, so a
renamed column fails and a changed reading does not. Page *behaviour* (a key
that scrolls, an edit that commits) belongs in the per-page files beside it,
asserted inline. See `tests/pyte/README.md` for the full rationale, including
why LOGS and ALARMS are pinned the way they are.

## Hierarchy

\startuml "the page class hierarchy: every page is library-owned and site-independent; three of them double as log writers"
hide members
hide circle
Page <|-- ShellPage
Page <|-- LogsPage
Page <|-- NotesPage
Page <|-- MetricPage
Page <|-- ScrollTablePage
MetricPage <|-- HomePage
MetricPage <|-- "StatusPage<T>"
ScrollTablePage <|-- AlarmsPage
ScrollTablePage <|-- ParamsPage
ScrollTablePage <|-- MonitorPage
ScrollTablePage <|-- TasksPage
LogWriter <|-- ShellPage
LogWriter <|-- LogsPage
LogWriter <|-- HomePage
ShellWriter <|-- ShellPage
\enduml

One row per page, in the order they derive:

| Page | Base | Is |
|---|---|---|
| @ref Botanik::Pages::ShellPage | Page | the interactive shell: a @ref Botanik::Terminal::TextEditor on the bottom row over a @ref Botanik::Shell::CommandShell, with a log tail interleaved above. Also a `LogWriter` and `ShellWriter` |
| @ref Botanik::Pages::LogsPage | Page | the full scrolling log; a `LogWriter` that replays its @ref Botanik::Pages::LogsScrollback on focus |
| @ref Botanik::Pages::NotesPage | Page | the operator's note, behind `BOTANIK_FEATURE_NOTES_PAGE`. Same editor as the shell but top-anchored with a scrolling viewport (@ref dev_editor); persisted under its own `HAL::Storage` key |
| @ref Botanik::Pages::MetricPage | Page | base for read-only metric displays |
| @ref Botanik::Pages::HomePage | MetricPage | the landing page (also a `LogWriter`) |
| @ref Botanik::Pages::StatusPage "StatusPage<T>" | MetricPage | a two-column health grid templated on a site struct. Shipped but **not** in the default tab order |
| @ref Botanik::Pages::ScrollTablePage | Page | base for the scrollable tables |
| @ref Botanik::Pages::AlarmsPage | ScrollTablePage | the alarm conditions |
| @ref Botanik::Pages::ParamsPage | ScrollTablePage | the parameter block, and the only page that writes it |
| @ref Botanik::Pages::MonitorPage | ScrollTablePage | every published variable, resolved from `VarRef`s at render time; the one read-only table that also WRITES, through the override keys |
| @ref Botanik::Pages::TasksPage | ScrollTablePage | the beat as a table, off the scheduler's `Tasks::TaskCost` samples |

Three of those entries carry a decision worth knowing:

**`MetricPage` repaints its fields whole** and lets the back-buffer diff drop
whatever did not move, so a refresh never saturates the low-baud link. Its one
value cache exists only to spot *which* values changed for the `watch -d`-style
highlight - a question about the data, which the diff cannot answer (it knows
which cells differ, not why).

**`ScrollTablePage` owns the scroll offset, the selection cursor, the shared
navigation keys, the row filter, the legend line and the `^`/`v` hidden-row
marks**, and leaves the column layout and the detail panel to the subclass. That
split is why four tables cost four column layouts rather than four scrollers.

**Two pages open an inline field** - PARAMS on a parameter, MONITOR on an
override - so the editing keys live in the base (`_edit_kind()` classifies,
`_edit_apply()` applies) and only what `ENTER` commits to is per-page. The split
is what lets the marker for a rejected value be dropped BEFORE the edit that
moves the field's end.

**The row filter is the base's, the predicate the page's.** The base holds the
typed text, the keys that edit it (typing appends, `BACKSPACE` pops, `ESC`
clears) and a GENERATION counter it bumps on every change; a page calls
`_filter_match()` from its own row predicate and caches whatever that walk costs
against `_filter_gen`. Nothing per-page decides what a filter IS, so the four
tables cannot drift on it - only on which cell they match against (the variable,
parameter, condition or task name). View MODES stay a separate axis, on the same
`LEFT`/`RIGHT` keys: `AlarmsPage` selects on what an alarm is doing, `MonitorPage`
on the two things its VALUE column already flags per row - a reading that is not
good, and a standing override - and the filter narrows whichever view is showing.

**`NotesPage` drives the same PS1/PS2/PS3 gutter the shell does** - `"> "` on a
row the operator started, `"  "` on a soft-wrapped tail - so a wrapped line reads
as a continuation rather than a new one. Up/down are cursor motion here; history
belongs to `CommandShell`.

Every page is library-owned and site-independent. Sites contribute **data** - the
home fields, the status groups, the alarm table - not page classes. `HomePage` and
`MonitorPage` resolve their content generically from `VarRef`s at render time, which
is why adding a reading to a site needs no code at all.

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
@ref dev_screen). What they do bound is how much *work* a beat
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

## AlarmCondition

Each row of @ref Botanik::Pages::AlarmsPage is one
@ref Botanik::Alarms::AlarmCondition - a **static** metadata prefix the site defines
by aggregate-init, followed by a **dynamic** suffix the
@ref Botanik::Alarms::AlarmController owns and drives (left zeroed at init). A threshold
of `-1` disables that action; dwell fields are in seconds.

\startuml "AlarmCondition: site-defined static metadata + model-owned dynamic state"
class AlarmCondition {
  code : const char* - short code (<= 8 chars)
  name : const char* - the variable it reads, by name
  desc : const char* - human description
  notes : const char* - remediation text (may be null)
  on_s : int32 - ON dwell before announce (-1 off)
  off_s : int32 - OFF dwell before announce (-1 off)
  recent_s : int32 - "recent" window (0 = a week, <0 forever)
  hold_s : int16 - stay active past the last raw true
  --
  ref : VarRef - the boolean variable carrying the condition
  local_raw : bool - locally asserted, OR'd with ref
  active : bool - active or latched
  on_sent : bool - ON transition announced
  off_sent : bool - OFF transition announced
  user_ack : bool - acknowledged by the user
  ack_ms : uint64 - when it was acknowledged
  edge_ms : uint64 - since last transition
  started_ms : uint64 - since last ON transition
  hold_on_rem_ms : uint32 - hold-on countdown
  min_on_rem_ms : uint32 - minimum-ON countdown
}
\enduml

The `--` separator splits the site-owned static metadata from the model-owned dynamic
state; `step()` advances the countdowns and announces due ON/OFF transitions each
tick.

There is **no aggregate bitset**. Each condition names the `VarRef` of the boolean
that carries it and the model reads that variable per condition, OR'd with a local
override for the platform alarms. A row written out positionally (the platform
table) gives all four text fields, `notes` included, before the dwell numbers.
