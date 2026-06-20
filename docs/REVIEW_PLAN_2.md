# Expert review round 2 - action plan

Outcome of the second systematic review (`expert-review-2`) and the follow-up
design discussion. Round 1 lives in `REVIEW_PLAN.md` (all closed); this is the
next wave. Each item keeps the `[ANCHOR]` from the discussion so it stays
referenceable. Low/nit findings and their per-item decisions live in
`REVIEW_2_MINOR.md`; this file carries the shape work and the accepted fixes.

Conventions unchanged: no heap, no blocking, no recursion, volatile +
`HAL::dsb()`/`dmb()`/`ordered_*` instead of `std::atomic`, 32-bit-ms wrap-safe
timing (`Utils::Millis32`), functions fit one screen, classes stay small, short
client-readable comments.

Ordering: Block 0 is the shape work everything hangs off (HealthMonitor + alarm
refactor + watchdog escalation + composition). Then cross-core/HW, scheduler,
logging, terminal/pages, shell, build, serial, docs.

---

## Decisions log (the shape calls we converged on)

- **Watchdog = timed escalation, not decouple.** Letting the HW WDT expire resets
  *both* cores and gives the PLC image a fresh start - that is wanted. So: keep
  kicking through a transient PLC fault (logged + alarm); withhold the kick only
  after the fault persists past a grace window. HealthMonitor owns the grace timer.
  (This **supersedes** the old `~/.claude/plans/healthmonitor.md`, which preserved
  today's immediate-withhold behaviour.)
- **Alarms: separate namespaces, not splice-into-top-bits.** PLC-published alarms
  occupy condition indices `[0..N)`, locally-generated platform alarms `[N..N+M)`.
  The remote word is masked to `N` bits before merge, so a stray PLC bit cannot
  reach a platform condition. The condition array is the source of truth (sized
  `N+M`, not a fixed 64); a bitset, if kept, is a *derived* active-set view. This
  kills `SPLICE-NO-BOUND`, `PLC-PLATFORM-COLLISION`, `ALARM-MODEL-MUTATES-SITE-TABLE`
  and `MASKEDBY-PLATFORM-COUPLING` by construction.
- **Composition: Template Method, no new templates.** `setup()`/`loop()` become
  non-virtual in `BotanikFirmware` (they own the fixed bring-up order) and call
  protected virtual hooks. Page registration is one such hook: the default
  registers the built-in pages; a site overrides it to add or disable pages. Magic
  `add_page(&p, 3)` indices removed - append order in the hook is the tab order. The
  concrete site is `final`.
- **Criticals: rate-limited (flood backstop) AND reserve-protected at the writer.**
  The rate limiter may drop a critical as a last-ditch flood guard (`CRITICAL-RATELIMITED`
  = docs only); the writer FIFO never drops one because K slots are reserved for
  criticals (`SUPPRESS-REPORT-FANOUT-LOSS`), which also breaks the self-feeding
  sink-loss report loop.
- **TX truncation prevented by a guaranteed floor, not per-write accounting.** Size
  the cold-depth reserve to a bounded worst-case field (`MAX_FIELD_TX_BYTES`) +
  `static_assert`, so `tx_below_cold()` true ⇒ a field can't truncate, by construction.
- **Latch timing: countdown, not absolute deadline.** No wrap-sensitive `now <
  latch_until`; decrement a remaining counter each beat. Drops the 64-bit dependency.

---

## Block 0 - Self-monitoring, alarms, watchdog, composition (do first)

### `[HEALTHMONITOR]` - the coordinator the alarm flow runs through

Merge of `~/.claude/plans/healthmonitor.md`, with the two edits noted in the
Decisions log. New header-only `Botanik::Health::HealthMonitor`
(`src/libbotanik/health/health.h`), non-templated (the remote bitset is passed
into `step()` by the already-templated firmware).

- [x] Owns (moved out of `BotanikFirmware`): `Alarms::MqttAlarmNotifier _mqtt`,
  the (refactored) `Alarms::AlarmModel _model`, the per-tick platform-alarm
  assertion block (`SYS_RST`/`CPU_LOAD`/`CPU_JITT`/`STACK`/`CPU_XRUN`), the
  `_xrun_last` edge bookkeeping, and pointers to `Logger`/`SchedulerContext`/
  `PlcSyncContext`. Subsumes `AlarmReader` (`alarms/reader.h` now unused).
- [x] `post(in_region, out_region)` - power-on self-test, once from `setup()`:
  platform codes resolve via `index_of()` (no silent `set_local` no-op);
  regions non-null and **32-byte aligned** (cache/DMA); logger has >=1 writer.
  POST failure logs CRITICAL but does **not** withhold the watchdog. (Verified: sim
  logs "POST ok".)
- [x] `step(remote_raw, now)` - assert platform alarms from the scheduler context,
  then `_model.step(remote_raw, now)`. (`now` is `uptime_ms()` this pass, matching
  the model - the 32-bit `timestamp_ms` projection is the separate
  `[CYCLE-CONTRACT-DRIFT]` item in Block 8.)
- [x] Runtime self-test set: a rising `rx_torn_count()` raises a held `RX_TORN` alarm
  (new `PLATFORM_ALARMS` row; the handler's torn count is passed into `step()`). Held in
  the source via the shared `_held_event` helper (like `CPU_XRUN`). Should never fire.
- [x] **`[WATCHDOG-ESCALATION]`** (was `[WATCHDOG-COUPLING]`) `watchdog_ok()` owns the
  grace timer: kicks while healthy and through a transient fault; withholds (logging
  the reboot once, edge-triggered) only once the fault persists past
  `Config::PLC_FAULT_GRACE_MS` (15 s). The `PLC_FAULT` edge log was retuned from
  "withholding kick, rebooting" to "PLC link fault" since the withhold is now deferred.
- [x] Wire-through: `_terminal(..., &_health.model())`, `_alarms_page(...,
  &_health.model())`; `health()` / `alarm_model()` accessors; `Logger::has_writers()`.
  (TODO: resolve `[ARCH-HEALTH]` in TODO.md + update CLAUDE.md subsystem list - docs,
  Block 8.)
- [ ] **`[HOME-ALARM-SOURCE]`** Home's alarm/status summary reads `_health.model()`
  (the single source of truth), not raw payload booleans. **Deferred** to the Home-page
  rework (a gh_mlb41 site-page change; the model is now available to it via the terminal).

### `[ALARM-NAMESPACE]` - separate PLC and platform ranges

- [x] Condition array sized `N + M` (PLC count + platform count); PLC conditions at
  `[0..N)`, platform at `[N..N+M)`. No splice into "top of 64". (Model owns the array;
  the site table is now `const` - kills `ALARM-MODEL-MUTATES-SITE-TABLE`.)
- [x] `step()` masks `remote_raw` to the low `N` bits before merging with local;
  `set_local` only addresses the platform range. Collision impossible by construction.
  (Kills `SPLICE-NO-BOUND`, `PLC-PLATFORM-COLLISION`; `BOTANIK_ASSERT(N+M<=64)` + clamp.)
- [x] Bitset stays a 64-bit `_raw` *internally* (masked); the page/ribbon active-set
  signatures are computed from the conditions (derived view). `count()` is now `N+M`.
- [x] **`[LATCH-WRAP]`** latch as a per-condition countdown (`latch_remaining_ms`
  decremented by the beat's elapsed, refreshed to `latch_s*1000` while raw-on; latched
  while `> 0`). Wrap-immune. (Kept `edge_ms`/`started_ms` on 64-bit uptime this pass -
  correct, and display ages > 49 days need it; the full 32-bit `timestamp_ms` projection
  is the separate `[CYCLE-CONTRACT-DRIFT]` item in Block 8.)
- [x] **`[XRUN-LATCH-ONLY]`** HealthMonitor holds `CPU_XRUN` active for
  `ALARM_XRUN_HOLD_MS` after each overrun (a proper latched state in the source); the
  table's `latch_s` for `CPU_XRUN` is now 0 (no double-latch).
- [x] **`[ALARM-DESC-EMPTY-LOG]`** START/END log falls back to `c.code` when `desc` null.
- [x] **`[BOOT-ACTIVE-NOTIFY]`** keep current behaviour (announce a pre-existing
  boot-active alarm as new - best we can do). No code change.

### `[CHAINUP-SILENT-FAILURE]` - Template-Method composition

- [x] `BotanikFirmware::setup()`/`loop()` non-virtual; they call protected virtual
  hooks (`register_pages()`, `on_setup()`, `on_loop(now_ms)`) at defined points. A site
  cannot skip/misorder base bring-up - it overrides hooks, never setup()/loop().
- [x] Page registration hook: the default registers the built-ins via `add_page`
  (append); a site overrides `register_pages()` to list its pages in tab order (or drop
  a built-in). **The magic indices are gone** - `add_page(p, index)` was removed, so the
  order of `add_page()` calls IS the tab order (no `PageSlot` enum needed). Verified: the
  sim boots on Home (page 0) with tab order Home/Shell/Logs/Status/Alarms.
- [x] **`[VIRTUAL-IN-HOT-LOOP]`** the concrete site is `final` (devirtualizes the hooks);
  `loop()`/`setup()` are non-virtual so there's no vtable lookup to enter them.

---

## Block 1 - Cross-core / hardware correctness (least testable on the sim)

- [x] **`[TX-WAIT-STALE-ACK]` / `[FIRST-CONTACT-RXSEQ]`** explicit first-contact baseline
  in the handler ctor: snapshot the PLC's output seq/ack, then seed our input region to a
  known start (rxSeqNum=0; txAckNum=the PLC's current txSeqNum so a stale value isn't a
  phantom RX message). `_tx_acknowledged()` gates the first ack on `ack != _ack_baseline`
  (the bind-time stale value), so a stale rxAckNum can't declare the link RUNNING + feed
  the watchdog. A real PLC catches up within a commit or two. Verified: sim still links.
- [x] **`[SIM-DILATION-OVERFLOW]`** `_now_as()` now does the scale multiply in `__int128`
  over nanoseconds (sim is x86-64 only), keeping full precision and overflow-safe to
  astronomical timepoints. Verified at `TIME_SCALE=1000`.
- [x] **`[CACHE-INVALIDATE-EVICT]`** `@warning` documents the real invariant (invalidate
  only the read-only PLC-output region, clean the input region; sub-region calls round to
  that region's own lines). NOTE: a blanket "32-byte aligned" *assert* proved too strict -
  `logs.cpp` legitimately invalidates the log ring at offset 8 of the read-only `_plcout`
  (safe, nothing dirty to lose), so I dropped the runtime assert and kept the doc. DECIDED:
  leave as-is - an alignment assert wouldn't catch the real hazard (invalidating a dirty
  line, not an alignment property), and forcing whole-region invalidates to enable one
  would flush ~8 KB to re-read 8 bytes on the torn path. `@warning` is the right protection.
- [~] **`[WALLCLOCK-WRAP-2038]`** flagged precisely in `opta.h` (the `time_t` 2038 wrap
  before the widening cast). The real fix needs a 64-bit RTC read, which is coupled to the
  future TimeSyncHandler/RTC path - **deferred** there, comment left as the marker.
- [x] **`[STACK-DEPTH-SIGN]`** `get_stack_depth` guards `sp <= base` (returns 0 otherwise).
- [x] **`[ALIGNAS-PLC-CONTRACT]`** `static_assert(is_empty<Data> || sizeof(Data) % 4 == 0)`
  inside both `PlcInput`/`PlcOutput` templates (alignas(32) already guarantees the 32-byte
  region multiple, so the per-site check that matters is the payload word-multiple).
- [x] **`[RESTRICT-ALIAS]`** `@warning` on `ordered_copy` + a DEBUG `assert(dst != src)`
  (both backends).
- [x] **`[EMPTY-STRUCT-SIZEOF1]`** `if (is_empty<T>) return;` no-op in `ordered_copy`/
  `ordered_move` (both backends).
- [x] **`[HEARTBEAT-FROM-ACK]`** `_tx_heartbeat_elapsed()` now measures from `_tx_commit_ms`
  (last commit), not `_tx_last_ms` (last ack), so the cadence stays steady when unacked.
- [x] **`[EMU-CLOCK-RAW32]` / `[DILATED-CLOCK-WRAP]`** the emulator context now uses
  `Utils::Millis32` + a `_now()` helper (wrap-safe subtraction), mirroring the handler.
- [x] **`[JOIN-RACE-ON-RESET]`** added an `_emu_stopped()` ack flag: the thread sets it as
  its last act; `stop_emulator_thread()` waits on it before joining, so the handoff doesn't
  assume the caller is the main loop.
- [x] **`[SLEEP-MS-OVERFLOW]`** sim `sleep_ms` computes `ms*1000` in `uint64`.
- [x] **`[SEQ-WRAP-SENTINEL]`** fixed with a reader-side `bool _have_seq` flag instead of
  `_last_seq == 0` - the uint32 gap math is ALREADY modular-wrap-correct, only the sentinel
  was the bug. NOTE: chose this over **64-bit seqnums** deliberately - 64-bit would change
  `LogEntry_t` (uint64 forces 8-byte alignment -> a 16-byte header the PLC must mirror), a
  wire ABI change for no extra correctness. DECIDED: keep `_have_seq` - 64-bit only buys an
  absolute counter (no near-term need), and absolute log identity (which would also need a
  64-bit timestamp) is better settled as one coordinated PLC-contract change if/when MQTT
  log export is built.
- [x] **`[DROP-COUNT-CAP]`** a gap > ring size is clamped to the ring size and reported as
  ">=N" (a lap; true count unknowable), so one corrupt/lapped seqNum can't poison `_dropped`.
- [ ] Docs/notes (no code): `[ORDERED-READ-ACQUIRE-LATE]`, `[SIM-DSB-OVERKILL]`,
  `[RX-TX-INTRA-STEP-COUPLING]`, `[TX-NO-TORN-DIAG]` (keep single-buffer + handshake;
  a seqlock, not double-buffering, if detection is ever wanted), `[CTX-RAW-CLOCK-DEREF]`,
  `[STATUS-STRICT-GT]` (host/emulator consistent), `[BATCHED-LOG-DELIVERY]` /
  `[TORN-LOG-SNAPSHOT]`, `[STALE-WRITEINDEX-DOC]` (reader consumes a local snapshot -
  behaviour correct, fix the comments).

---

## Block 2 - Scheduler / stack / timing

- [x] **`[STACK-BISECT-MONOTONE]` (+ `[STACK-SCAN-LIMIT-1MB]` / `[PAINT-GAP-UNINIT]`)**
  bisection replaced. **OPTA now reads the RTOS stack watermark** (`osThreadGetStackSpace`)
  - the old `_estack`/`__StackLimit` is a ~1KB MSP stack, NOT the RTOS thread the firmware
  runs on, and `get_stack_depth()` was returning used-at-setup, not the region size. The
  **sim** keeps painting but scans the painted region top-down for the first run of intact
  sentinel words (sound, vs the non-monotone bisection); scan range == paint range
  (`PAINT-GAP` fixed); `STACK_SCAN_LIMIT` 1 MiB -> 64 KiB (`SCAN-LIMIT`). New
  `HAL::stack_high_water_bytes()`. NOTE: OPTA's watermark is all-time (monotonic), so the
  per-slice "recent" window degenerates to the all-time peak there (documented in code).
  **Block 2.5 follow-up:** since the high-water IS a max, dropped the EMA + rolling-max
  window for the stack entirely - now just the all-time peak (bytes + % of depth). Removed
  `stack_usage_ema`/`stack_bytes_recent` (now `stack_usage`), the `_stack_usage` EMA,
  `_stack_slices`, and the dead EMA config; consumers (health/status/terminal/shell) updated.
- [x] **`[REPAINT-MARGIN-FIXED]`** ceiling now captured from a `&local` *inside*
  `_paint_below` (no caller-frame dependency). NOTE: the high-severity *production* concern
  is **moot** - OPTA no longer paints at all (RTOS watermark), so there's no live frame to
  clobber on hardware. Sim painting keeps a small margin below `&local` (clears this frame
  + memset) - a magic-margin-but-sim-only-test-code tradeoff; an inline fill clobbered its
  own spilled locals at -O0, so `memset` (bounds captured once) is the correct primitive.
- [x] **`[WINDOWEDMAX-NO-DECAY-IDLE]`** `WindowedMax::push(v, weight=1)` extended: a
  skipped-beat catch-up passes `weight = 1 + missed_beats`, so the window ages by elapsed
  beats (wall-time) instead of by released-tick count - a stale peak can't linger past the
  window during an overrun. Rotation bounded to K (huge weight clamps, no runaway). The
  `weight=1` default keeps every existing caller/test valid; added a weighted-push unit test.
- [x] **`[EMA-FLOAT-OK-DOUBLE-LEAK]`** `-Wdouble-promotion` **scoped to `scheduler.cpp`** (the
  hot-path TU) via a source COMPILE_FLAGS property - global would flag the benign
  float->double promotions in display formatting (`format_float` takes a double) and break
  the `-Werror` strict build. Verified scheduler.cpp is clean under it on sim + OPTA.
- [x] **`[TPS-SIM-SAMPLED-DILATED]`** the metric label is now `RTPS` (achieved real ticks/s);
  pyte `normalize.py` regex updated to match.
- [x] **`[TPS-ANCHOR-ZERO-SENTINEL]`** uses a `bool _tps_open` flag, not `anchor==0`.
- [x] **`[OVERRUN-LOG-WRAPSAFE]`** commented that the overrun-log throttle compares 64-bit
  uptime (not the 32-bit timestamp_ms), so it's not the wrap-safe-32 idiom.
- [ ] `[LOAD-WINDOW-SPANS-SKIP]` - keep (skipped beats were genuinely busy; consistent
  with accepting >100% duty).

---

## Block 3 - Logging

- [x] **`[SUPPRESS-REPORT-FANOUT-LOSS]`** the writer FIFO reserves the last
  `LOG_QUEUE_CRITICAL_RESERVE` (4) slots for `LOG_CRITICAL`: normals drop once they'd
  intrude, criticals seat into the reserve WITHOUT evicting, so the sink-loss report
  (itself a critical) no longer manufactures the drop it reports. Last-resort eviction
  only on a completely full queue. Updated the round-1 eviction unit test to the new semantics.
- [x] **`[CRITICAL-RATELIMITED]`** code comment in `push_entry` states the two-stage policy
  (rate limiter may drop a critical as a flood backstop + count/report it; the writer FIFO
  never drops one). No behaviour change.
- [x] **`[FILL-AMOUNT-TRUNC]` (+ `[REFILL-MULTIPLY-OVERFLOW]`)** refill the bucket by
  `min(amount, burst)` (the bucket saturates at burst anyway) so the uint16 `_fill_bucket`
  arg can't truncate a long gap; the anchor still advances by the full elapsed periods
  (bounded by the wrap-safe gap, so no multiply overflow).
- [x] **`[SUPPRESS-NONE-NODRAIN]`** the per-topic fill + report loops start at index 1
  (slot 0 = NONE, never consumed).
- [x] **`[FORMATTED-VS-MESSAGE-LEN]` / `[FORMAT-RETURN-EQ-LEN]`** `SerialLogWriter::format`
  formats into `len-1` so there is ALWAYS room for the trailing newline (no exact-fill merge).
- [x] **`[TX-WATCH-SINGLE]`** `watch_tx` now takes up to two serials (console + monitor);
  `step()` sums their `tx_dropped`. Firmware watches both.
- [x] **`[FILLTIME-INIT-RACE]`** the ctor samples `current_ms()` once into all three anchors.
- [x] **(folded in) stack-usage display:** the absolute value is now the *minimum free
  bytes ever* (worst-case headroom = depth - peak used), the directly safety-relevant
  number, not peak-used. `stack_bytes_max` -> `stack_bytes_free`; status/shell labels +
  pyte regex updated.

---

## Block 4 - Terminal / pages

- [x] **`[PAINT-SHORTWRITE]` / `[STATIC-SHORTWRITE]` / `[CLOCK-FLOOR-COUPLING]`** the cold
  floor now reserves `MAX_FIELD_TX_BYTES` (256, a worst-case 32-col field) +
  `static_assert(tx_capacity() > MAX_FIELD_TX_BYTES + 8)`, so `tx_below_cold()` true
  guarantees a paced field/label/clock fits - it can never truncate. The ignored
  per-run return is now safe.
- [ ] **`[DUP-PACED-PAINTER]`** extract one paced-field painter (reserve -> emit ->
  cache-what-wrote -> budget) shared by the status bar and MetricPage. Target a net
  LoC reduction; subsumes `[STATIC-ORDINAL-ROWGUARD]` / `[EMIT-DEFERRED-NORESET]`.
  **DEFERRED**: this is a maintainability refactor (the `PAINT-SHORTWRITE` correctness
  fix already landed via the guaranteed floor in 4A), and it reworks two delicate
  rendering paths whose regression net is the pyte golden suite - which is env-divergent
  in this sandbox. Best done as a dedicated pass where pyte is runnable.
- [x] **`[EOL-FLAG-LOST]`** `_hw_at_eol` is now saved/restored alongside the other three
  shadows in all three passes (metrics/clock/alarm), so an interleaved bar repaint can't
  eat a page's last-column glyph.
- [x] **`[SLOT-RANGE-UNGUARDED]`** DEBUG `BOTANIK_ASSERT(slot < CACHE_SLOTS)` in `_field`.
- [x] **`[FIELD-WIDTH-TRUNCATE]`** documented the hard 31-col (CACHE_COLS-1) cell limit.
- [x] **`[METRICS-NO-PLC-GAP]`** `_build_metrics` shows a `PLC:---` placeholder instead of
  returning 0 (blank field) when `_ctx.plc` is null.
- [x] **`[SGR-PCT-VIDEO]`** the duplicated magic column ranges collapsed into one
  `metric_value_col()` helper tied to the `_build_metrics` format.
- [~] **`[ALARMS-FIXED-80]`** AlarmsPage already derives its description width from
  `cols()` (`_desc_w()`). The fixed-80 **Status grid** column origins are **deferred** -
  reworking them to `cols()`-relative is a layout change with pyte-golden impact, better
  done with the status-page rework (construction hard-codes 80, so it's safe today).
- [x] **`[ALARMS-SORT-COST]`** documented: the sort runs once per repaint (gated on a set
  change = effectively recompute-on-change), and `[ALARM-NAMESPACE]` bounds `nvis` to the
  real count, so the O(n^2) pass is well within budget - no cache restructure needed.
- [x]/[~] **`[HOME-VENT-PARTIAL]`** done - the Home summary now aggregates the four
  window/blower zones (most-open / highest), not just zone A. **`[HOME-BG-BACKLOG]`** /
  **`[HOME-TINY-SCREEN]`** **deferred** to the Home-page rework (the background-drain and
  tiny-screen-band changes touch the live-tail render path; low severity, defer with it).
- [x] **`[WRAP-MULTIBYTE]` / `[NARROW-WRAP-FANOUT]`** a continuation row drops its prefix
  when it'd leave < CONT_MIN (4) for content (no one-char-per-row fanout); documented the
  ASCII-only byte == column assumption.
- [x] **`[STATUS-RAIN-AGE]`** `rain_age_s` widened to `uint32` (moved after `wind_peak_kmh`,
  absorbed existing padding - no PlcOutput size-pin change, stays word-multiple). Sim cast +
  the misleading "999 y" comment fixed.
- [x] **`[DRAINTAIL-DOC]`** corrected the `drain_tail` comment to `LOG_ROWS_PER_TICK`.
- [x] Parser/editor: **`[ALT0-ENUM-ORDER-COUPLING]`** static_assert on ALT_1..ALT_9
  contiguity; **`[COUNT-SATURATION]`** saturating `_pending_cmd_count`; **`[UTF8-COLWIDTH]`**
  documented ASCII-only (byte == column); **`[BATCH-TIMEOUT-DRAIN-DEP]`** a held command now
  flushes at the shorter `ANSI_CMD_FLUSH_TIMEOUT_MS` (250 ms), not the 1 s text-batch
  timeout; **`[CSI-INTERMEDIATE-ABORT]` / `[ESC-TIMEOUT-LATENCY]`** documented; **`[DEAD-ESC-MAP]`
  / `[SEP-DUP-MAP]` / `[INSERT-CAP-OFFBYONE-OK]`** commented. **`[PENDING-CMD-STATE-GATE]`**:
  documented the rare split-escape interleave rather than restructuring delicate parser FSM
  state for a low-likelihood case (escapes arrive atomically; the 100 ms timeout bounds it).

---

## Block 5 - Shell / argparse

- [x] **`[NO-ARG-PARSE]` (+ `[TWOWORD-EXACT]`)** added bounded NUL-free argparse helpers
  to `utils/text.h` (`arg_next` / `arg_eq` / `parse_uint`, no libc `strtol`). Dispatch now
  matches the first word and passes the argument slice; `logs` parses its own `on`/`off`
  (the `logs on`/`logs off` exact-match entries are gone).
- [~] **`[MISSING-COMMANDS]`** added `uptime` and `plc` (link status from the context).
  **Deferred** (need new APIs first): `alarm ack`/`alarm list` (an AlarmModel ack mutation),
  `loglevel` (a Logger level control), `net`/`ntp` (the network subsystem), guarded
  `reboot` (a confirm flow; `reset` exists), `mem` (no heap / free-RAM HAL primitive).
- [x] **`[LEADING-WS-NOMATCH]`** `arg_next` skips leading whitespace, so a leading-space
  command line now matches.
- [x] **`[HELP-DRIFT]` (+ `[HELP-NAME-TRUNC]` / `[VERSION-BUF-80]`)** `help` is generated
  line-by-line from the (now help-bearing) built-in + site tables (`%-10s` is a minimum,
  not a cap, so a long name isn't truncated); `version` buffer sized from LOG_FORMATTED_LEN.
- [x] **`[SETCMD-NO-CAP]`** `set_commands` clamps to `SHELL_COMMANDS_MAX`; documented that a
  site name colliding with a built-in is silently dead (built-ins match first, by design).
- [x] **`[STATS-UNPACED-BURST]`** commented the per-tick (LOG_ROWS_PER_TICK) drain cadence.
- [ ] `[HISTORY-KILLRING-READY]` - **deferred**; add the editor mutators + draft/ring
  storage when the TODO is taken up.

---

## Block 6 - Composition / build / config (beyond `[CHAINUP]`)

- [x] **`[OUTPUT-DATA-PTR-CAPTURE]`** `output_data()` documented as a stable interior
  pointer (readers cache it; the RX buffer must never relocate).
- [x] **`[CMAKE-SITE-GLOB-NONDETERMINISTIC]`** `CONFIGURE_DEPENDS` added to the lib and
  both site globs (a new source now triggers a reconfigure).
- [x] **`[CMAKE-CONFIG-ABI-GUARD-MISSING]`** commented: a TU including
  `libbotanik_config.h` must link `botanik` to pick up the PUBLIC define transitively
  (else it silently compiles against un-overridden defaults).
- [x] **`[BAUD-CONST-DUPLICATED]`** added `BOTANIK_CONSOLE_BAUD` (default 115200); the OPTA
  entry uses `Config::CONSOLE_BAUD`, the sim keeps 4800 with a comment on the deliberate
  slow-baud override (exercises the TX pacing).
- [x] **`[ASSERT-LOGGER-GLOBAL-SINGLETON]`** noted the assert logger is a single
  process-global (last setup() wins) - fine for the single-instance model.
- [ ] `[NETWORK-STUB-UNWIRED-NULL-CTX]` / `[NETWORK-CONFIG-FIELD-MISSING]` - **deferred**;
  fix the ctor injection contract and design the `PlcOutputConfig_t` field with the PLC
  program before the handlers are implemented.

---

## Block 7 - Serial / text / libc reduction

- [x] **`[TX-DROPPED-WRAP]`** `_tx_dropped` is now saturating (clamps at UINT32_MAX).
- [x] **`[RX-OVERRUN-SILENT]`** added a saturating `rx_overrun` counter (receive() cycles
  aborted with a full RX ring = possible device-FIFO loss), forwarded through the terminal
  and shown on a new `stats` "Console serial" line (TX dropped / RX overruns).
- [x] **`[TRANSMIT-DSB-PLACEMENT]`** dropped the per-byte `dsb()` from all the ring loops:
  the ring is single-thread (main-loop-only, documented as not-ISR-safe), so program order
  alone keeps it consistent - no barrier needed. `volatile` head/tail stay as an intent
  marker, and the class doc now spells out the rationale (a future ISR rewiring re-adds them).
- [x]/[~] **`[ENV-STRTOL-LIBC]`** `env_u8` now parses via the in-house `parse_uint` (drops
  `strtol`, its only user - real libc reduction). **`[FORMAT-AGE-SNPRINTF]` deferred**:
  `snprintf` stays linked everywhere else, so replacing one call isn't a binary reduction
  yet - do it when `snprintf` is eliminated wholesale (`[TODO-LIBC]`).
- [x] **`[FORMAT-FLOAT-WIDTH-U8]`** documented: the effective width cap is the build
  buffer (`sizeof(rev)-1`, ~39); the existing guard clamps a larger request, no overrun.
- [x] **`[RX-CAPACITY-255]`** capacity docs corrected (usable depth is size-1: 255 / 4095,
  one slot reserved to distinguish full from empty).

---

## Block 8 - Docs

- [x] **`[DOC-TIMING-MISMATCH]`** CLAUDE.md now cites the real symbols/values: heartbeat
  `PLC_HEARTBEAT_MS` 250 ms, timeout `PLC_TIMEOUT_MS` 2.5 s, sw watchdog `PLC_WATCHDOG_MS`
  5 s, then the `PLC_FAULT_GRACE_MS` 15 s grace before the `HW_WATCHDOG_MS` 10 s HW
  watchdog resets both cores. Added a `health/` subsystem blurb + the serial barrier note.
- [x] **`[CYCLE-CONTRACT-DRIFT]`** CONCURRENCY.md's cycle contract now includes the
  `health -> on_loop` steps (matching `BotanikFirmware::loop()`), with where/why they sit.
  **`[LOOP-TIME-BASE-SPLIT]`** resolved as a **doc fix, not a code change**: the entire
  health/alarm/on_loop path is uint64 `uptime_ms` throughout (`_fault_since_ms`,
  `_model.step`, `on_loop`); switching it to the 32-bit `timestamp_ms` would push 32-bit
  wrap handling into timers that are deliberately wrap-free, i.e. *worse*. Documented the
  two-bases-one-clock split (32-bit display vs 64-bit health timers) in CONCURRENCY.md and
  the `loop()` comment instead.
- [x] Folded the Block-1 confirmed-design notes into `PLC_SYNC.md` (a new "Confirmed
  design decisions" section: ack baseline, 32-bit-not-64-bit seqnums, no-TX-torn-diag /
  seqlock-not-double-buffer, reader-snapshot correctness, sim conservative no-op barriers).

---

## Deferred / keep-as-is

- Deferred to later work: `[HISTORY-KILLRING-READY]`, `[NETWORK-*]`,
  `[NETWORK-CONFIG-FIELD-MISSING]`, `[NOTIFIER-NULL-OK]` (latch `notified` on a
  successful `publish()` once the real MQTT client replaces the stub).
- Keep as-is (intended / confirmed): `[BOOT-ACTIVE-NOTIFY]` (announce pre-existing),
  `[DUTY-OVER-100]` (>100% duty is a real signal), `[LOAD-WINDOW-SPANS-SKIP]`,
  `[CURRENT-US-WRAP-DILATED]`, `[OUTQ-FIFO-DRIFT]`, `[TX-DROPPED-OVERCOUNT]`, and the
  `[PEEK-NO-LEN]` / `[POWER-OF-TWO-MASK]` / `[TX-DISCARD-TAIL-RACE]` no-action nits.
