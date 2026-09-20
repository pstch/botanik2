# Pages {#dev_pages}

<!-- source of truth: console/page.h, pages/metric_page.h, pages/scroll_table.h -->

Nine full-screen views, one at a time, behind one interface. Every one is owned
by `BotanikFirmware` and independent of any site: a site does not write a page,
it declares rows and the pages show them.

The rendered shape of each is a committed artifact - `make docs-screens` boots a
simulator and captures one 80x24 screen per page into
`docs/generated/screens/`. Those are the pictures; this page is the reasons.

## The Page interface

`Terminal::Page` (`console/page.h`) is nine virtuals: a `name()` for the tab, the
input hooks (`handle_text`, `handle_text_batch`, `handle_command`,
`handle_cancel`), and the lifecycle (`focus`, `deactivate`, `redraw`, `tick`).

It lives beside `Console`, not in `pages/`, because `Console` is what
dereferences it. `pages/` holds implementations.

Two things about `tick(bool active)` are easy to get wrong:

- It is called for **every** page, not just the one on screen, so a page can
  drain a queue in the background. An inactive page must emit nothing.
- It is the only place per-tick work belongs. `paint()` may run twice in a tick;
  anything that advances state from a paint advances it at double rate.

## Focus and redraw

**Every page redraws its whole screen and lets the terminal pare it.** There is
no per-page paint cache anywhere in `pages/`, and there should not be:
`AnsiTerminal` owns a back-buffer and emits only the cells that differ, so an
unchanged row costs zero bytes. A page-side cache would duplicate that, four
times, each slightly differently.

This is the module's central bargain and it is a byte bargain. At 4800 baud a
full 80x24 repaint is about 1,920 bytes, or **four seconds of wire**. Everything
that looks like over-engineering in `pages/` falls out of that number:

- **Refresh cadences.** `PAGE_HOT_MS` (1 s), `PAGE_COLD_MS` (5 s),
  `PAGE_TASKS_MS` (2 s). A 1 Hz whole-table repaint would be around 40% of the
  link on its own.
- **Hot and cold sets must be disjoint.** A field drawn from both paints twice a
  cycle and flickers.
- **A changed digit run is drawn reverse-video and cleared once it settles**, and
  is never lit on a repaint - only on a real change. That is why `MetricPage`
  reads the FRONT grid back: it needs to know *what* changed, not which cells
  moved.
- **Band scrolls are IL/DL.** The target terminal cannot set a scroll region
  (no DECSTBM), so a scrolling table inserts and deletes lines. Every write to
  the window top says whether it announces one, because a one-row band is bytes
  for nothing.

The last one has a measured case behind it: cycling a view does **not** blank the
band first, because a queued band scroll goes out ahead of the diff and outside
its pacing - 320 bytes in the first frame against 106. `test_alarms.cpp` pins it.

## The page table

Tab order is registration order, and page 0 is the boot page, so it is
positional. `BotanikFirmware::register_pages()` is the hook a site overrides to
change it.

Every page is behind its own `BOTANIK_FEATURE_*_PAGE` flag, all of which require
`CONSOLE`; `LOGS_PAGE` also requires `LOGGING`, and `SHELL_PAGE` gates the whole
command interpreter because `ShellPage` owns `Shell::CommandShell`.

## Fields and tables

Two bases, and three pages that use neither.

**`MetricPage`** - a read-only grid of labelled values. HOME and SYSTEM. It owns
the focus/refresh lifecycle, the cell renderer and the change highlight. The
invariant it exists to hold is the hot/cold disjointness above: two copies of
that rule would drift.

**`ScrollTablePage`** - a bordered, scrolling, filterable table with a selection
cursor. ALARMS, PARAMS, MONITOR and TASKS. A subclass declares its columns in
about seven lines and inherits the fit-to-display solve, the row builder, the
header, the rules with their scroll caps, the centred legend, the glob filter,
the Alt+W copy, the band walk and the IL/DL scroll decision.

It is a net saving in lines, but that is not the reason it exists. The reason is
that all four tables scroll and lay out identically, and the window top moves in
exactly one place - which is where the band-scroll byte decision is made. Four
tables each owning that is how a byte regression gets in.

**LOGS, SHELL and NOTES use neither**, correctly: they are not metric grids and
not tables. LOGS and SHELL share `Scrollback` by composition, which is the right
relationship - a third base for three pages would not pay.

## The layouts

Row 0 is the terminal's status bar and the bottom row its alarm bar; neither
belongs to the page. `N` is `Console::rows()`.

A page that restates what the chrome already carries wastes the one screen an
operator opens when something is wrong, which is why SYSTEM does not repeat the
bar.

### MONITOR

```
row 1          blank top margin
row 2          header  (| KIND | VARIABLE | VALUE | UNIT | OVERRIDE |)
row 3          top rule (corners cap to '^' when rows are hidden above)
rows 4..N-10   one row per published row (a window into the full set)
row N-9        bottom rule (corners cap to 'v' when hidden below)
rows N-7..N-3  detail panel: Description / Notes / Mapping / Quality
row N-2        legend: view badge + position + [filter] + hint
row N-1        the override field, or what the last override key did
```

LEFT/RIGHT step the view: every published row, only the ones reading dead, or
only the ones being forced. The typed filter narrows whichever is showing.

The one page that **forces** a row as well as showing it: `^O` opens an override
field on the bottom row, `^V` toggles whether the force reads good or dead, `^R`
releases it, `^S` writes it to flash. All four act on the row under the cursor.
An override is live the moment it commits and is lost at the next boot until
saved. The audit line is `Var`'s, not the page's - see @ref dev_blind_spots.

### PARAMS

```
row 1          blank top margin
row 2          header  (| PARAMETER | TYPE | VALUE | UNIT | REGISTER |)
row 3          top rule
rows 4..K      one row per parameter (windowed / scrollable)
rows K..       detail panel: Minimum / Maximum / Default, Description, Notes
last_row()     edit prompt / hint line, just above the alarm bar
```

ENTER opens an inline edit on the prompt row: a static ` name = ` label and a
single-line editor anchored to the bottom row, seeded with the row's current
text. ENTER commits through the band - which may name another row, so the limit
is whatever that row holds now - and ESC cancels. An invalid value is refused **in
place**, flagged, and leaves the field open.

An applied value is live but not durable, and the page says so three ways at
once: a trailing `*` on the row, a count on the prompt row, and `^O` to commit.

### ALARMS

```
row 1          blank top margin
row 2          header  (| CODE | VARIABLE | ON | ACK | OVR | LAST EVENT |)
row 3          top rule; corners cap to '^' when more above
rows 4..N-9    data rows
row N-8        bottom rule; corners cap to 'v' when more below
row N-7        what the last keypress had to say, else blank
rows N-6..N-4  detail panel: Description / Remediation / Acknowledged
row N-3        blank
row N-2        legend: reverse-video view badge, counts, filter
row N-1        blank
```

On the selected condition: ENTER toggles the acknowledgement, `^O` flips the
boolean override, `^R` releases it, `^S` commits it.

Rows are ordered active-first then by most-recent onset, **except** in the `all`
view, which keeps table order so rows do not reorder underneath the cursor as
alarms toggle. An acknowledgement belongs to one episode: it is demoted in the
ordering, never hidden.

### TASKS

```
row 1          blank top margin
row 2          header (| TASK | GROUP | HEALTH | PHASE | MEAN us | MISSED |)
row 3          top rule
rows 4..K      one row per registered task (windowed / scrollable)
rows K..       detail panel: last and peak sample, run and missed counts, and
               the share of its own period the mean takes
last_row()-1   legend: position, filter, hint
```

Read-only. `stats` reports the whole beat's CPU with no attribution, so *which
task blew the budget* is the question this page answers.

Rows are the site's control programs first, then the library's `sys_` tasks,
each block by name. The beat's own order is phase-major and the PHASE column
still carries it, but finding one task by eye beats reading the cycle here.

`HEALTH` is one vocabulary for both kinds of entry (`Plc::Health`): a control
program's verdict on its last activation, and a component's on itself. Blank
only for a program that did not opt in.

`MEAN` is microseconds measured around each dispatch and says whether a task is
affordable. `MISSED` takes the column beside it rather than the peak, because
only one of the two is a fault: the peak never decays - one outlier pins it for
the boot - and lives in the panel, while a missed activation means the task did
not run at the rate it declared and was dropped, never caught up.

## Writing one

1. Implement `Terminal::Page`, or derive `MetricPage` (a value grid) or
   `ScrollTablePage` (a table). Do not add a third base.
2. Draw immediate-mode. Never cache paint state; the terminal diff is the cache.
3. Put per-tick work in `tick()`, never in a paint.
4. Keep the hot and cold field sets disjoint.
5. Register it in `register_pages()`, remembering that order is the tab order
   and page 0 boots.
6. Gate it behind a `BOTANIK_FEATURE_*_PAGE` flag, and guard the whole `.cpp`
   body rather than the declarations.
7. Add a case to the site's pyte screens so the layout is asserted at every
   supported geometry, not just at 80x24.
