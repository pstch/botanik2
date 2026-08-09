# The pages {#user_pages}

Eight pages ship in the default tab order - the full set the console can hold.
Switch between them with `TAB` or `Alt+<number>` (see @ref user_console).

| # | Page | For |
|---|---|---|
| 1 | HOME | at-a-glance readings and a live log tail |
| 2 | SHELL | the command prompt |
| 3 | LOGS | the full scrolling log |
| 4 | ALARMS | the alarm table, and acknowledging alarms |
| 5 | PARAMS | reading and editing parameters |
| 6 | MONITOR | every status variable, with detail |
| 7 | TASKS | what runs each beat, how often, and what it costs |
| 8 | NOTES | a free-text note kept on the panel |

Every entry below says the same four things: what the page is for, what it looks
like, what it shows, and the keys it adds. The four table pages (ALARMS, PARAMS,
MONITOR, TASKS) share their movement keys, listed once under
[Table navigation](#user_pages_nav).

## HOME

The landing page: site identity, and the readings worth a glance.

**Shows** the site name, firmware version and build commit, the uptime, and up to
22 site-chosen readings in three columns. Values refresh every 5 s (uptime every
second), and one that just changed is briefly shown in reverse video so a moving
number catches the eye. The bottom band tails the last six log lines under a
` LOGS ` divider; it does not scroll, so use LOGS for history.

**Keys**

| Key | Action |
|---|---|
| `ENTER` | refresh now |

## SHELL

The `botanik>` prompt, with output and live log lines interleaved above it.

**Shows** command output, plus log entries that arrive *while the page is
active* - the complete log is always on LOGS. `logs off` suppresses the
interleave without affecting command output.

**Keys** are the line editor's; @ref user_shell documents them and the commands.

## LOGS

The full log, newest at the bottom, keeping the last 64 lines.

**Shows** the log as it arrives. While you are scrolled back the view **freezes**
so incoming lines cannot yank it, and a ` v N more ` tag in the top-right counts
what you have not seen.

**Keys**

| Key | Action |
|---|---|
| `UP` / `Ctrl+P` | scroll back one line |
| `DOWN` / `Ctrl+N` | scroll forward one line |
| `ENTER` | return to the live bottom, releasing the freeze |
| `SPACE` | stamp a `--- MARK ---` line, to bracket an action you are about to take |

## ALARMS

One row per alarm condition, and where you acknowledge them.

```
CODE       VARIABLE            ON   LAST EVENT
TOO_HOT    zone1_temp_high     ON   26/07/31 14:02
WTR_LK     tank_leak           OFF  26/07/29 06:41
```

**Shows** the short `CODE` for the eye and the `VARIABLE` that raises it, whole,
so you can `get` or `override` it by name (the declared `alarm_` prefix is
stripped on screen). `ON` means active now, `OFF` that it has fired and cleared,
blank that it never has; the badge is highlighted while an alarm is active **and**
unacknowledged - the same condition that puts it on the alarm bar. The panel below
carries the description, the **Remediation** note and the **Ack'd** time, because
the sentence explaining a condition needs more room than a column.

Filters, cycled with `SPACE` or `LEFT`/`RIGHT`:

| Filter | Shows |
|---|---|
| `recent` (default) | anything active, plus anything that fired inside its own "recent" window |
| `all` | every declared alarm, in fixed table order |
| `active` | active now, acknowledged or not |
| `unmasked` | active and **not** acknowledged - the alarm bar's contents |

Except in `all`, rows sort unacknowledged-active first, then acknowledged-active,
then inactive, most recent onset first within each group. In `all` the order never
moves, which is what you want when comparing against a printed list.

**Keys**

| Key | Action |
|---|---|
| `a` | acknowledge the selected alarm |
| `SPACE`, `LEFT` / `RIGHT` | cycle the filter |

See @ref user_alarms for what acknowledging does.

## PARAMS

The editable parameters - the only page that writes them.

```
PARAMETER          TYPE  VALUE            DEFAULT      UNIT
ntp_server1        str   pool.ntp.org     pool.ntp.org
ntp_poll_s         u16   64               64           s
```

**Shows** the parameter block, with the selected row's **Minimum**, **Maximum**,
**Register**, **Description** and **Group** in the panel below. Views step
through `all`, then one per group.

**Register** is the parameter's address on the Modbus TCP slave - the one number
about a parameter that is an external contract, since a master's register map is
written against it. It is assigned per row and never renumbered by inserting a
parameter, so it is safe to quote.

`ENTER` opens a one-line editor on the bottom row, seeded with the current value:
`ENTER` commits, `Ctrl+C` or `ESC` cancels, and the usual editing keys apply
(`Ctrl+A`/`Ctrl+E`, `Ctrl+U`, `Ctrl+B`/`Ctrl+F`, `BACKSPACE`, `Ctrl+D`). A
rejected value appends `[invalid]` and leaves you in the editor. One that was
accepted but could not be stored shows `NOT SAVED - value is live but storage
refused it`: in effect now, gone after a reboot.

**Keys**

| Key | Action |
|---|---|
| `ENTER` | edit the selected parameter |
| `SPACE`, `LEFT` / `RIGHT` | next / previous group view |

See @ref user_params.

## MONITOR

Every status variable the site publishes. Read-only.

```
SCOPE  VARIABLE            VALUE            UNIT   OVERRIDE
MI     grid_L1_V           237.4            V
MI     grid_L2_V           236.9            V
G      ats_supply_Hz       50.0             Hz     FORCED
```

**Shows** `SCOPE`, the declared kind of the row (`MI`/`AI`/`DI` inputs,
`MO`/`AO`/`DO`/`RO` outputs, `G` computed, `SV` status), and `OVERRIDE`, which
marks a row someone has forced - a forced reading is indistinguishable from a real
one in `VALUE`, which is the whole hazard, so the table says so per row rather
than only in the panel. Rows group by name prefix, one view per group. The panel
carries **Description**, **Notes**, **Source** (which Modbus register, which local
channel, or `computed`) and **Quality**:

| Quality | Meaning |
|---|---|
| `live` | a driver is publishing it and it is fresh |
| `no data (showing default)` | nothing has written it, or it timed out - the number shown is the declared default |
| `OVERRIDDEN (forced by hand)` | someone forced a value; see @ref user_variables |
| `OVERRIDDEN (forced invalid)` | someone forced it to read as a dead sensor |

**Keys**

| Key | Action |
|---|---|
| `SPACE`, `LEFT` / `RIGHT` | next / previous group view |

This page never writes - use `set` or `override` from the shell.

## TASKS

What the beat actually costs, one row per registered task. Read-only.

```
TASK             GROUP  EVERY ms  PHASE    MEAN us  MISSED
console          slow   100       present      412        0
expire_stale     bkgnd  500       input         88        0
scan             fast   10        execute       21        3
```

**Shows** where the beat's time goes. `stats` reports the whole beat's CPU with no
attribution; this is where "which task blew the budget" has an answer. `MEAN` says
whether a task is affordable. `MISSED` is the count of activations the task never
got: a task that comes due late runs **once** and the ones it slept through are
dropped, never caught up, so this is the only trace they leave - a non-zero figure
means the task is not running at the rate it declared. Rows sort by name: the
beat's own order is phase-major and the `PHASE` column still carries it, but
finding one task by eye beats reading the cycle here. The panel carries the last
and peak samples, the run and missed counts, and the share of its own period the
mean takes - the peak is an all-time maximum that never decays, so one outlier
pins it for the rest of the boot and it belongs there as a diagnostic rather than
in the table as a running cost.

**Keys**: table navigation only.

## NOTES

A free-text note kept on the panel: what was changed, what to check next, what the
last visit found.

**Shows** a full-screen editor with the same keys as the shell prompt (see
@ref user_shell). Rows a wrapped line spills onto are marked differently from
lines you started, so a paragraph reads as one thing, and the status row says
whether there are unsaved changes.

Saving is deliberate rather than continuous - a note is kilobytes and each write
is a real flash erase - so `Ctrl+O` writes, and leaving the page flushes anything
unsaved.

**Keys**

| Key | Action |
|---|---|
| `Ctrl+O` | write the note to flash |
| `UP` / `DOWN` | move the cursor (there is no history here) |

## Table navigation {#user_pages_nav}

Shared by ALARMS, PARAMS, MONITOR and TASKS.

| Key | Action |
|---|---|
| `UP` / `Ctrl+P` | up one row |
| `DOWN` / `Ctrl+N` | down one row |
| `PAGE_UP` / `PAGE_DOWN` | up / down half a screen |
| `END` | last row |
| `SPACE`, `LEFT` / `RIGHT` | next / previous view, where the page has views |
| `ENTER` | back to the top (ALARMS, MONITOR) |

`^` and `v` marks on the table's top and bottom rules show when rows are hidden
above or below.
