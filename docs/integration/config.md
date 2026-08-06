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
| `MODBUS_TCP_SERVER` | off | `NETWORK`, `PLC_SCAN` | *reserved* - no implementation yet |
| `MODBUS_RTU_CLIENT` | off | `PLC_SCAN` | *reserved* - no implementation yet |

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

## Config reference

### Identity

| Macro | Default |
|---|---|
| `FIRMWARE_VERSION` | `"0.13.12-dev"` |
| `SITE_NAME` | `"generic"` |
| `MQTT_TOPIC_PREFIX` | `"default/" SITE_NAME` |

### Interfaces

| Macro | Default | |
|---|---|---|
| `CONSOLE_BAUD` | 4800 | also sizes the terminal's per-render TX budget |
| `MONITOR_BAUD` | 115200 | |
| `SERIAL_TX_PER_STEP` | 256 | bytes one `transmit()` may push, so a fast port (USB CDC) cannot eat a whole beat |
| `SERIAL_RX_BUF` / `SERIAL_TX_BUF` | 256 / 4096 | the two rings, per port. Powers of two (`static_assert`ed); usable depth is size-1 |
| `CONSOLE_COLS` / `CONSOLE_ROWS` | 80 / 24 | geometry of the terminal on the wire. Fixed at construction, bounded by `TERMINAL_MAX_COLS` / `_ROWS` |

### Scheduling and health thresholds

| Macro | Default | |
|---|---|---|
| `TASK_PERIOD_MS` | 10 | the beat. Must be >= 2 |
| `GROUP_FAST_MS` | = `TASK_PERIOD_MS` | the four task rate groups. A task's group is its rate; its phase is its place in the data flow (@ref dev_architecture) |
| `GROUP_MAIN_MS` | 50 | |
| `GROUP_SLOW_MS` | 100 | |
| `GROUP_BACKGROUND_MS` | 500 | |
| `MAX_TASKS` | 24 | registry capacity, base tasks plus the site's |
| `SCAN_STAGGER_MS` | = `TASK_PERIOD_MS` | spreads the scan's control programs over beats by table index, so they do not pile onto one |
| `HW_WATCHDOG_MS` | 10000 | hardware watchdog timeout |
| `LOAD_METRIC_WINDOW_MS` | 5000 | metric window (`LOAD_METRIC_WINDOW` derives the sample count from it and the beat) |
| `LOAD_METRIC_BUCKETS` | 10 | windowed-max buckets |
| `ALARM_CPU_LOAD_PCT` | 50 | `CPU_LOAD` alarm threshold |
| `ALARM_CPU_JITTER_PCT` | 50 | `CPU_JITT` alarm threshold |
| `ALARM_STACK_PCT` | 80 | `STACK` alarm threshold |
| `ALARM_EVENT_HOLD_MS` | 300000 | how long an event-style alarm stays active |
| `ALARM_NOTIFIERS_MAX` | 4 | |
| `ALARM_RECENT_S` | 604800 | RECENT window a cleared alarm stays listed for (a row's own `recent_s` wins) |
| `HEALTH_RESET_WINDOW_MS` | 300000 | how long after boot `SYS_RST` stays asserted |
| `CPU_LOAD_EMA_ATTACK_MS` / `_DECAY_MS` | 0 / 2000 | asymmetric smoothing: rise instantly, fall slowly |
| `CPU_JITTER_EMA_ATTACK_MS` / `_DECAY_MS` | 0 / 5000 | |

### Site capacity

`HOME_FIELDS_MAX` 22, `ALARM_SLOTS` 64. See @ref int_tables.

| Macro | Default | |
|---|---|---|
| `VAR_GROUPS_MAX` | 16 | distinct variable groups a site may have. A group is a name prefix, so this bounds how many domains one table spans; past it `var_group()` answers `?` |
| `VAR_GROUP_NAME_LEN` | 16 | bytes one interned group name may take, NUL included |

### Networking

| Macro | Default | |
|---|---|---|
| `HOST_STR_LEN` | 64 | hostname buffer |
| `DNS_CACHE_SIZE` | 8 | |
| `DNS_CACHE_TTL_MS` | 3600000 | 0 disables expiry |
| `DNS_PENDING_MAX_MS` | 30000 | caps an abandoned lookup, so a slot cannot leak |
| `NET_MAX_UDP` / `NET_MAX_TCP` | 2 / 2 | HAL socket tables. NTP takes one UDP and MQTT one TCP, so a further client needs these raised |
| `NET_MAX_DNS` | = `DNS_CACHE_SIZE` | async lookups the HAL can have in flight. `hal/net.cpp` `static_assert`s it is not below `DNS_CACHE_SIZE`, which is how many rows the resolver can leave pending |
| `NET_SETUP_TIMEOUT_MS` | 30000 | |
| `NET_RETRY_MS` | 10000 | |
| `NTP_QUERY_TIMEOUT_MS` | 5000 | |
| `NTP_RETRY_MS` | 60000 | |
| `NTP_POLL_DEFAULT_S` | 3600 | |
| `NTP_PORT` | 123 | server UDP port, for a time source reached through a proxy |
| `MQTT_PORT_DEFAULT` | 1883 | |
| `MQTT_KEEPALIVE_DEFAULT_S` | 60 | |
| `MQTT_CONNECT_TIMEOUT_MS` | 10000 | |
| `MQTT_RETRY_MS` | 30000 | |
| `MQTT_TX_BUF` | 256 | bounds topic + payload of one publish |
| `MQTT_TOPIC_LEN` | 96 | topic buffer shared by all three publishers, so a long `MQTT_TOPIC_PREFIX` truncates the same way everywhere |
| `MQTT_PAYLOAD_LEN` | 64 | caps a published telemetry value's formatted width |
| `MQTT_RX_PER_STEP` | 8 | socket reads drained per activation, the RX-side twin of `MQTT_TELEMETRY_PER_STEP` |
| `MQTT_TELEMETRY_PERIOD_MS` | 30000 | 0 disables telemetry |
| `MQTT_TELEMETRY_PER_STEP` | 8 | fields published per step, so one cycle cannot eat a beat |
| `MQTT_TELEMETRY_TOPIC` | `"status"` | |
| `MQTT_TELEMETRY_RETAIN` | 1 | |
| `MQTT_LOG_TOPIC` | `"logs"` | |
| `MQTT_LOG_MIN_LEVEL` | 4 (notice) | the MQTT sink's own threshold |
| `MQTT_ALARM_TOPIC` | `"alarms"` | segment alarm transitions publish under |
| `SUN_RECOMPUTE_MS` | 60000 | |

### Logging

| Macro | Default | |
|---|---|---|
| `LOG_MIN_LEVEL` | 2 (info) | the initial `loglevel`. SIMULATOR builds force it to 1 (debug) from `CMakeLists.txt`, so the sim never shows this default |
| `LOG_MESSAGE_LEN` | 256 | |
| `LOG_FORMATTED_LEN` | 256 | |
| `LOG_GLOBAL_BUCKET_BURST` / `_PERIOD_MS` | 32 / 1000 | sized to absorb the boot burst |
| `LOG_PERTOPIC_BUCKET_BURST` / `_PERIOD_MS` | 20 / 5000 | |
| `LOG_SUPPRESS_REPORT_PERIOD_MS` | 10000 | |
| `LOG_SINK_DROP_REPORT_PERIOD_MS` | 5000 | |
| `LOG_TX_DROP_REPORT_PERIOD_MS` | 10000 | |
| `LOG_TOPIC_MAX` | 255 | must stay below 256 |
| `LOG_WRITERS_MAX` | 8 | |
| `LOG_QUEUE_SIZE` | 32 | per writer |
| `LOG_QUEUE_CRITICAL_RESERVE` | 4 | |

### Console and pacing

| Macro | Default | |
|---|---|---|
| `TERMINAL_TYPE` | 1 | 0 = VT100, 1 = VT102, 2 = Minitel 1B, 3 = Minitel 2. A plain integer, because this header is included everywhere and must not depend on the console |
| `TERMINAL_MAX_COLS` / `_ROWS` | 132 / 32 | largest terminal supported. They size the screen back-buffer (two cell grids, so 2 x their product in RAM) and clamp `CONSOLE_COLS` / `_ROWS` |
| `CONSOLE_PAGES_MAX` | 8 | pages the Console multiplexes. At most 10, since a page is also reachable by Alt+`<digit>` |
| `TABLE_LINE_LEN` | 200 | buffer one rendered table row is assembled in, shared by every scrolling table |
| `ANSI_INPUT_BATCH_SIZE` | 32 | |
| `ANSI_INPUT_BATCH_TIMEOUT_MS` | 1000 | |
| `ANSI_CMD_TIMEOUT_MS` | 100 | how long a lone `ESC` waits for a sequence |
| `ANSI_CMD_FLUSH_TIMEOUT_MS` | 250 | |
| `STATUS_REFRESH_MS` | 1000 | status-bar metrics |
| `PAGE_REFRESH_MS` | 10000 | cold page refresh |
| `HOME_REFRESH_MS` | 5000 | |
| `PAGE_TASKS_MS` | 2000 | the TASKS page's cost columns |
| `PAGE_HOT_MS` | 1000 | clock and uptime |
| `TX_COLD_TARGET_MS` | 128 | the per-render byte budget target |
| `TX_HEADROOM_BYTES` | 256 | ring space the depth floor always leaves free, so one oversized run is not deferred forever |
| `LOG_ROWS_PER_TICK` | 2 | |
| `INPUT_EVENTS_PER_TICK` | 4 | |

### Shell and editor

| Macro | Default | |
|---|---|---|
| `SHELL_INPUT_LEN` | 512 | |
| `SHELL_HISTORY_SIZE` / `_ENTRY` | 8 / 128 | |
| `SHELL_KILL_SIZE` | 128 | |
| `SHELL_COMMANDS_MAX` | 64 | |
| `SHELL_SCROLLBACK_LINES` | 32 | |
| `LOGS_SCROLLBACK_LINES` | 64 | |
| `HOME_SCROLLBACK_LINES` | 6 | the home page's log tail |
| `SCROLLBACK_LINE_LEN` | 256 | |
| `SHELL_CONT_PREFIX` / `SHELL_LOG_CONT_PREFIX` | | continuation indents |
| `EDITOR_MAX_ROWS` | 32 | |
| `EDITOR_PROMPT_MAX` | 16 | |
| `EDITOR_MIN_COLS` | 32 | |

`EDITOR_MAX_ROWS * (EDITOR_MIN_COLS - EDITOR_PROMPT_MAX)` must be at least
`SHELL_INPUT_LEN`; a `static_assert` enforces it.

### Notes page

| Macro | Default | |
|---|---|---|
| `NOTES_LEN` | 2048 | the note's buffer, in bytes |
| `NOTES_MAX_ROWS` | 128 | row slots the editor lays the note out across |
| `NOTES_STORAGE_KEY` | `"notes"` | the `HAL::Storage` key it persists under |

The editor's buffers are supplied by their owner, so these size the NOTES page alone
and are independent of the `EDITOR_*` values the shell prompt uses.

### Persistent storage

| Macro | Default | |
|---|---|---|
| `STORAGE_KEY_MAX` | 129 | longest `HAL::Storage` key, NUL and partition prefix included. Mbed's KVStore sets it; every caller that formats a key (`ovr.<name>`, `<name>_serial`) sizes its scratch from this one number |

### Simulator only

`SIM_MAIN_CPU_CORE` 0, `STACK_SCAN_LIMIT` 65536, `STACK_PAINT_BYTE` 0xCD,
`STACK_SLICE_TICKS` (derived), `STORE_MAX_BYTES` 16384 (cap on the key-value store
file). Guarded by `TARGET_SIMULATOR`.

@note These per-parameter `#ifndef` guards cannot be collapsed into a helper macro:
the C++11 preprocessor has no way to conditionally define a macro whose name it
computes. The repetition is irreducible.
