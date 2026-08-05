# The pages {#user_pages}

Eight pages ship in the default tab order - the full set the console can hold.
Switch between them with `TAB` or `Alt+<number>` (see @ref user_console).

| # | Page | For |
|---|---|---|
| 1 | HOME | at-a-glance readings and a live log tail |
| 2 | SHELL | the command prompt |
| 3 | LOGS | the full scrolling log |
| 4 | ALARMS | the alarm table, and acknowledging alarms |
| 5 | PARAMS | reading and editing settings |
| 6 | MONITOR | every status variable, with detail |
| 7 | TASKS | what runs each beat, how often, and what it costs |
| 8 | NOTES | a free-text note kept on the panel |

Keys shared by the four table pages (ALARMS, PARAMS, MONITOR, TASKS) are listed once
under [Table navigation](#user_pages_nav). LOGS scrolls differently and has its own
list, and NOTES is an editor rather than a table.

## HOME

The landing page. It shows the site name, firmware version and build commit, the
uptime, and up to 22 site-chosen readings laid out in three columns. Values refresh
every 5 s (the uptime every second), and a value that just changed is briefly shown
in reverse video so a moving number catches the eye.

The bottom band is a live tail of the last six log lines, under a ` LOGS ` divider.
It does not scroll - use the LOGS page for history.

`ENTER` forces an immediate refresh; nothing else does anything.

## SHELL

The `botanik>` prompt, with command output and live log lines interleaved above it.
Fully documented in @ref user_shell.

Only log entries that arrive **while the page is active** appear here; the complete
log is always on LOGS. `logs off` suppresses the log interleave without affecting
command output.

## LOGS

The full log, newest at the bottom, keeping the last 64 lines.

| Key | Action |
|---|---|
| `UP` / `Ctrl+P` | scroll back one line |
| `DOWN` / `Ctrl+N` | scroll forward one line |
| `ENTER` | return to the live bottom and repaint |
| `SPACE` | stamp a `--- MARK ---` line, to bracket an action you are about to take |

While you are scrolled back the view **freezes** so incoming lines cannot yank it,
and a ` v N more ` tag in the top-right counts what you have not seen. `ENTER`
releases it.

## ALARMS

One row per alarm condition:

```
CODE       VARIABLE            ON   LAST EVENT
TOO_HOT    zone1_temp_high     ON   26/07/31 14:02
WTR_LK     tank_leak           OFF  26/07/29 06:41
```

The `CODE` is the short label for the eye; the `VARIABLE` is the row in the variable
table that raises it, shown whole so you can `get` or `override` it by name (an
alarm's declared `alarm_` prefix is stripped on screen). There is no description
column: the sentence explaining the condition belongs in the panel, where there is
room for the whole of it.

`ON` means active now; `OFF` means it has fired at some point and cleared; blank
means it has never fired. The `ON` badge is highlighted while the alarm is **active
and unacknowledged** - the same condition that puts it on the alarm bar.

Below the table, a panel shows the selected alarm's description, its **Remediation**
note (what to actually do about it) and the time it was **Ack'd**.

| Key | Action |
|---|---|
| `a` | acknowledge the selected alarm |
| `SPACE`, `LEFT` / `RIGHT` | cycle the filter |

The mode line shows the current filter with its neighbours either side, how many rows
are listed and how many are active. The four filters:

| Filter | Shows |
|---|---|
| `recent` (default) | anything active, plus anything that fired inside its own "recent" window |
| `all` | every declared alarm, in fixed table order |
| `active` | active now, acknowledged or not |
| `unmasked` | active and **not** acknowledged - the alarm bar's contents |

Except in `all`, rows sort unacknowledged-active first, then acknowledged-active,
then inactive; within each group, most recent onset first. In `all` the order never
moves, which is what you want when comparing against a printed list.

See @ref user_alarms for what acknowledging does.

## PARAMS

The editable settings table:

```
PARAMETER          TYPE  VALUE            DEFAULT      UNIT
ntp_server1        str   pool.ntp.org     pool.ntp.org
ntp_poll_s         u16   64               64           s
```

The panel below shows the selected parameter's **Minimum**, **Maximum**,
**Description** and **Group**. `SPACE` (or `LEFT` / `RIGHT`) steps through the views:
`all`, then one per parameter group.

| Key | Action |
|---|---|
| `ENTER` | edit the selected parameter |

Editing opens a one-line editor on the bottom row, seeded with the current value.
`ENTER` commits, `Ctrl+C` (or `ESC`) cancels. Standard editing keys apply:
`Ctrl+A`/`Ctrl+E` for line start/end, `Ctrl+U` to clear, `Ctrl+B`/`Ctrl+F` or the
arrows to move, `BACKSPACE` and `Ctrl+D` to delete.

A rejected value appends `[invalid]` and leaves you in the editor. A value that was
accepted but could not be written to storage shows
`NOT SAVED - value is live but storage refused it`: the setting is in effect now,
but will not survive a reboot. See @ref user_settings.

## MONITOR

Every status variable the site publishes:

```
SCOPE  VARIABLE            VALUE            UNIT   OVERRIDE
MI     grid_L1_V           237.4            V
MI     grid_L2_V           236.9            V
G      ats_supply_Hz       50.0             Hz     FORCED
```

`SCOPE` is the declared kind of the row (`MI`/`AI`/`DI` inputs, `MO`/`AO`/`DO`/`RO`
outputs, `G` computed, `SV` status). `OVERRIDE` marks a row someone has forced: a
forced reading is indistinguishable from a real one in the `VALUE` column, which is
the whole hazard of an override, so the table says so per row rather than only in the
panel.

Variables are grouped by the prefix of their name, and `SPACE` (or `LEFT` / `RIGHT`)
steps through the views: `all`, then one per group.

The panel below shows the selected variable's **Description** and **Notes**, its
**Source** (which Modbus register, which local channel, or `computed`) and its
**Quality**:

| Quality | Meaning |
|---|---|
| `live` | a driver is publishing it and it is fresh |
| `no data (showing default)` | nothing has written it, or it timed out - the number shown is the declared default |
| `OVERRIDDEN (forced by hand)` | someone forced a value; see @ref user_variables |
| `OVERRIDDEN (forced invalid)` | someone forced it to read as a dead sensor |

Read-only - this page never writes. Use `set` or `override` from the shell.

## TASKS

What the beat actually costs, one row per registered task:

```
TASK             GROUP  EVERY ms  PHASE    MEAN us  PEAK us
console          slow   100       present      412     2180
expire_stale     bkgnd  500       input         88      143
scan             fast   10        execute       21       96
```

`stats` reports the whole beat's CPU with no attribution; this page is where
"which task blew the budget" has an answer. `MEAN` is what says whether a task is
affordable. `PEAK` is an all-time maximum that never decays, so one outlier pins it
for the rest of the boot - read it as a diagnostic, not as a running cost.

Rows are sorted by name. The beat's own order is phase-major and the `PHASE` column
still carries it, but finding one task by eye beats reading the cycle here.

The panel below shows the selected task's last sample, its run count, and the share
of its own period the mean takes. Read-only.

## NOTES

A free-text note kept on the panel - what was changed, what to check next, what the
last visit found. It is a full-screen editor with the same keys as the shell prompt
(see @ref user_shell), and rows a wrapped line spills onto are marked differently
from lines you started, so a paragraph reads as one thing.

| Key | Action |
|---|---|
| `Ctrl+O` | write the note to flash |
| `UP` / `DOWN` | move the cursor (there is no history here) |

Saving is deliberate rather than continuous: a note is kilobytes, and each write is a
real flash erase. `Ctrl+O` writes, and leaving the page flushes anything unsaved. The
status row says whether there are unsaved changes.

## Table navigation {#user_pages_nav}

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
