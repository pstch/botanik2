# The pages {#user_pages}

Six pages ship in the default tab order. Switch between them with `TAB` or
`Alt+<number>` (see @ref user_console).

| # | Page | For |
|---|---|---|
| 1 | HOME | at-a-glance readings and a live log tail |
| 2 | SHELL | the command prompt |
| 3 | LOGS | the full scrolling log |
| 4 | ALARMS | the alarm table, and acknowledging alarms |
| 5 | PARAMS | reading and editing settings |
| 6 | MONITOR | every status variable, with detail |

Keys shared by the three table pages (ALARMS, PARAMS, MONITOR) are listed once under
[Table navigation](#user_pages_nav). LOGS scrolls differently and has its own list.

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
CODE       ON   LAST EVENT        DESCRIPTION
TOO_HOT    ON   26/07/31 14:02    Zone temperature above limit
WTR_LK     OFF  26/07/29 06:41    Water leak detected
```

`ON` means active now; `OFF` means it has fired at some point and cleared; blank
means it has never fired. The `ON` badge is highlighted while the alarm is **active
and unacknowledged** - the same condition that puts it on the alarm bar.

Below the table, a panel shows the selected alarm's **Remediation** note (what to
actually do about it) and the time it was **Ack'd**.

| Key | Action |
|---|---|
| `a` | acknowledge the selected alarm |
| `SPACE` | cycle the filter |

The footer shows the current filter, how many rows are listed and how many are
active. The four filters:

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
**Description** and **Group**.

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

Every status variable the site publishes, grouped:

```
GROUP        VARIABLE           VALUE            UNIT
power        pwr_L1_V           237.4            V
             pwr_L2_V           236.9            V
ats          ats_supply_Hz      50.0             Hz
```

The panel below shows the selected variable's **Description**, its **Source** (which
Modbus register, which local channel, or `computed`) and its **Quality**:

| Quality | Meaning |
|---|---|
| `live` | a driver is publishing it and it is fresh |
| `no data (showing default)` | nothing has written it, or it timed out - the number shown is the declared default |
| `OVERRIDDEN (forced by hand)` | someone forced a value; see @ref user_variables |
| `OVERRIDDEN (forced invalid)` | someone forced it to read as a dead sensor |

Read-only - this page never writes. Use `set` or `override` from the shell.

## Table navigation {#user_pages_nav}

| Key | Action |
|---|---|
| `UP` / `Ctrl+P` | up one row |
| `DOWN` / `Ctrl+N` | down one row |
| `PAGE_UP` / `PAGE_DOWN` | up / down half a screen |
| `END` | last row |
| `ENTER` | back to the top (ALARMS, MONITOR) |

`^` and `v` marks on the table's top and bottom rules show when rows are hidden
above or below.
