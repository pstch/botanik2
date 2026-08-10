# Configuration and features {#int_config}

Two headers hold everything set at compile time: `libbotanik_config.h` for tunable
values, `libbotanik_features.h` for whole subsystems.

## How an override works

Every value in `libbotanik_config.h` follows one shape:

```cpp
#ifndef BOTANIK_TASK_PERIOD_MS
#  define BOTANIK_TASK_PERIOD_MS 10
#endif
BOTANIK_CFG(uint32_t, TASK_PERIOD_MS)   // -> Botanik::Config::TASK_PERIOD_MS
```

So a site pins a value by `#define`-ing the `BOTANIK_`-prefixed macro in its
`config.h`, which CMake includes ahead of the library defaults. Code reads the
`Botanik::Config::` constant, never the macro.

@warning The override is defined `PUBLIC` on the `botanik` target so the whole build
agrees. **Any target that includes `libbotanik_config.h` must link `botanik`** -
anything narrower is an ABI mismatch, silently compiled against different values.

## Feature flags

Same mechanism, plus `-DBOTANIK_FEATURE_<X>=ON/OFF` on the CMake command line.

Precedence is **command line, then site `config.h`, then the header default**. A `-D`
is already defined by the time the site config is read, so a site pins a flag with an
`#ifndef` guard - which states what the installation needs without overruling someone
deliberately building a cut-down image:

```cpp
#ifndef BOTANIK_FEATURE_NETWORK      // gh_mlb41/config.h
#  define BOTANIK_FEATURE_NETWORK 1
#endif
```

@warning An **unguarded** `#define` in a site config inverts that: it redefines the
command line's value, wins, and says so only through a "redefined" warning buried in
the build log. Guard every feature flag a site pins.

| Flag | Default | Requires | Gates |
|---|---|---|---|
| `CONSOLE` | **on** | - | the interactive ANSI UI: the terminal, the console UART, and the `register_pages()` / `register_commands()` hooks. Off = headless |
| `LOGGING` | **on** | - | the log writers, the rate limiter and the queue. Off leaves `Logs::Logger` as an inert shell, so nothing that holds a `Logger*` changes and every log call becomes a no-op before it formats |
| `NETWORK` | off | - | the network stack: device bring-up and the DNS resolver |
| `NTP` | off | `NETWORK` | the NTP client |
| `MQTT` | off | `NETWORK` | the MQTT client |
| `SUN_TRACKER` | off | - | the solar-position tracker. Standalone - `BotanikFirmware` does not instantiate it, so a site that wants it owns the instance |
| `HOME_PAGE` | = `CONSOLE` | `CONSOLE` | the HOME page and its log-tail sink |
| `SHELL_PAGE` | = `CONSOLE` | `CONSOLE` | the SHELL page **and the whole command interpreter** - `ShellPage` owns `Shell::CommandShell`, so this also gates `register_commands()` |
| `LOGS_PAGE` | off | `CONSOLE`, `LOGGING` | the LOGS page and its sink |
| `ALARMS_PAGE` | off | `CONSOLE` | the ALARMS page. The alarm *controller* is unconditional - the bottom alarm bar reads it either way |
| `PARAMS_PAGE` | off | `CONSOLE` | the PARAMS page |
| `MONITOR_PAGE` | off | `CONSOLE` | the MONITOR page. **Not** `LOGS_MONITOR`, which is a serial log sink |
| `TASKS_PAGE` | off | `CONSOLE` | the TASKS page |
| `NOTES_PAGE` | off | `CONSOLE` | the NOTES page. Costs `NOTES_LEN` bytes plus 2 x `NOTES_MAX_ROWS` row slots of RAM |
| `LOGS_PUBLISH` | off | `LOGGING`, `MQTT` | `MqttLogWriter` - log entries to the broker |
| `ALARMS_PUBLISH` | off | `LOGGING`, `MQTT` | `MqttAlarmNotifier` - alarm transitions to the broker |
| `LOGS_MONITOR` | off | `LOGGING` | the secondary MONITOR UART and its log sink |
| `PLC_SCAN` | **on** | - | `Scan::Driver`. Off does *not* remove control authority: the Bank's enable/disable latch has consumers of its own |
| `PLC_BLK_BASE` | off | - | the base blocks: timers, logic, analog, meter, pid, actuator |
| `PLC_BLK_FILTERS` | off | - | the filter blocks: `Ewma`, `MedianOf3`, `MovingAverage`. Distinct from `utils/filters.h`, the scheduler's metric filter |
| `MODBUS_TCP_SERVER` | off | `NETWORK`, `PLC_SCAN` | the Modbus TCP slave: the framing, the register map over the variable table, and the server. Stepped every beat, and it splices four `mb_` parameters into the library's rows. On for `gh_mlb41` |
| `MODBUS_TCP_CLIENT` | off | `NETWORK` | a Modbus TCP **master**: this unit polling another device. The simulator points one at our own slave as a self-check - a real socket, the real map, the real `Bank::set()` - so the server is exercised end to end every run |
| `MODBUS_RTU_CLIENT` | off | `PLC_SCAN` | the RS-485 field bus master. Plans the site's `XP_MODBUS` rows into blocks, honours each device's ceiling and alignment, and scans them at their declared rates. Stepped in `PHASE_INPUT`/`PHASE_OUTPUT`. On for `gh_mlb41` |
| `MODBUS_RTU_SERVER` | off | - | `RtuSlave`, the device model that answers: registers, coils, exceptions and CRC resync. It is what the simulator's `FieldBus` builds the far end of the wire from, one per unit the site addresses. On for `gh_mlb41` |

(Each is spelled `BOTANIK_FEATURE_<NAME>`.)

The defaults are a **minimal core**: a build that sets nothing gets a console
with a home screen and a shell, logging, and the scan cycle. Everything else is
opt-in. `src/sites/gh_mlb41/config.h` is the worked full profile.

Dependency rules are enforced by `#error`, so a contradiction fails the build
rather than quietly producing a binary missing what was asked for.

A site's opt-in must yield to the command line. `#ifndef` alone does not achieve
that: with `-DBOTANIK_FEATURE_CONSOLE=OFF` the page flags are still undefined
when the site config is read, so a bare `#define ..._PAGE 1` there would turn the
builder's choice into a dependency error the builder never wrote. Guard the block
instead:

```c
#if !defined(BOTANIK_FEATURE_CONSOLE) || BOTANIK_FEATURE_CONSOLE
#  ifndef BOTANIK_FEATURE_TASKS_PAGE
#    define BOTANIK_FEATURE_TASKS_PAGE 1
#  endif
#endif
```

Adding a flag is **two** edits: an `X()` entry plus an `#ifndef` default in
`src/libbotanik_features.h`, and a row above. CMake discovers `-D`s by prefix and
the `features` command expands `BOTANIK_FEATURE_LIST`, so neither can fall out of
step.

A built unit reports its own set through the shell's `features` command
(@ref user_shell), which is how you tell a compiled-out subsystem from a broken one
without going back to the build that produced the binary.

@note Keep the `foreach(_feat ...)` list in `CMakeLists.txt` in step with this table.
A flag present in the header but missing from that list still has its default, but
`-DBOTANIK_FEATURE_<X>=...` on the command line is **silently ignored** for it.

Gate code with the preprocessor, not `constexpr` - the flag must be able to drop
member declarations and constructor-init entries, and C++11 has no `if constexpr`:

```cpp
#if BOTANIK_FEATURE_NETWORK
    NetworkStack _net;
#endif
```

The reference site turns `NETWORK`, `NTP` and `MQTT` on in its own `config.h`.

## What belongs here

A value earns a place in `libbotanik_config.h` when an **installation** varies it:
a baud rate, a threshold, the size of a table a site fills. A number that the code,
a data type or a protocol already fixes does not - that is a `static constexpr`
beside its one consumer, where the thing constraining it can be read in the same
breath. The NTP port (`network/ntp.h`), the storage key length
(`HAL::Storage::KEY_MAX`), the log continuation indents (`logs/format.h`) and the
simulator's stack-painting constants (`scheduler/scheduler.cpp`) all live that way.

The sections below follow the header's own order.

## Config reference

### Project identity

| Macro | Default |
|---|---|
| `FIRMWARE_VERSION` | `"0.13.12-dev"` (keep `Doxyfile`'s `PROJECT_NUMBER` in step) |
| `SITE_NAME` | `"generic"` |
| `MQTT_TOPIC_PREFIX` | `"default/" SITE_NAME` |

### Serial interfaces

| Macro | Default | |
|---|---|---|
| `CONSOLE_BAUD` | 4800 | drives the UART **and** the terminal's per-render TX budget, which `AnsiTerminal` derives from it at construction. Compile-time only: a runtime setter could move one and not the other, so there isn't one |
| `MONITOR_BAUD` | 115200 | effective on the simulator. The OPTA's MONITOR endpoint is USB CDC and has no line rate |
| `SERIAL_RX_BUF` / `SERIAL_TX_BUF` | 256 / 4096 | the two rings, per port. Powers of two (`static_assert`ed); usable depth is size-1 |
| `SERIAL_TX_PER_STEP` | 256 | bytes one `transmit()` may push, so a fast port (USB CDC) cannot eat a whole beat |

### Scheduling

| Macro | Default | |
|---|---|---|
| `TASK_PERIOD_MS` | 10 | the beat. Must be >= 2 |
| `GROUP_FAST_MS` | = `TASK_PERIOD_MS` | the four task rate groups. A task's group is its rate; its phase is its place in the data flow (@ref dev_architecture) |
| `GROUP_MAIN_MS` | 50 | **currently has no members** - the console render it was named for moved to `GROUP_SLOW`. Check `tasks.h` before tuning it |
| `GROUP_SLOW_MS` | 100 | dwell, config, and the console render. `TX_COLD_TARGET_MS` must stay above it |
| `GROUP_BACKGROUND_MS` | 500 | |
| `MAX_TASKS` | 24 | registry capacity, base tasks plus the site's |
| `SCAN_STAGGER_MS` | = `TASK_PERIOD_MS` | spreads the scan's control programs over beats by table index, so they do not pile onto one |
| `HW_WATCHDOG_MS` | 10000 | hardware watchdog timeout, clamped to what the IWDG can express |

### Performance metrics

| Macro | Default | |
|---|---|---|
| `LOAD_METRIC_WINDOW_MS` | 5000 | metric window. The scheduler divides it by the beat to get the sample count |
| `LOAD_METRIC_BUCKETS` | 10 | windowed-max buckets |
| `CPU_LOAD_EMA_ATTACK_MS` / `_DECAY_MS` | 0 / 2000 | asymmetric smoothing: rise instantly, fall slowly, so a spike cannot be smoothed away before the threshold sees it. `ema_alpha()` treats any constant at or below the beat as instant; above it, a real time constant |
| `CPU_JITTER_EMA_ATTACK_MS` / `_DECAY_MS` | 0 / 5000 | |

### Health and alarms

The three thresholds are **runtime parameters** (`sys_cpu_load_pct`,
`sys_jitter_pct`, `sys_stack_pct`), read live each beat: what counts as too much
load is a property of the installation, and the number worth moving is the one a
unit is nuisance-alarming on. The macros are their factory values.

| Macro | Default | |
|---|---|---|
| `ALARM_CPU_LOAD_PCT` | 50 | factory default for `sys_cpu_load_pct`; the `CPU_LOAD` threshold |
| `ALARM_CPU_JITTER_PCT` | 50 | factory default for `sys_jitter_pct`. Equal to the load one today, but they threshold different quantities, so they stay two knobs |
| `ALARM_STACK_PCT` | 80 | factory default for `sys_stack_pct`; the `STACK` threshold |
| `ALARM_EVENT_HOLD_MS` | 300000 | how long an event-style alarm stays active |
| `HEALTH_RESET_WINDOW_MS` | 300000 | how long after boot `SYS_RST` stays asserted |
| `ALARM_NOTIFIERS_MAX` | 4 | |
| `ALARM_RECENT_S` | 604800 | RECENT window a cleared alarm stays listed for (a row's own `recent_s` wins) |
| `ALARM_SLOTS` | 64 | `SCOPE_A` rows the model has room for - the site's and the library's together, since they share one table. See @ref int_tables |

### Site table capacity

| Macro | Default | |
|---|---|---|
| `HOME_FIELDS_MAX` | 22 | home fields (3 columns x 8, minus the uptime cells) |
| `VAR_GROUPS_MAX` | 24 | distinct variable groups a site may have. A group is a name prefix, so this bounds how many domains one table spans; past it `var_group()` answers `?`. The pool covers the WHOLE table, so the library's eight config groups (`serial`, `net`, `ntp`, `mqtt`, `geo`, `mb`, `term`, `sys`) count against it - `gh_mlb41` reaches 15 |
| `VAR_GROUP_NAME_LEN` | 16 | bytes one interned group name may take, NUL included |
| `RETAIN_SAVE_MS` | 300000 | factory default for `sys_retain_sec` (in SECONDS there): how often the `F_RETAIN` rows are written to flash. A wear-versus-loss dial. 0 leaves only the save on the way down and the `save` command |

### Logging

| Macro | Default | |
|---|---|---|
| `LOG_MIN_LEVEL` | 2 (info) | factory default for `sys_log_level`, which IS the master floor. Moving it is a parameter edit, so it persists. SIMULATOR builds force this to 1 (debug) from `CMakeLists.txt`, so the sim never shows this default |
| `LOG_MESSAGE_LEN` | 256 | the raw message, before formatting |
| `LOG_FORMATTED_LEN` | 320 | the formatted line. A `static_assert` keeps it at least `LOG_MESSAGE_LEN + 64`, so a full-length message does not lose its tail to the timestamp/level/topic prefix |
| `LOG_WRITERS_MAX` | 8 | |
| `LOG_QUEUE_SIZE` | 32 | per writer, and the dominant RAM line in a logging build: roughly `LOG_QUEUE_SIZE * (LOG_MESSAGE_LEN + 16)` per writer, times the five the full page set registers. Keep it above the ~20-entry boot burst. `static_assert`ed <= 255 (the ring index is `uint8_t`) |
| `LOG_QUEUE_CRITICAL_RESERVE` | 4 | |
| `LOG_GLOBAL_BUCKET_BURST` / `_PERIOD_MS` | 32 / 1000 | sized to absorb the boot burst |
| `LOG_PERTOPIC_BUCKET_BURST` / `_PERIOD_MS` | 20 / 5000 | |
| `LOG_SUPPRESS_REPORT_PERIOD_MS` | 10000 | |
| `LOG_SINK_DROP_REPORT_PERIOD_MS` | 5000 | |
| `LOG_TX_DROP_REPORT_PERIOD_MS` | 10000 | |

### Network transport

| Macro | Default | |
|---|---|---|
| `HOST_STR_LEN` | 64 | hostname buffer, matching the site's `STR_CAP` for `mqtt_host` / `ntp_srv*` |
| `NET_MAX_UDP` / `NET_MAX_TCP` | 2 / 2 | HAL socket tables. NTP takes one UDP and MQTT one TCP, so a further client needs these raised |
| `NET_SETUP_TIMEOUT_MS` | 30000 | |
| `NET_RETRY_MS` | 10000 | |

### Name resolution

| Macro | Default | |
|---|---|---|
| `DNS_CACHE_SIZE` | 8 | |
| `NET_MAX_DNS` | = `DNS_CACHE_SIZE` | async lookups the HAL can have in flight. Separate because the two sit on opposite sides of the HAL boundary; `hal/net.cpp` `static_assert`s it is not below `DNS_CACHE_SIZE` |
| `DNS_CACHE_TTL_MS` | 3600000 | 0 disables expiry |
| `DNS_PENDING_MAX_MS` | 30000 | caps an abandoned lookup, so a slot cannot leak |

### Time sync (NTP)

Only the client's own timings are compile-time. Which servers, whether to sync, how
often and on which port are runtime parameters (`ntp_srv1`, `ntp_enable`,
`ntp_poll_sec`, `ntp_port`), and **a setting that is a parameter has no config
default here** - the parameter's own declared default is the default, and its band
keeps it valid, so a firmware-side fallback would just be a second default free to
drift from the one the operator sees.

| Macro | Default | |
|---|---|---|
| `NTP_QUERY_TIMEOUT_MS` | 5000 | |
| `NTP_RETRY_MS` | 60000 | |

### MQTT client

Broker host, port and keepalive are runtime parameters (`mqtt_host`, `mqtt_port`,
`mqtt_ka_sec`) and so, per the rule above, have no config default here.

| Macro | Default | |
|---|---|---|
| `MQTT_CONNECT_TIMEOUT_MS` | 10000 | |
| `MQTT_RETRY_MS` | 30000 | |
| `MQTT_TX_BUF` | 384 | bounds the whole PUBLISH; an oversized one is refused outright. `static_assert`ed to clear `MQTT_TOPIC_LEN + LOG_MESSAGE_LEN + 32`, since the largest payload is a log line, not a reading |
| `MQTT_TOPIC_LEN` | 96 | topic buffer shared by all three publishers, so a long `MQTT_TOPIC_PREFIX` truncates the same way everywhere |
| `MQTT_PAYLOAD_LEN` | 64 | caps a published telemetry value's formatted width |
| `MQTT_RX_PER_STEP` | 8 | socket reads drained per activation, the RX-side twin of `MQTT_TELEMETRY_PER_STEP` |
| `MQTT_TELEMETRY_PERIOD_MS` | 30000 | factory default for `mqtt_tlm_sec` (in SECONDS there); 0 disables telemetry |
| `MQTT_TELEMETRY_PER_STEP` | 8 | fields published per step, so one cycle cannot eat a beat |
| `MQTT_TELEMETRY_RETAIN` | 1 | factory default for `mqtt_retain`. @warning an invalid reading is skipped rather than published, and there is no last will, so the broker keeps serving the last good value of a dead sensor. Turn it off where "current" must be distinguishable from "last seen" |
| `MQTT_TELEMETRY_TOPIC` / `MQTT_LOG_TOPIC` / `MQTT_ALARM_TOPIC` | `"status"` / `"logs"` / `"alarms"` | the path schema under `MQTT_TOPIC_PREFIX`. Changing one moves every subscriber |
| `MQTT_LOG_MIN_LEVEL` | 1 (debug) | the MQTT sink's own threshold; DEBUG, so the broker carries everything the logger does |

### Sun position tracking

| Macro | Default | |
|---|---|---|
| `SUN_RECOMPUTE_MS` | 60000 | elevation drifts ~0.25 deg/min, so a minute stays under a degree stale |

### Terminal and console

Which terminal is on the wire and how big it is are **runtime parameters**
(`term_type`, `term_cols`, `term_rows`), so swapping a VT102 for a Minitel, or
an 80x24 for a 132x32, needs no firmware build. The three below are their
factory values: what the console is CONSTRUCTED at, since the parameter store
cannot be read before the port is open, and what a wiped store comes back to.

They are staged behind `term_serial`, unlike `serial_console_baud`: the three
describe ONE physical terminal, and applying them one edit at a time walks the
screen through geometries that match nothing on the wire.

@warning A geometry the terminal does not actually have is a garbled screen that
**survives a reboot** - there is no resize handshake to discover the truth and
correct it. The rows' declared bands (`EDITOR_MIN_COLS`..`TERMINAL_MAX_COLS`,
12..`TERMINAL_MAX_ROWS`) are the whole guard. Same class of footgun as
`serial_console_baud`, which can lock the console out entirely.

| Macro | Default | |
|---|---|---|
| `TERMINAL_TYPE` | 1 | factory default for `term_type`. 0 = VT100, 1 = VT102, 2 = Minitel 1B, 3 = Minitel 2. A plain integer, because this header is included everywhere and must not depend on the console |
| `CONSOLE_COLS` / `CONSOLE_ROWS` | 80 / 24 | factory defaults for `term_cols` / `term_rows` |
| `TERMINAL_MAX_COLS` / `_ROWS` | 132 / 32 | largest terminal supported. They size the screen back-buffer (two cell grids, so 2 x their product in RAM) and clamp both the construction geometry and anything the `term_` rows ask for |
| `CONSOLE_PAGES_MAX` | 10 | pages the Console multiplexes, at the Alt+`<digit>` ceiling. The full page set is 8, and registration past the cap is refused **silently**, so the headroom is what lets a site splice its own page in |

### Terminal input decoding

| Macro | Default | |
|---|---|---|
| `ANSI_INPUT_BATCH_SIZE` | 32 | |
| `ANSI_INPUT_BATCH_TIMEOUT_MS` | 1000 | |
| `ANSI_CMD_TIMEOUT_MS` | 100 | how long a lone `ESC` waits for a sequence |
| `ANSI_CMD_FLUSH_TIMEOUT_MS` | 250 | |
| `INPUT_EVENTS_PER_TICK` | 4 | |

### Page refresh cadences

| Macro | Default | |
|---|---|---|
| `PAGE_HOT_MS` | 1000 | a page's hot fields, and the status bar's clock and metrics |
| `PAGE_COLD_MS` | 5000 | a metric page's cold fields |
| `PAGE_TASKS_MS` | 2000 | the TASKS page's cost columns: smoothed aggregates, not a live trace |
| `TABLE_LINE_LEN` | 200 | buffer one rendered table row is assembled in, shared by every scrolling table |

### TX pacing

| Macro | Default | |
|---|---|---|
| `TX_COLD_TARGET_MS` | 128 | the depth floor, in ms of wire time. Must exceed `GROUP_SLOW_MS` (`static_assert`ed) since the render runs on that group; the margin is thin at the defaults and nothing measures an underrun, so raise rather than lower |
| `TX_HEADROOM_BYTES` | 256 | ring space the depth floor always leaves free, so one oversized run is not deferred forever |
| `LOG_ROWS_PER_TICK` | 2 | |

### Shell and scrollback

| Macro | Default | |
|---|---|---|
| `SHELL_INPUT_LEN` | 512 | |
| `SHELL_HISTORY_SIZE` / `_ENTRY` | 8 / 128 | the entry cap is below `SHELL_INPUT_LEN`, so a longer command is stored truncated and recalling it yields a different command |
| `SHELL_COMMANDS_MAX` | 64 | |
| `SHELL_SCROLLBACK_LINES` | 32 | |
| `LOGS_SCROLLBACK_LINES` | 64 | |
| `HOME_SCROLLBACK_LINES` | 6 | the home page's log tail |
| `SCROLLBACK_LINE_LEN` | 256 | what a page **stores**, times the three line counts above. Distinct from the log-message sizes, which are what the logger formats |

### Text editor

| Macro | Default | |
|---|---|---|
| `EDITOR_MAX_ROWS` | 32 | |
| `EDITOR_PROMPT_MAX` | 16 | |
| `KILL_RING_SIZE` | 256 | one clipboard for the whole console, not one per editor. A cut past it takes what fits and leaves the rest, so a paste restores exactly what went |
| `EDITOR_MIN_COLS` | 32 | read only by the two layout `static_assert`s, which is why it sits beside the lengths it relates |

`EDITOR_MAX_ROWS * (EDITOR_MIN_COLS - EDITOR_PROMPT_MAX)` must be at least
`SHELL_INPUT_LEN`; a `static_assert` enforces it. It is exact at the defaults, and
does not model `TextEditor`'s `_indent` - safe today only because the one indented
editor is also single-line.

### Operator notes

| Macro | Default | |
|---|---|---|
| `NOTES_LEN` | 2048 | the note's buffer, in bytes |
| `NOTES_MAX_ROWS` | 128 | row slots the editor lays the note out across |

The same layout `static_assert` applies. The editor's buffers are supplied by their
owner, so these size the NOTES page alone and are independent of the `EDITOR_*`
values the shell prompt uses. The storage key is `NotesPage::STORAGE_KEY`.

### Field bus (Modbus RTU)

The line, and what the master does when a device stops answering. The bus
arithmetic these sit on is worked out in `docs/design/modbus-budget.md`; what
happens under fault is `docs/design/modbus-resilience.md`.

| Macro | Default | |
|---|---|---|
| `MODBUS_BAUD` | 19200 | carries the reference plant at about a fifth of the bus and survives long cable runs. 38400 is the first step up |
| `MODBUS_TURNAROUND_MS` | 10 | what the PLANNER costs a transaction with, not a timeout. It dominates everything and the link speed does not touch it, so measure it rather than trusting it |
| `MODBUS_TIMEOUT_MS` | 300 | how long a slave has to answer. A device slower than this is indistinguishable from a dead one unless its model says otherwise (`DeviceModel::timeout_ms`) |
| `MODBUS_RETRIES` | 1 | misses before a device is backed off |
| `MODBUS_RETRY_DELAY_MS` | 500 | caps how long the retry waits. Without it a read's retry comes round on its own poll period, so declaring a device dead cost two of them: 2 s on the 1 s rung but two **minutes** on the 60 s one |
| `MODBUS_BACKOFF_MS` | 1000 | first probe interval after a device is declared down, doubling |
| `MODBUS_BACKOFF_MAX_MS` | 60000 | the ceiling on that. Affordable only because a probe can no longer pre-empt live traffic |
| `MODBUS_PROBE_SPACING_MS` | 1500 | live traffic owed between two unanswered transactions. With the timeout it fixes the share of the bus devices that are not answering can take, at `timeout/(timeout + spacing)` = 17 %, **independent of how many are down**. Measured in time and not in transactions: a probe costs a full timeout and a live two-register read costs 22 ms, so "one per four transactions" would be 77 % of the bus |
| `MODBUS_WRITE_MIN_MS` | 200 | shortest interval between two writes to one block, so a chattering demand cannot monopolise the bus |
| `MODBUS_WRITE_DEADLINE_MS` | 500 | how long a demand that has moved may sit behind reads. A **denominator**, not a throttle: every block is ranked on how far it is through the lateness it is allowed, and this is a write's share of that, so shorter means higher priority against the read rungs and not a faster retry. The one value here with a plant consequence rather than a bus one |
| `MODBUS_REWRITE_MS` | 5000 | unconditional re-drive: it re-drives a slave that rebooted, and is the keepalive that slave's own failsafe timer watches for. **Configure that failsafe** - it is the only mechanism that still works when the master is the thing that failed |
| `MODBUS_REFUSAL_MAX_MS` | 60000 | ceiling on a refused write block's own backoff. Per block and not per device: an exception says the register map is wrong, not that the box is gone, so its reads keep their rates |
| `MODBUS_GAP_MS` | 3 | inter-frame silence, raised per device by `DeviceModel::min_gap_ms` |

### Simulator only

`STORE_MAX_BYTES` 16384, the cap on the key-value store file, guarded by
`TARGET_SIMULATOR`. The host-side knobs are environment variables, not build
settings: `BOTANIK_SIM_CPU`, `BOTANIK_SIM_CLOCK` (see `tests/pyte/README.md`).

@note These per-parameter `#ifndef` guards cannot be collapsed into a helper macro:
the C++11 preprocessor has no way to conditionally define a macro whose name it
computes. The repetition is irreducible.
