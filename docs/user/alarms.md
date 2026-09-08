# ALARMS: the alarm list {#user_alarms}

The alarms page is the fourth page and it shows a table of alarms, and can be used to monitor alarm state & acknowledge alarms. It can be filtered to show only active alarms, and it supports searching, just by typing text while the page is open.
For each alarm, the table shows :
 - the alarm's short code and name
 - the current status, the acknowledgeent state and alarm overrides
 - the timestamp of the last evente

@verbinclude alarms.txt

## Keyboard shortcuts

| key                  | what it does                                                     |
|----------------------|------------------------------------------------------------------|
| `Up`, `Ctrl+P`       | move the selection up                                            |
| `Down`, `Ctrl+N`     | move the selection down                                          |
| `PageUp`, `PageDown` | a screenful at a time                                            |
| `Home`               | the first row                                                    |
| `End`                | the last row                                                     |
| `Left`, `Right`      | cycle the view shown in the legend                               |
| any text             | filter on it; the legend shows the pattern                       |
| `Backspace`          | edit the filter                                                  |
| `Esc`                | clear the filter                                                 |
| `Alt+W`              | copy the selected cell to the kill ring                          |
| `Enter`              | acknowledge the selected alarm, or take the acknowledgement back |
| `Ctrl+O`             | force the alarm on or off by hand                                |
| `Ctrl+R`             | clear the force                                                  |
| `Ctrl+S`             | save the force, so it survives a reboot                          |

`Left` and `Right` cycle the view between the unmasked alarms, the recent ones
and all of them.

## Alarm mechanics

Each alarm carries four times, and they do two different jobs. Two decide what
is **said**, one decides what is **true**, and one decides how long it is
**remembered**.

| time       | what it governs                                                                                           |
|------------|-----------------------------------------------------------------------------------------------------------|
| `on_s`     | how long the condition must hold before the START is announced. -1 never announces that edge              |
| `off_s`    | how long it must have cleared before the END is announced. -1 never announces that edge                   |
| `hold_s`   | how long the alarm stays ACTIVE past the last moment the condition was true, re-armed every beat it is up |
| `recent_s` | how long a cleared alarm stays in the RECENT view. 0 is the configured default, negative is forever       |

`on_s` and `off_s` gate the log line and the notifier only. Neither touches
whether the alarm is active, so a dwell cannot hide a live condition from the
alarm bar.

`hold_s` is the opposite: it is the anti-flicker. It bridges a chattering
signal into one episode, and it keeps a pulse too short to see on screen there
long enough to be seen. With `hold_s` at 0 the alarm follows the raw condition
exactly.

An alarm that comes and goes faster than its `on_s` never announces a START at
all; it is logged as a FLAP instead, so the churn is visible without filling
the log with pairs.

Acknowledging goes with the **episode**, not the raw condition. An alarm held up
by `hold_s` has not stopped, so acknowledging it stays acknowledged for as long
as the episode lasts; a new episode raises it unacknowledged again.

## Library alarms

These come from the library and are present on every unit, whatever the site
declares of its own. The site's are in @ref gh_mlb41_alarms.

| code        | row                   | what it means                                                              |
|-------------|-----------------------|----------------------------------------------------------------------------|
| `NET_FLT`   | `alarm_net_fault`     | the network link is down                                                   |
| `NTP_TIM`   | `alarm_ntp_timeout`   | no NTP sync: anything dated, scheduled or solar is adrift                  |
| `LOG_FLD`   | `alarm_log_flood`     | log messages are being dropped                                             |
| `SYS_RST`   | `alarm_system_reset`  | the unit reset recently                                                    |
| `CPU_XRUN`  | `alarm_cpu_xrun`      | the scheduler skipped a beat                                               |
| `CPU_LOAD`  | `alarm_cpu_load`      | CPU load over its threshold                                                |
| `CPU_JITT`  | `alarm_cpu_jitter`    | scheduling jitter over its threshold                                       |
| `STACK`     | `alarm_stack`         | stack usage over its threshold                                             |
| `FLASH_FLT` | `alarm_flash_error`   | a write to persistent storage failed: parameters will not survive a reboot |
| `BUS_DEAD`  | `alarm_fieldbus`      | a field device is not answering                                            |
| `BUS_LINE`  | `alarm_fieldbus_line` | the whole RS-485 segment has gone quiet                                    |
| `SCAN_LATE` | `alarm_scan_late`     | a control program is missing its period                                    |
| `IO_DROP`   | `alarm_io_drop`       | a field row is never scanned: the planner could not place it               |
