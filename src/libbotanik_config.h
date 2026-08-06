// REVIEW_35: DONE
#ifndef LIBBOTANIK_CONFIG_H
#define LIBBOTANIK_CONFIG_H

/** @file libbotanik_config.h
 *  @brief Global configuration parameters for the Botanik library.
 *
 *  @note Every parameter in this file is overridable per site. The site selected
 *        by the build tools may ship `src/sites/<name>/config.h` and `#define`
 *        overrides. The path of the selected site is passed by CMake in
 *        BOTANIK_SITE_CONFIG.
 *
 *  @note What belongs here is what an INSTALLATION varies: a baud rate, a
 *        threshold, the size of a table a site fills. What does not is a number
 *        the code, a data type or a protocol already fixes - that is a
 *        `static constexpr` beside its one consumer, where the thing that
 *        constrains it can be read in the same breath. */

#include <cstddef>                    // size_t - the buffer-capacity constants
#include <cstdint>                    // uint8_t/uint16_t/uint32_t - the BOTANIK_CFG constant types

#define BOTANIK_CFG(type, name) static constexpr type name = BOTANIK_##name

// Site configuration
// ===========================================================================

// Vendored builds (the library copied into an Arduino PLC IDE sketch) get no -D
// and no include path: the IDE adds neither the sketch root nor its src/ as an
// include dir. So the vendoring step drops a generated botanik_site_config.h
// beside THIS header, at the vendored tree's root. A quoted include resolves
// against the including file's own directory, which is exactly that root, so the
// site config reaches every library TU with no build-system cooperation at all.
// Skipped when the build already named a site config (our CMake passes one).
#if !defined(BOTANIK_SITE_CONFIG) && defined(__has_include)
#  if __has_include("botanik_site_config.h")
#    define BOTANIK_SITE_CONFIG "botanik_site_config.h"
#  endif
#endif

#include "libbotanik_site.h"          // resolves BOTANIK_SITE -> BOTANIK_SITE_CONFIG / _HEADER

// The site's config.h, pulled in before the defaults below so a site can override
// any of them build-wide. It is included here with NONE of the library in scope,
// which is exactly why it is its own small file and not part of the site's
// site.h: that one needs the library types, and this one cannot have them.
#if defined(BOTANIK_SITE_CONFIG)
#  include BOTANIK_SITE_CONFIG        // #define BOTANIK_* before the defaults
#endif

// Feature flags
// ===========================================================================

#include "libbotanik_features.h"      // the BOTANIK_FEATURE_* defaults and BOTANIK_FEATURE_LIST

// Configuration variables
// ===========================================================================

namespace Botanik { namespace Config {

    /** @name Project identity
     *
     *  Who this unit is, in the three places it says so: the boot banner, the
     *  home screen, and the root of every MQTT topic it publishes.
     *  @{ */

    /** @brief Firmware version string, surfaced in the boot banner (hardware
     *  half comes from HAL::hardware_description()).
     *  @note Doxyfile's PROJECT_NUMBER quotes this; move the two together. */
#ifndef BOTANIK_FIRMWARE_VERSION
#  define BOTANIK_FIRMWARE_VERSION "0.13.12-dev"
#endif
    BOTANIK_CFG(const char*, FIRMWARE_VERSION);

    /** @brief Human-readable site (greenhouse) name, surfaced in the version
     *  command, home screen, and POST line. Pinned per site (matches the
     *  -DBOTANIK_SITE dir). Defined before the prefix so it can seed its
     *  default. */
#ifndef BOTANIK_SITE_NAME
#  define BOTANIK_SITE_NAME "generic"
#endif
    BOTANIK_CFG(const char*, SITE_NAME);

    /** @brief MQTT topic root shared by the alarm, telemetry, and log topics
     *  ("<prefix>/alarms/...", "<prefix>/<telemetry>/...",
     *  "<prefix>/<logs>/..."). Defaults to "default/<site>"; pinned per site. */
#ifndef BOTANIK_MQTT_TOPIC_PREFIX
#  define BOTANIK_MQTT_TOPIC_PREFIX "default/" BOTANIK_SITE_NAME
#endif
    BOTANIK_CFG(const char*, MQTT_TOPIC_PREFIX);

    /** @}
     *  @name Serial interfaces
     *
     *  The two UARTs and the rings behind them. Geometry of what is on the
     *  console wire is further down, under Terminal and console.
     *  @{ */

    /** @brief The FACTORY DEFAULT for the `serial_console_baud` parameter.
     *
     *  The live value is a parameter, not this - the rate an installation runs
     *  its console at is the operator's, and a unit whose terminal was swapped
     *  should not need a firmware build. `set serial_console_baud 9600` retunes
     *  the UART and the per-render TX budget together, on the next beat.
     *
     *  Kept here as the default so a site can still pin what its hardware ships
     *  with, and so a wiped store comes back at a known rate. */
#ifndef BOTANIK_CONSOLE_BAUD
#  define BOTANIK_CONSOLE_BAUD 4800
#endif
    BOTANIK_CFG(uint32_t, CONSOLE_BAUD);

    /** @brief The factory default for the `serial_monitor_baud` parameter.
     *  @note Effective on the simulator, which throttles its write to it. The
     *        OPTA's MONITOR endpoint is USB CDC and has no line rate, so
     *        begin() there ignores it. */
#ifndef BOTANIK_MONITOR_BAUD
#  define BOTANIK_MONITOR_BAUD 115200
#endif
    BOTANIK_CFG(uint32_t, MONITOR_BAUD);

    /** @brief BufferedSerial ring sizes, per port. Both must be a power of two
     *  (BufferedSerial static_asserts it); usable depth is size-1, so the TX
     *  ring below carries 4095 bytes - the figure SERIAL_TX_PER_STEP's note
     *  quotes. Costs RX+TX bytes of RAM for every port a build opens. */
#ifndef BOTANIK_SERIAL_RX_BUF
#  define BOTANIK_SERIAL_RX_BUF 256
#endif
    BOTANIK_CFG(uint16_t, SERIAL_RX_BUF);
#ifndef BOTANIK_SERIAL_TX_BUF
#  define BOTANIK_SERIAL_TX_BUF 4096
#endif
    BOTANIK_CFG(uint16_t, SERIAL_TX_BUF);

    /** @brief Most bytes BufferedSerial::transmit() hands the driver in one
     *  activation. 0 disables the cap (drain until the ring empties).
     *
     *  A bound, not a throttle. The drain used to run until the ring emptied or
     *  the driver refused, which is fine for a UART - its availableForWrite()
     *  reflects a real FIFO - but the OPTA's MONITOR port is USB CDC, where with
     *  no host attached availableForWrite() always answers 1. One activation
     *  would then push the whole 4095-byte ring, and with a host attached
     *  USBCDC::send() blocks per byte, putting tens of milliseconds inside a
     *  10 ms beat.
     *
     *  256 is far above steady demand on either port (the console's TX ring is
     *  self-capped near 61 bytes by the terminal's depth floor, and a log line
     *  is ~90), so nothing normal ever reaches it. What it buys is a bounded
     *  worst case. */
#ifndef BOTANIK_SERIAL_TX_PER_STEP
#  define BOTANIK_SERIAL_TX_PER_STEP 256
#endif
    BOTANIK_CFG(uint16_t, SERIAL_TX_PER_STEP);

    /** @}
     *  @name Scheduling
     *
     *  The beat, the rate classes tasks are grouped into, and the watchdog that
     *  outlives them. A task's RATE is one of the groups below; its PHASE is its
     *  place in the data flow and is written out in code, not configured.
     *  @{ */

    /** @brief Main task execution period (ms), i.e. the loop rate everything
     *  else divides down from. */
#ifndef BOTANIK_TASK_PERIOD_MS
#  define BOTANIK_TASK_PERIOD_MS 10
#endif
    BOTANIK_CFG(uint32_t, TASK_PERIOD_MS);

    /** @brief Every beat: the field bus, the log drain, the serial pumps.
     *
     *  This and the three below are the scheduler's rate classes. A task names
     *  one instead of carrying a raw period, so a rate is retuned in one place
     *  for everything that shares it. Each must be an integer multiple of
     *  TASK_PERIOD_MS. See docs/dev/scheduler.md for what sets each one. */
#ifndef BOTANIK_GROUP_FAST_MS
#  define BOTANIK_GROUP_FAST_MS BOTANIK_TASK_PERIOD_MS
#endif
    BOTANIK_CFG(uint32_t, GROUP_FAST_MS);

    /** @brief The operator-facing rate.
     *
     *  @note Currently has NO members: the console render, which this class was
     *  named for, now runs in GROUP_SLOW. Kept because a rate between "every
     *  beat" and "dwell" is the obvious home for the next interactive thing -
     *  but changing it presently affects nothing, so check tasks.h before
     *  tuning it. */
#ifndef BOTANIK_GROUP_MAIN_MS
#  define BOTANIK_GROUP_MAIN_MS 50
#endif
    BOTANIK_CFG(uint32_t, GROUP_MAIN_MS);

    /** @brief Dwell and config: alarm on_s/off_s are in SECONDS, and a parameter
     *  edit is bounded by how fast a human types. Also the console render, which
     *  is why TX_COLD_TARGET_MS must stay above this. */
#ifndef BOTANIK_GROUP_SLOW_MS
#  define BOTANIK_GROUP_SLOW_MS 100
#endif
    BOTANIK_CFG(uint32_t, GROUP_SLOW_MS);

    /** @brief Housekeeping measured in seconds - the input-expiry sweep, which
     *  is a full linear scan of the variable table run to resolve timeouts of
     *  3x a poll group's period. */
#ifndef BOTANIK_GROUP_BACKGROUND_MS
#  define BOTANIK_GROUP_BACKGROUND_MS 500
#endif
    BOTANIK_CFG(uint32_t, GROUP_BACKGROUND_MS);

    /** @brief Capacity of the scheduler's task table. Fixed, no allocation;
     *  registrations past it are refused and counted, never silently dropped. */
#ifndef BOTANIK_MAX_TASKS
#  define BOTANIK_MAX_TASKS 24
#endif
    BOTANIK_CFG(uint8_t, MAX_TASKS);

    /** @brief Phase step between control programs, applied by table index, so
     *  the scan's own programs spread over beats the way the registry's tasks
     *  do. Program @e i is offset by `i * this` (wrapped into its period).
     *
     *  Set to 0 to disable and have every program stay phase-locked to boot -
     *  the escape hatch for a site whose programs must share a beat. */
#ifndef BOTANIK_SCAN_STAGGER_MS
#  define BOTANIK_SCAN_STAGGER_MS BOTANIK_TASK_PERIOD_MS
#endif
    BOTANIK_CFG(uint32_t, SCAN_STAGGER_MS);

    /** @brief Hardware watchdog timeout. Clamped to what the IWDG can express. */
#ifndef BOTANIK_HW_WATCHDOG_MS
#  define BOTANIK_HW_WATCHDOG_MS 10000
#endif
    BOTANIK_CFG(uint32_t, HW_WATCHDOG_MS);

    /** @}
     *  @name Performance metrics
     *
     *  How the scheduler's self-instrumentation is smoothed before the health
     *  thresholds below read it. Window and buckets give the "recent peak";
     *  the EMA pairs give the displayed value.
     *  @{ */

    /** @brief Sliding window for the "recent peak" CPU/stack metrics, and the
     *  number of rotating buckets approximating it. The scheduler divides the
     *  window by TASK_PERIOD_MS to get the sample count the rolling-max filters
     *  are configured with. */
#ifndef BOTANIK_LOAD_METRIC_WINDOW_MS
#  define BOTANIK_LOAD_METRIC_WINDOW_MS 5000
#endif
    BOTANIK_CFG(uint32_t, LOAD_METRIC_WINDOW_MS);
#ifndef BOTANIK_LOAD_METRIC_BUCKETS
#  define BOTANIK_LOAD_METRIC_BUCKETS 10
#endif
    BOTANIK_CFG(uint32_t, LOAD_METRIC_BUCKETS);

    /** @brief CPU-load EMA time constants (ms), attack then decay.
     *
     *  Asymmetric on purpose: a load metric feeding an alarm threshold must
     *  rise at once and fall slowly, so a spike cannot be smoothed away before
     *  ALARM_CPU_LOAD_PCT sees it. Utils::ema_alpha() collapses any constant at
     *  or below TASK_PERIOD_MS to 1.0 (instant), which is what an attack of 0
     *  asks for; a larger value is a real time constant. */
#ifndef BOTANIK_CPU_LOAD_EMA_ATTACK_MS
#  define BOTANIK_CPU_LOAD_EMA_ATTACK_MS 0
#endif
    BOTANIK_CFG(uint32_t, CPU_LOAD_EMA_ATTACK_MS);
#ifndef BOTANIK_CPU_LOAD_EMA_DECAY_MS
#  define BOTANIK_CPU_LOAD_EMA_DECAY_MS 2000
#endif
    BOTANIK_CFG(uint32_t, CPU_LOAD_EMA_DECAY_MS);

    /** @brief CPU-jitter EMA time constants (ms), attack then decay. Same
     *  asymmetry as the load pair, with a longer tail. */
#ifndef BOTANIK_CPU_JITTER_EMA_ATTACK_MS
#  define BOTANIK_CPU_JITTER_EMA_ATTACK_MS 0
#endif
    BOTANIK_CFG(uint32_t, CPU_JITTER_EMA_ATTACK_MS);
#ifndef BOTANIK_CPU_JITTER_EMA_DECAY_MS
#  define BOTANIK_CPU_JITTER_EMA_DECAY_MS 5000
#endif
    BOTANIK_CFG(uint32_t, CPU_JITTER_EMA_DECAY_MS);

    /** @}
     *  @name Health and alarms
     *
     *  The platform-health thresholds HealthMonitor asserts from the metrics
     *  above, and the sizing of the model those alarms land in.
     *  @{ */

    /** @brief Platform-health alarm thresholds: CPU_LOAD / CPU_JITT fire when
     *  the rolling-peak load / jitter exceeds this percent of the task period;
     *  STACK fires above this percent stack usage. Per-alarm latch is in the
     *  site table (hold_on_s).
     *
     *  The load and jitter thresholds agree at 50% today but threshold
     *  different quantities (duty cycle vs scheduling lateness), so they stay
     *  two knobs. */
#ifndef BOTANIK_ALARM_CPU_LOAD_PCT
#  define BOTANIK_ALARM_CPU_LOAD_PCT 50
#endif
    BOTANIK_CFG(uint8_t, ALARM_CPU_LOAD_PCT);
#ifndef BOTANIK_ALARM_CPU_JITTER_PCT
#  define BOTANIK_ALARM_CPU_JITTER_PCT 50
#endif
    BOTANIK_CFG(uint8_t, ALARM_CPU_JITTER_PCT);
#ifndef BOTANIK_ALARM_STACK_PCT
#  define BOTANIK_ALARM_STACK_PCT 80
#endif
    BOTANIK_CFG(uint8_t, ALARM_STACK_PCT);

    /** @brief How long a momentary-event alarm (CPU_XRUN) stays held
     *  active after its counter ticks. HealthMonitor latches this in source (a
     *  proper held state, not a one-beat pulse), independent of the table's
     *  hold_on_s. */
#ifndef BOTANIK_ALARM_EVENT_HOLD_MS
#  define BOTANIK_ALARM_EVENT_HOLD_MS 300000
#endif
    BOTANIK_CFG(uint32_t, ALARM_EVENT_HOLD_MS);

    /** @brief How long after boot the SYS_RST alarm stays asserted, i.e. the
     *  window in which "this unit has recently reset" is still news. Read by
     *  HealthMonitor and by the SYS_RST description text, so the duration is
     *  stated once rather than in the code and the operator's string both. */
#ifndef BOTANIK_HEALTH_RESET_WINDOW_MS
#  define BOTANIK_HEALTH_RESET_WINDOW_MS 300000
#endif
    BOTANIK_CFG(uint32_t, HEALTH_RESET_WINDOW_MS);

    /** @brief Maximum number of alarm notifiers the model fans out to. */
#ifndef BOTANIK_ALARM_NOTIFIERS_MAX
#  define BOTANIK_ALARM_NOTIFIERS_MAX 4
#endif
    BOTANIK_CFG(uint8_t, ALARM_NOTIFIERS_MAX);

    /** @brief How long a cleared alarm stays in the ALARMS page's RECENT view
     *  (s). A condition's own recent_s overrides it (0 = this default, < 0 =
     *  never expires). */
#ifndef BOTANIK_ALARM_RECENT_S
#  define BOTANIK_ALARM_RECENT_S (7 * 24 * 3600)
#endif
    BOTANIK_CFG(int32_t, ALARM_RECENT_S);

    /** @brief SCOPE_A rows the alarm model has room for - the site's own and the
     *  library's (alarms/platform.h) together, since they share one table. What
     *  AlarmController sizes itself on; a table with more loses its tail, which
     *  BOTANIK_ASSERT catches. One number, because there are no longer two
     *  tables to divide it between. */
#ifndef BOTANIK_ALARM_SLOTS
#  define BOTANIK_ALARM_SLOTS 64
#endif
    BOTANIK_CFG(uint8_t, ALARM_SLOTS);

    /** @}
     *  @name Site table capacity
     *
     *  Bounds a site's generated tables must respect; each would otherwise be a
     *  silent runtime truncation (home fields past the three columns ignored, a
     *  table count past its uint8_t counter wrapped, a byte offset past
     *  ScalarField's int16_t field truncated). Each limit's authoritative source
     *  (HomePage geometry, ScalarField's offset type) static_asserts it still
     *  agrees, so the two can never drift.
     *  @{ */

    /** @brief Home-screen readings the HomePage lays out (its three columns
     *  summed); extras are ignored at render. Pinned in pages/home.h. */
#ifndef BOTANIK_HOME_FIELDS_MAX
#  define BOTANIK_HOME_FIELDS_MAX 22
#endif
    BOTANIK_CFG(uint8_t, HOME_FIELDS_MAX);

    /** @brief Distinct variable groups a site may have. A group is the prefix of
     *  a variable's name up to the first '_' (vars/access.h), interned into a
     *  small pool: naming a variable is what files it, so this bounds how many
     *  domains a table may span. Past it, var_group() answers "?" and the
     *  MONITOR / PARAMS view modes stop gaining rows. */
#ifndef BOTANIK_VAR_GROUPS_MAX
#  define BOTANIK_VAR_GROUPS_MAX 16
#endif
    BOTANIK_CFG(uint8_t, VAR_GROUPS_MAX);

    /** @brief Bytes one interned group name may occupy, NUL included, so a
     *  prefix longer than this truncates. Sizes the intern pools in both the
     *  variable table and the pages that group over it. */
#ifndef BOTANIK_VAR_GROUP_NAME_LEN
#  define BOTANIK_VAR_GROUP_NAME_LEN 16
#endif
    BOTANIK_CFG(size_t, VAR_GROUP_NAME_LEN);

    /** @}
     *  @name Logging
     *
     *  Message sizing, the per-writer queues, and the dual-bucket rate limiter
     *  in front of them.
     *  @{ */

    /** @brief Default minimum forwarded log level.
     *  @note SIMULATOR builds force it to 1 (debug) from CMakeLists.txt. */
#ifndef BOTANIK_LOG_MIN_LEVEL
#  define BOTANIK_LOG_MIN_LEVEL 2   /* Logs::LOG_INFO */
#endif
    BOTANIK_CFG(uint8_t, LOG_MIN_LEVEL);

    /** @brief Maximum length of a single log message, before formatting. */
#ifndef BOTANIK_LOG_MESSAGE_LEN
#  define BOTANIK_LOG_MESSAGE_LEN 256
#endif
    BOTANIK_CFG(size_t, LOG_MESSAGE_LEN);

    /** @brief Maximum length of a single formatted log line including metadata
     *  (bytes). Must clear LOG_MESSAGE_LEN plus the prefix format_log_entry()
     *  prepends (timestamp + level + topic + separators, ~40 bytes) plus the
     *  newline a serial writer appends, or a full-length message loses its tail
     *  on the way out. The static_assert below holds the two together. */
#ifndef BOTANIK_LOG_FORMATTED_LEN
#  define BOTANIK_LOG_FORMATTED_LEN 320
#endif
    BOTANIK_CFG(size_t, LOG_FORMATTED_LEN);

    static_assert(LOG_FORMATTED_LEN >= LOG_MESSAGE_LEN + 64,
                  "LOG_FORMATTED_LEN cannot hold LOG_MESSAGE_LEN plus the line prefix");

    /** @brief Maximum number of pointers to registered log writers */
#ifndef BOTANIK_LOG_WRITERS_MAX
#  define BOTANIK_LOG_WRITERS_MAX 8
#endif
    BOTANIK_CFG(uint8_t, LOG_WRITERS_MAX);

    /** @brief Per-writer in-flight message queue depth.
     *
     *  The dominant RAM line in a logging build: one writer costs roughly
     *  LOG_QUEUE_SIZE * (LOG_MESSAGE_LEN + 16) + LOG_FORMATTED_LEN, and the
     *  full page set registers five of them, each holding its own copy of every
     *  entry. Lowering it is the first thing to try on a tight build - but keep
     *  it above the boot burst (~20 entries in the first beat) or the announces
     *  are what gets dropped. */
#ifndef BOTANIK_LOG_QUEUE_SIZE
#  define BOTANIK_LOG_QUEUE_SIZE 32
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_SIZE);

    // BufferedLogWriter indexes the ring with uint8_t.
    static_assert(LOG_QUEUE_SIZE <= 255, "LOG_QUEUE_SIZE exceeds the writer's uint8_t ring index");

    /** @brief Log writer queue slots reserved for CRITICAL entries. */
#ifndef BOTANIK_LOG_QUEUE_CRITICAL_RESERVE
#  define BOTANIK_LOG_QUEUE_CRITICAL_RESERVE 4
#endif
    BOTANIK_CFG(size_t, LOG_QUEUE_CRITICAL_RESERVE);

    /** @brief Maximum burst size (number of logs allowed in a quick succession).
     *  Sized to absorb the boot burst: every Component announces on its first
     *  step, all in the first beat, on top of the setup() banner + POST. */
#ifndef BOTANIK_LOG_GLOBAL_BUCKET_BURST
#  define BOTANIK_LOG_GLOBAL_BUCKET_BURST 32
#endif
    BOTANIK_CFG(uint16_t, LOG_GLOBAL_BUCKET_BURST);

    /** @brief Refill period in milliseconds (determines the steady-state log rate) */
#ifndef BOTANIK_LOG_GLOBAL_BUCKET_PERIOD_MS
#  define BOTANIK_LOG_GLOBAL_BUCKET_PERIOD_MS 1000
#endif
    BOTANIK_CFG(uint32_t, LOG_GLOBAL_BUCKET_PERIOD_MS);

    /** @brief Maximum burst size per individual topic */
#ifndef BOTANIK_LOG_PERTOPIC_BUCKET_BURST
#  define BOTANIK_LOG_PERTOPIC_BUCKET_BURST 20
#endif
    BOTANIK_CFG(uint16_t, LOG_PERTOPIC_BUCKET_BURST);

    /** @brief Refill period in milliseconds for per-topic buckets */
#ifndef BOTANIK_LOG_PERTOPIC_BUCKET_PERIOD_MS
#  define BOTANIK_LOG_PERTOPIC_BUCKET_PERIOD_MS 5000
#endif
    BOTANIK_CFG(uint32_t, LOG_PERTOPIC_BUCKET_PERIOD_MS);

    /** @brief How often (ms) a summary of suppressed entries may be emitted.
     *  @note Decoupled from the bucket refill periods so a sustained log storm
     *  produces an occasional summary instead of one per refill tick. */
#ifndef BOTANIK_LOG_SUPPRESS_REPORT_PERIOD_MS
#  define BOTANIK_LOG_SUPPRESS_REPORT_PERIOD_MS 10000
#endif
    BOTANIK_CFG(uint32_t, LOG_SUPPRESS_REPORT_PERIOD_MS);

    /** @brief Minimum interval (ms) between dropped-entries-reports.
     *  @note First drop reports instantly; further reports wait out this holdoff. */
#ifndef BOTANIK_LOG_SINK_DROP_REPORT_PERIOD_MS
#  define BOTANIK_LOG_SINK_DROP_REPORT_PERIOD_MS 5000
#endif
    BOTANIK_CFG(uint32_t, LOG_SINK_DROP_REPORT_PERIOD_MS);

    /** @brief Minimum interval (ms) between dropped-bytes-reports.
     *  @note First drop reports instantly; further reports wait this holdoff. */
#ifndef BOTANIK_LOG_TX_DROP_REPORT_PERIOD_MS
#  define BOTANIK_LOG_TX_DROP_REPORT_PERIOD_MS 10000
#endif
    BOTANIK_CFG(uint32_t, LOG_TX_DROP_REPORT_PERIOD_MS);

    /** @}
     *  @name Network transport
     *  @{ */

    /** @brief Char-buffer length for a network address (hostname or IPv4).
     *  @note Matches the site's STR_CAP for the mqtt_host / ntp_srv* rows. */
#ifndef BOTANIK_HOST_STR_LEN
#  define BOTANIK_HOST_STR_LEN 64
#endif
    BOTANIK_CFG(size_t, HOST_STR_LEN);

    /** @brief Sockets HAL::Network keeps, per kind. These are the hard ceiling
     *  on how many network clients a site may run at once: NTP takes one UDP,
     *  MQTT one TCP, so the defaults leave nothing spare - a second publisher or
     *  a syslog client needs this raised, and gets a refused socket otherwise. */
#ifndef BOTANIK_NET_MAX_UDP
#  define BOTANIK_NET_MAX_UDP 2
#endif
    BOTANIK_CFG(int, NET_MAX_UDP);
#ifndef BOTANIK_NET_MAX_TCP
#  define BOTANIK_NET_MAX_TCP 2
#endif
    BOTANIK_CFG(int, NET_MAX_TCP);

    /** @brief Max time spent waiting for the link to come up. */
#ifndef BOTANIK_NET_SETUP_TIMEOUT_MS
#  define BOTANIK_NET_SETUP_TIMEOUT_MS 30000
#endif
    BOTANIK_CFG(uint32_t, NET_SETUP_TIMEOUT_MS);

    /** @brief Backoff after a failed (or lost) network bring-up before a retry. */
#ifndef BOTANIK_NET_RETRY_MS
#  define BOTANIK_NET_RETRY_MS 10000
#endif
    BOTANIK_CFG(uint32_t, NET_RETRY_MS);

    /** @}
     *  @name Name resolution
     *  @{ */

    /** @brief Size of the DNS resolver cache size.
     *  @note Also determines the maximum concurrent requests supported. */
#ifndef BOTANIK_DNS_CACHE_SIZE
#  define BOTANIK_DNS_CACHE_SIZE 8
#endif
    BOTANIK_CFG(size_t, DNS_CACHE_SIZE);

    /** @brief Async DNS lookups the HAL can have in flight. Defaults to
     *  DNS_CACHE_SIZE because that is how many rows NameResolver can leave
     *  PENDING at once (_alloc() handles every row being in flight), and a
     *  shortfall shows up as lookups that fail only once enough are concurrent.
     *  hal/net.cpp static_asserts the relation rather than trusting it.
     *
     *  Separate from DNS_CACHE_SIZE because they sit on opposite sides of the
     *  HAL boundary: a build with a second resolver needs more HAL slots than
     *  the library keeps cache rows. */
#ifndef BOTANIK_NET_MAX_DNS
#  define BOTANIK_NET_MAX_DNS BOTANIK_DNS_CACHE_SIZE
#endif
    BOTANIK_CFG(int, NET_MAX_DNS);

    /** @brief Max age (ms) a resolved entry is served from the cache before it is
     *  re-resolved. The HAL DNS API exposes no record TTL, so this bounds
     *  staleness (e.g. a server whose address changes) without it. 0 disables
     *  expiry (cache until evicted / cleared on reconfig). Default 1 h. */
#ifndef BOTANIK_DNS_CACHE_TTL_MS
#  define BOTANIK_DNS_CACHE_TTL_MS 3600000u
#endif
    BOTANIK_CFG(uint32_t, DNS_CACHE_TTL_MS);

    /** @brief Max time (ms) a row stays in flight before _alloc() may cancel and
     *  reclaim it. A caller that abandons a lookup (a group serial bumps mid
     *  resolve) never frees its row, so without this the table leaks a slot per
     *  abandonment until every row is PENDING and resolution fails for good. */
#ifndef BOTANIK_DNS_PENDING_MAX_MS
#  define BOTANIK_DNS_PENDING_MAX_MS 30000u
#endif
    BOTANIK_CFG(uint32_t, DNS_PENDING_MAX_MS);

    /** @}
     *  @name Time sync (NTP)
     *
     *  @note Only the timings are compile-time. Which servers to ask, whether
     *        to ask at all, and how often are runtime parameters in the site's
     *        variable table (ntp_srv1 / ntp_enable / ntp_poll_sec). The server
     *        port is fixed at the well-known 123 in network/ntp.h.
     *  @{ */

    /** @brief How long the SNTP client waits before giving up on a server. */
#ifndef BOTANIK_NTP_QUERY_TIMEOUT_MS
#  define BOTANIK_NTP_QUERY_TIMEOUT_MS 5000
#endif
    BOTANIK_CFG(uint32_t, NTP_QUERY_TIMEOUT_MS);

    /** @brief Backoff after a failed NTP sync before retrying. */
#ifndef BOTANIK_NTP_RETRY_MS
#  define BOTANIK_NTP_RETRY_MS 60000
#endif
    BOTANIK_CFG(uint32_t, NTP_RETRY_MS);

    /** @}
     *  @name MQTT client
     *
     *  @note The broker host and port are runtime parameters (mqtt_host /
     *        mqtt_port); what is here is the client's own sizing and cadence,
     *        plus the topic schema hanging off MQTT_TOPIC_PREFIX.
     *  @{ */

    /** @brief Max time to establish the TCP/MQTT session. */
#ifndef BOTANIK_MQTT_CONNECT_TIMEOUT_MS
#  define BOTANIK_MQTT_CONNECT_TIMEOUT_MS 10000
#endif
    BOTANIK_CFG(uint32_t, MQTT_CONNECT_TIMEOUT_MS);

    /** @brief Backoff after a failed connection before the MQTT client retries. */
#ifndef BOTANIK_MQTT_RETRY_MS
#  define BOTANIK_MQTT_RETRY_MS 30000
#endif
    BOTANIK_CFG(uint32_t, MQTT_RETRY_MS);

    /** @brief Scratch buffer (bytes) MqttClient builds an outgoing packet in.
     *  Bounds the whole PUBLISH: fixed header + topic + payload. A publish that
     *  would exceed it is refused outright, so it must clear MQTT_TOPIC_LEN plus
     *  the largest payload - which is a formatted log line, not a telemetry
     *  reading. The static_assert below holds that. */
#ifndef BOTANIK_MQTT_TX_BUF
#  define BOTANIK_MQTT_TX_BUF 384
#endif
    BOTANIK_CFG(size_t, MQTT_TX_BUF);

    /** @brief Buffer every publisher formats its topic in. One bound for the
     *  three of them (telemetry, logs, alarms), because all three build
     *  "<MQTT_TOPIC_PREFIX>/<segment>/<name>" from the same configurable prefix
     *  - sized apart, whichever was smallest truncated first. */
#ifndef BOTANIK_MQTT_TOPIC_LEN
#  define BOTANIK_MQTT_TOPIC_LEN 96
#endif
    BOTANIK_CFG(size_t, MQTT_TOPIC_LEN);

    static_assert(MQTT_TX_BUF >= MQTT_TOPIC_LEN + LOG_MESSAGE_LEN + 32,
                  "MQTT_TX_BUF cannot carry a full log line under the longest topic");

    /** @brief Buffer a single telemetry value is formatted into; caps the width
     *  of any published reading. */
#ifndef BOTANIK_MQTT_PAYLOAD_LEN
#  define BOTANIK_MQTT_PAYLOAD_LEN 64
#endif
    BOTANIK_CFG(size_t, MQTT_PAYLOAD_LEN);

    /** @brief Socket reads MqttClient drains per activation. The per-beat bound
     *  on its RX work, like MQTT_TELEMETRY_PER_STEP on the TX side. */
#ifndef BOTANIK_MQTT_RX_PER_STEP
#  define BOTANIK_MQTT_RX_PER_STEP 8
#endif
    BOTANIK_CFG(uint8_t, MQTT_RX_PER_STEP);

    /** @brief Period between MQTT telemetry publish cycles (ms).
     *  @note 0 disables telemetry.
     *  @note Each cycle publishes every registered field once. */
#ifndef BOTANIK_MQTT_TELEMETRY_PERIOD_MS
#  define BOTANIK_MQTT_TELEMETRY_PERIOD_MS 30000
#endif
    BOTANIK_CFG(uint32_t, MQTT_TELEMETRY_PERIOD_MS);

    /** @brief Topics published per activation while a telemetry sweep is in
     *  progress. 0 publishes the whole table in one go (the old behaviour).
     *
     *  Each topic is its own snprintf and its own tcp_send - MQTT 3.1.1 has no
     *  multi-topic PUBLISH - so a full sweep in one beat was 38-80 of them,
     *  0.8-1.9 ms against a 10 ms budget. Pacing spreads that over as many beats
     *  as it takes; 8 clears a typical table in well under a second, against a
     *  cadence measured in tens of seconds. */
#ifndef BOTANIK_MQTT_TELEMETRY_PER_STEP
#  define BOTANIK_MQTT_TELEMETRY_PER_STEP 8
#endif
    BOTANIK_CFG(uint16_t, MQTT_TELEMETRY_PER_STEP);

    /** @brief Publish telemetry with the MQTT retain flag set, so a late
     *  subscriber gets the last value of each field immediately.
     *
     *  @warning An invalid reading is skipped rather than published, and there
     *  is no last will, so with retain on the broker keeps serving the last good
     *  value of a sensor that has since died. Turn it off for an integration
     *  that must be able to tell "current" from "last seen". */
#ifndef BOTANIK_MQTT_TELEMETRY_RETAIN
#  define BOTANIK_MQTT_TELEMETRY_RETAIN 1
#endif
    BOTANIK_CFG(bool, MQTT_TELEMETRY_RETAIN);

    /** @brief Topic segment between the prefix and the field name, for each of
     *  the three publishers. They are the path schema every subscriber and
     *  dashboard agrees on; MQTT_TOPIC_PREFIX above is what separates one
     *  installation from another.
     *    telemetry: "<prefix>/<TELEMETRY>/<group>/<name>"
     *    logs:      "<prefix>/<LOG>/<level>/<topic>"
     *    alarms:    "<prefix>/<ALARM>/<CODE>" */
#ifndef BOTANIK_MQTT_TELEMETRY_TOPIC
#  define BOTANIK_MQTT_TELEMETRY_TOPIC "status"
#endif
    BOTANIK_CFG(const char*, MQTT_TELEMETRY_TOPIC);
#ifndef BOTANIK_MQTT_LOG_TOPIC
#  define BOTANIK_MQTT_LOG_TOPIC "logs"
#endif
    BOTANIK_CFG(const char*, MQTT_LOG_TOPIC);
#ifndef BOTANIK_MQTT_ALARM_TOPIC
#  define BOTANIK_MQTT_ALARM_TOPIC "alarms"
#endif
    BOTANIK_CFG(const char*, MQTT_ALARM_TOPIC);

    /** @brief Minimum level the MQTT log sink publishes (its own gate, on top of
     *  the logger's). Keeps low-severity chatter off the broker; criticals always
     *  pass. */
#ifndef BOTANIK_MQTT_LOG_MIN_LEVEL
#  define BOTANIK_MQTT_LOG_MIN_LEVEL 4   /* Logs::LOG_NOTICE */
#endif
    BOTANIK_CFG(uint8_t, MQTT_LOG_MIN_LEVEL);

    /** @}
     *  @name Field bus (Modbus RTU)
     *
     *  What the RS-485 installation is, and how much of it the planner may hold.
     *  The protocol's own fixed numbers are not here - they are beside the
     *  arithmetic that depends on them, in modbus/wire.h.
     *  @{ */

    /** @brief Bus line rate.
     *
     *  19200 carries the reference plant at about a fifth of the bus and
     *  survives long greenhouse cable runs; 115200 returns barely 2x on paper
     *  and less than that once the beat quantises each transaction, in exchange
     *  for real signal-integrity risk. See docs/design/modbus-budget.md, which
     *  works the whole sum. 38400 is the step to take first if a site outgrows
     *  it. */
#ifndef BOTANIK_MODBUS_BAUD
#  define BOTANIK_MODBUS_BAUD 19200
#endif
    BOTANIK_CFG(uint32_t, MODBUS_BAUD);

    /** @brief How long a slave is assumed to think before it answers.
     *
     *  Not a timeout: this is the figure the planner COSTS a transaction with,
     *  and it decides how wide a gap is worth reading across. It dominates
     *  everything - the link speed has no effect on it at all - so it is worth
     *  measuring on a real device rather than trusting. 10 ms is fair for a
     *  modern RS-485 meter; cheap ones are 20-50. */
#ifndef BOTANIK_MODBUS_TURNAROUND_MS
#  define BOTANIK_MODBUS_TURNAROUND_MS 10
#endif
    BOTANIK_CFG(uint32_t, MODBUS_TURNAROUND_MS);

    /** @brief Most XP_MODBUS rows the planner will hold. Sized above the
     *  reference site's 70; rows past it are dropped and counted, never
     *  silently ignored. */
#ifndef BOTANIK_MODBUS_MAX_ROWS
#  define BOTANIK_MODBUS_MAX_ROWS 128
#endif
    BOTANIK_CFG(uint16_t, MODBUS_MAX_ROWS);

    /** @brief Most blocks one plan may contain.
     *
     *  A backstop, not the real bound: the poll ladder already limits the count
     *  to devices x function codes x rungs. If this ever binds, the table has
     *  scattered registers rather than the plan being too ambitious. */
#ifndef BOTANIK_MODBUS_MAX_BLOCKS
#  define BOTANIK_MODBUS_MAX_BLOCKS 32
#endif
    BOTANIK_CFG(uint16_t, MODBUS_MAX_BLOCKS);

    /** @brief Most registers one block may span.
     *
     *  Under the protocol's own 125, so that an FC16 of a full block stays
     *  inside the 255 bytes the UART can hold in one go: the driver enable is
     *  hardware-timed and drops when the shift register empties, so a frame that
     *  does not fit in the buffer would be cut in half on the wire. Coils are
     *  not capped here - eight to a byte, their own ceiling binds first. */
#ifndef BOTANIK_MODBUS_MAX_REGS_PER_BLOCK
#  define BOTANIK_MODBUS_MAX_REGS_PER_BLOCK 120
#endif
    BOTANIK_CFG(uint16_t, MODBUS_MAX_REGS_PER_BLOCK);

    /** @brief Step between the due times of blocks sharing a rung, so a ladder
     *  full of one-second groups does not fire them all on the same beat. Same
     *  rule the task registry and the scan cycle use. */
#ifndef BOTANIK_MODBUS_BLOCK_STAGGER_MS
#  define BOTANIK_MODBUS_BLOCK_STAGGER_MS 20
#endif
    BOTANIK_CFG(uint32_t, MODBUS_BLOCK_STAGGER_MS);

    /** @}
     *  @name Sun position tracking
     *  @{ */

    /** @brief Recompute cadence (ms) for the solar-elevation tracker.
     *  @note The elevation drifts ~0.25 deg/min, so a minute between updates
     *        keeps it well under a degree stale for a fraction of the per-beat
     *        trig work. */
#ifndef BOTANIK_SUN_RECOMPUTE_MS
#  define BOTANIK_SUN_RECOMPUTE_MS 60000
#endif
    BOTANIK_CFG(uint32_t, SUN_RECOMPUTE_MS);

    /** @}
     *  @name Terminal and console
     *
     *  What is physically on the console wire, and how much of it the UI may
     *  use. There is no resize handshake: what is declared here is what the
     *  pages lay themselves out for.
     *  @{ */

    /** @brief Which terminal is wired to the console UART - a property of the
     *  installation, so a site declares it rather than a harness passing it in.
     *  It selects the one-time setup handshake and is what any
     *  terminal-specific behaviour branches on.
     *
     *  A plain integer here because this header is included by everything and
     *  must not depend on the console: it is the numeric form of
     *  Terminal::Ansi::TerminalType, which static_asserts the two agree.
     *    0 = VT100, 1 = VT102, 2 = Minitel 1B, 3 = Minitel 2 */
#ifndef BOTANIK_TERMINAL_TYPE
#  define BOTANIK_TERMINAL_TYPE 1
#endif
    BOTANIK_CFG(uint8_t, TERMINAL_TYPE);

    /** @brief Geometry of the terminal actually on the console wire. Bounded
     *  by TERMINAL_MAX_COLS / _ROWS, which size the screen back-buffer. */
#ifndef BOTANIK_CONSOLE_COLS
#  define BOTANIK_CONSOLE_COLS 80
#endif
    BOTANIK_CFG(uint16_t, CONSOLE_COLS);
#ifndef BOTANIK_CONSOLE_ROWS
#  define BOTANIK_CONSOLE_ROWS 24
#endif
    BOTANIK_CFG(uint16_t, CONSOLE_ROWS);

    /** @brief Largest terminal the UI supports. These size the screen
     *  back-buffer (AnsiTerminal keeps two cell grids of COLS x ROWS bytes, so
     *  the pair costs 2 x their product in RAM) and clamp the console geometry
     *  at construction; they are also the sanity bound on full-width fills like
     *  the Home log-header bar, so a bogus column count cannot drive an
     *  unbounded paint. 132x32 is a wide VT with room to spare over the 80x24
     *  the pages actually lay out for. */
#ifndef BOTANIK_TERMINAL_MAX_COLS
#  define BOTANIK_TERMINAL_MAX_COLS 132
#endif
    BOTANIK_CFG(uint16_t, TERMINAL_MAX_COLS);
#ifndef BOTANIK_TERMINAL_MAX_ROWS
#  define BOTANIK_TERMINAL_MAX_ROWS 32
#endif
    BOTANIK_CFG(uint16_t, TERMINAL_MAX_ROWS);

    /** @brief Pages the Console multiplexes over. Registration past it is
     *  refused SILENTLY (add_page() returns -1 and register_pages() ignores it),
     *  so the tab order simply stops gaining pages. Cannot exceed 10: a page is
     *  also reachable by Alt+<digit>, and there are ten digits.
     *
     *  The full page set is already 8, so the default sits at the ceiling: at 8
     *  a site splicing in its own page (the documented StatusPage extension) got
     *  nothing, with no way to tell. */
#ifndef BOTANIK_CONSOLE_PAGES_MAX
#  define BOTANIK_CONSOLE_PAGES_MAX 10
#endif
    BOTANIK_CFG(uint8_t, CONSOLE_PAGES_MAX);

    /** @}
     *  @name Terminal input decoding
     *  @{ */

    /** @brief Number of input characters to process in a single batch. */
#ifndef BOTANIK_ANSI_INPUT_BATCH_SIZE
#  define BOTANIK_ANSI_INPUT_BATCH_SIZE 32
#endif
    BOTANIK_CFG(size_t, ANSI_INPUT_BATCH_SIZE);

    /** @brief Safety timeout: flush any stale batch regardless of stream activity. */
#ifndef BOTANIK_ANSI_INPUT_BATCH_TIMEOUT_MS
#  define BOTANIK_ANSI_INPUT_BATCH_TIMEOUT_MS 1000
#endif
    BOTANIK_CFG(uint32_t, ANSI_INPUT_BATCH_TIMEOUT_MS);

    /** @brief Time after the last byte before a stalled command sequence is resolved */
#ifndef BOTANIK_ANSI_CMD_TIMEOUT_MS
#  define BOTANIK_ANSI_CMD_TIMEOUT_MS 100
#endif
    BOTANIK_CFG(uint32_t, ANSI_CMD_TIMEOUT_MS);

    /** @brief Max time a coalesced command (held backspace / Ctrl+D) is held
     *         under TX backpressure before it flushes.
     *  @note Shorter than the text-batch timeout: an edit wants lower
     *        latency than a paste batch. */
#ifndef BOTANIK_ANSI_CMD_FLUSH_TIMEOUT_MS
#  define BOTANIK_ANSI_CMD_FLUSH_TIMEOUT_MS 250
#endif
    BOTANIK_CFG(uint32_t, ANSI_CMD_FLUSH_TIMEOUT_MS);

    /** @brief Max decoded input events handled per tick.
     *  @note Too low: typing/paste feels sluggish and the RX ring backs up.
     *  @note Too high: a big paste echoes a large burst in one tick. */
#ifndef BOTANIK_INPUT_EVENTS_PER_TICK
#  define BOTANIK_INPUT_EVENTS_PER_TICK 4
#endif
    BOTANIK_CFG(uint16_t, INPUT_EVENTS_PER_TICK);

    /** @}
     *  @name Page refresh cadences
     *
     *  All time-based, so a refresh rate is independent of the tick interval.
     *  Three classes: what changes every second, what changes slowly, and the
     *  TASKS table's smoothed costs in between.
     *  @{ */

    /** @brief Refresh interval (ms) for a page's hot fields, and for the status
     *  bar's clock and metrics. */
#ifndef BOTANIK_PAGE_HOT_MS
#  define BOTANIK_PAGE_HOT_MS 1000
#endif
    BOTANIK_CFG(uint32_t, PAGE_HOT_MS);

    /** @brief Cold-field refresh interval (ms) for a metric page. */
#ifndef BOTANIK_PAGE_COLD_MS
#  define BOTANIK_PAGE_COLD_MS 5000
#endif
    BOTANIK_CFG(uint32_t, PAGE_COLD_MS);

    /** @brief TASKS table repaint cadence. Slower than PAGE_HOT_MS: the costs it
     *  shows are smoothed aggregates, not a live trace. */
#ifndef BOTANIK_PAGE_TASKS_MS
#  define BOTANIK_PAGE_TASKS_MS 2000
#endif
    BOTANIK_CFG(uint32_t, PAGE_TASKS_MS);

    /** @brief Buffer one rendered table row is assembled in, before the
     *  terminal clips it to the screen width. Shared by every scrolling table
     *  (PARAMS, MONITOR, TASKS, ALARMS) and the ScrollTable framework under
     *  them: wide enough for a full-width row plus the escapes and separators
     *  the cells carry. */
#ifndef BOTANIK_TABLE_LINE_LEN
#  define BOTANIK_TABLE_LINE_LEN 200
#endif
    BOTANIK_CFG(uint16_t, TABLE_LINE_LEN);

    /** @}
     *  @name TX pacing
     *
     *  We only have one FIFO queue for serial TX and cannot jump ahead, so we need
     *  bulk producers to only progress while the TX buffer is shallow to keep the
     *  the interactive path responsive.
     *
     *  A depth floor gates that; per-tick caps bound the burst once open.
     *  See docs/ for the full model.
     *
     *  @{ */

    /** @brief Single paint depth floor, in ms of wire time at CONSOLE_BAUD.
     *
     *  Must stay above GROUP_SLOW_MS (console.cpp static_asserts it): the render
     *  runs on that group, so a floor below it would let the wire drain empty
     *  between paints. The margin is thin at the defaults - 128 against 100 -
     *  and nothing measures an underrun, so raise this rather than lower it. */
#ifndef BOTANIK_TX_COLD_TARGET_MS
#  define BOTANIK_TX_COLD_TARGET_MS 128
#endif
    BOTANIK_CFG(uint32_t, TX_COLD_TARGET_MS);

    /** @brief TX ring headroom the depth floor always leaves free, so a single
     *  oversized run still has somewhere to land rather than being deferred
     *  forever. Must stay comfortably under SERIAL_TX_BUF. */
#ifndef BOTANIK_TX_HEADROOM_BYTES
#  define BOTANIK_TX_HEADROOM_BYTES 256
#endif
    BOTANIK_CFG(uint16_t, TX_HEADROOM_BYTES);

    /** @brief Max log/command rows painted per tick per page.
     *  @note At low baud the wire is the real ceiling (4800 baud ~= 4.4 lines/s), so a
     *        larger value only deepens floor overshoot and worsens echo latency. */
#ifndef BOTANIK_LOG_ROWS_PER_TICK
#  define BOTANIK_LOG_ROWS_PER_TICK 2
#endif
    BOTANIK_CFG(uint16_t, LOG_ROWS_PER_TICK);

    /** @}
     *  @name Shell and scrollback
     *  @{ */

    /** @brief Maximum length of the shell input buffer (bytes) */
#ifndef BOTANIK_SHELL_INPUT_LEN
#  define BOTANIK_SHELL_INPUT_LEN 512
#endif
    BOTANIK_CFG(uint16_t, SHELL_INPUT_LEN);

    /** @brief Number of command history entries to retain */
#ifndef BOTANIK_SHELL_HISTORY_SIZE
#  define BOTANIK_SHELL_HISTORY_SIZE 8
#endif
    BOTANIK_CFG(uint8_t, SHELL_HISTORY_SIZE);

    /** @brief Maximum length of a single history entry (bytes).
     *  @note Below SHELL_INPUT_LEN, so a longer command is stored truncated and
     *        recalling it yields a different command. Raise to SHELL_INPUT_LEN
     *        (at SHELL_HISTORY_SIZE x the RAM) if that matters. */
#ifndef BOTANIK_SHELL_HISTORY_ENTRY
#  define BOTANIK_SHELL_HISTORY_ENTRY 128
#endif
    BOTANIK_CFG(uint16_t, SHELL_HISTORY_ENTRY);

    /** @brief Size of the kill ring buffer (bytes) */
#ifndef BOTANIK_SHELL_KILL_SIZE
#  define BOTANIK_SHELL_KILL_SIZE 128
#endif
    BOTANIK_CFG(uint16_t, SHELL_KILL_SIZE);

    /** @brief Max size of the site commands table */
#ifndef BOTANIK_SHELL_COMMANDS_MAX
#  define BOTANIK_SHELL_COMMANDS_MAX 64
#endif
    BOTANIK_CFG(size_t, SHELL_COMMANDS_MAX);

    /** @brief Scrollback lines retained by ShellPage for redraw/page-switch
     *  replay */
#ifndef BOTANIK_SHELL_SCROLLBACK_LINES
#  define BOTANIK_SHELL_SCROLLBACK_LINES 32
#endif
    BOTANIK_CFG(uint16_t, SHELL_SCROLLBACK_LINES);

    /** @brief Scrollback lines retained by LogsPage for page-switch replay */
#ifndef BOTANIK_LOGS_SCROLLBACK_LINES
#  define BOTANIK_LOGS_SCROLLBACK_LINES 64
#endif
    BOTANIK_CFG(uint16_t, LOGS_SCROLLBACK_LINES);

    /** @brief Log lines the Home page keeps in its bottom log tail.
     *  @note Small on purpose: a live "last few entries" widget,
     *        not a full scrollback like the Logs page. */
#ifndef BOTANIK_HOME_SCROLLBACK_LINES
#  define BOTANIK_HOME_SCROLLBACK_LINES 6
#endif
    BOTANIK_CFG(uint16_t, HOME_SCROLLBACK_LINES);

    /** @brief Maximum length of a single retained scrollback line (bytes).
     *  @note Shared by every scrollback / log buffer (Shell, Logs, Home), so the
     *        cost is this times the three line counts above. Distinct from the
     *        log-message sizes: this is what a page STORES, not what the logger
     *        formats. */
#ifndef BOTANIK_SCROLLBACK_LINE_LEN
#  define BOTANIK_SCROLLBACK_LINE_LEN 256
#endif
    BOTANIK_CFG(uint16_t, SCROLLBACK_LINE_LEN);

    /** @}
     *  @name Text editor
     *  @{ */

    /** @brief Maximum number of screen rows the TextEditor can lay out */
#ifndef BOTANIK_EDITOR_MAX_ROWS
#  define BOTANIK_EDITOR_MAX_ROWS 32
#endif
    BOTANIK_CFG(uint16_t, EDITOR_MAX_ROWS);

    /** @brief Maximum length (bytes) of a prompt prefix (PS1/PS2/PS3) */
#ifndef BOTANIK_EDITOR_PROMPT_MAX
#  define BOTANIK_EDITOR_PROMPT_MAX 16
#endif
    BOTANIK_CFG(uint16_t, EDITOR_PROMPT_MAX);

    /** @brief Narrowest terminal width the TextEditor can support.
     *  @note We need to keep some additional margin because the actual width
     *        is reduced by the row prefixes (PS1/PS2/PS3).
     *  @note Read only by the two layout static_asserts, which is why it lives
     *        here beside the lengths they relate rather than in the editor. */
#ifndef BOTANIK_EDITOR_MIN_COLS
#  define BOTANIK_EDITOR_MIN_COLS 32
#endif
    BOTANIK_CFG(uint16_t, EDITOR_MIN_COLS);

    // Ensure that the specified editor minimum size can hold the whole input
    // buffer. Exact at the defaults; it passes only because the buffer reserves
    // its NUL. Note it does not model TextEditor's _indent, which is safe today
    // only because the one indented editor is also single-line.
    static_assert(EDITOR_MAX_ROWS * (EDITOR_MIN_COLS - EDITOR_PROMPT_MAX) >= SHELL_INPUT_LEN,
                  "EDITOR_MAX_ROWS too small to lay out SHELL_INPUT_LEN at EDITOR_MIN_COLS");

    /** @}
     *  @name Operator notes
     *  @{ */

    /** @brief Capacity of the NOTES buffer (bytes, NUL included) */
#ifndef BOTANIK_NOTES_LEN
#  define BOTANIK_NOTES_LEN 2048
#endif
    BOTANIK_CFG(uint16_t, NOTES_LEN);

    /** @brief Rows the NOTES editor can lay out. Sized past what soft-wrap
     *  needs (see the static_assert below), so only a note made of very short
     *  hard-broken lines can reach it - and an insert that would is refused. */
#ifndef BOTANIK_NOTES_MAX_ROWS
#  define BOTANIK_NOTES_MAX_ROWS 128
#endif
    BOTANIK_CFG(uint16_t, NOTES_MAX_ROWS);

    // Same bound as SHELL_INPUT_LEN above: a row carries a prompt, so its worst
    // case is the narrowest screen minus the widest gutter.
    static_assert(NOTES_MAX_ROWS * (EDITOR_MIN_COLS - EDITOR_PROMPT_MAX) >= NOTES_LEN,
                  "NOTES_MAX_ROWS too small to lay out NOTES_LEN at EDITOR_MIN_COLS");

    /** @}
     *  @name Simulator only
     *  @{ */

#ifdef TARGET_SIMULATOR

    /** @brief Cap on the simulator's whole key-value store file (bytes).
     *  @note The OPTA's KVStore is a TDBStore on the external QSPI flash, bounded
     *        by its region and not by this, so this is sim-only. The sim file
     *        holds every key at once and hex-encodes values, so it needs twice
     *        the largest value plus room for every parameter. */
#ifndef BOTANIK_STORE_MAX_BYTES
#  define BOTANIK_STORE_MAX_BYTES 16384
#endif
    BOTANIK_CFG(uint32_t, STORE_MAX_BYTES);

    static_assert(2 * (uint32_t)NOTES_LEN + 1024 <= STORE_MAX_BYTES,
                  "STORE_MAX_BYTES too small for a hex-encoded NOTES_LEN plus the parameters");

#endif // TARGET_SIMULATOR

    /** @} */

} }


// internal helper; don't leak it to includers
#undef BOTANIK_CFG

#endif // LIBBOTANIK_CONFIG_H
