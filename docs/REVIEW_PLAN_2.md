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

- [ ] **`[TX-WAIT-STALE-ACK]` / `[FIRST-CONTACT-RXSEQ]`** define an explicit
  first-contact baseline: don't trust uninitialized/stale PLC counters; require an
  observed seq transition (or a handshake reset) before declaring the link RUNNING
  and feeding the HW watchdog.
- [x] **`[SIM-DILATION-OVERFLOW]`** `_now_as()` now does the scale multiply in `__int128`
  over nanoseconds (sim is x86-64 only), keeping full precision and overflow-safe to
  astronomical timepoints. Verified at `TIME_SCALE=1000`.
- [x] **`[CACHE-INVALIDATE-EVICT]`** `@warning` documents the real invariant (invalidate
  only the read-only PLC-output region, clean the input region; sub-region calls round to
  that region's own lines). NOTE: a blanket "32-byte aligned" *assert* proved too strict -
  `logs.cpp` legitimately invalidates the log ring at offset 8 of the read-only `_plcout`
  (safe, nothing dirty to lose), so I dropped the runtime assert and kept the doc. Open Q:
  switch the discipline to whole-region invalidates so an assert *could* stand? (see report)
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
- [ ] **`[HEARTBEAT-FROM-ACK]`** gate the idle heartbeat off last *commit*, not last *ack*.
- [ ] **`[EMU-CLOCK-RAW32]` / `[DILATED-CLOCK-WRAP]`** adopt `Millis32` in the emulator.
- [ ] **`[JOIN-RACE-ON-RESET]`** add an `emu_stopped` ack flag the joiner waits on
  (sim-only, cheap) to drop the "third caller" assumption.
- [x] **`[SLEEP-MS-OVERFLOW]`** sim `sleep_ms` computes `ms*1000` in `uint64`.
- [ ] **`[SEQ-WRAP-SENTINEL]`** 64-bit PLC log seqnums (removes the wrap-at-2^32 case).
- [ ] **`[DROP-COUNT-CAP]`** clamp the reported/accumulated drop gap (> buffer size => "lap").
- [ ] Docs/notes (no code): `[ORDERED-READ-ACQUIRE-LATE]`, `[SIM-DSB-OVERKILL]`,
  `[RX-TX-INTRA-STEP-COUPLING]`, `[TX-NO-TORN-DIAG]` (keep single-buffer + handshake;
  a seqlock, not double-buffering, if detection is ever wanted), `[CTX-RAW-CLOCK-DEREF]`,
  `[STATUS-STRICT-GT]` (host/emulator consistent), `[BATCHED-LOG-DELIVERY]` /
  `[TORN-LOG-SNAPSHOT]`, `[STALE-WRITEINDEX-DOC]` (reader consumes a local snapshot -
  behaviour correct, fix the comments).

---

## Block 2 - Scheduler / stack / timing

- [ ] **`[STACK-BISECT-MONOTONE]` (+ `[STACK-SCAN-LIMIT-1MB]` / `[PAINT-GAP-UNINIT]`)**
  replace the bisection with a multibyte-sentinel, linear top-down scan over a search
  range that exactly matches the painted range. Stays cheap (gated to slice boundaries;
  scans only used depth, word-at-a-time; paint cost unchanged).
- [ ] **`[REPAINT-MARGIN-FIXED]` (high - stack-safety, not a nit)** remove the
  hand-tuned `MARGIN=256` and paint strictly below a `&local` captured *inside*
  `_paint_below`, so the painted ceiling can never overrun a live frame regardless of
  how inlining grows the frame. A wrong margin silently clobbers live stack.
- [ ] **`[WINDOWEDMAX-NO-DECAY-IDLE]`** rotate the recent-peak windows by wall-time,
  not by released-tick count, so a stale peak can't linger past the window during an overrun.
- [ ] **`[EMA-FLOAT-OK-DOUBLE-LEAK]`** add `-Wdouble-promotion` to keep the M4 path float-clean.
- [ ] **`[TPS-SIM-SAMPLED-DILATED]`** label the metric `RTPS` (achieved real ticks/s).
- [ ] **`[TPS-ANCHOR-ZERO-SENTINEL]`** use a bool flag, not `anchor==0`.
- [ ] **`[REPAINT-MARGIN-FIXED]`** capture `&local` inside `_paint_below` instead of a
  fixed 256 margin; **`[OVERRUN-LOG-WRAPSAFE]`** comment that the compare is 64-bit.
- [ ] `[LOAD-WINDOW-SPANS-SKIP]` - keep (skipped beats were genuinely busy; consistent
  with accepting >100% duty).

---

## Block 3 - Logging

- [ ] **`[SUPPRESS-REPORT-FANOUT-LOSS]`** reserve K writer-FIFO slots for `LOG_CRITICAL`
  (normals fill to `SIZE-K` then drop; criticals seat into the reserve without eviction,
  so the sink-loss report stops manufacturing the drop it reports). K ~ 2-4.
- [ ] **`[CRITICAL-RATELIMITED]`** docs only: state the two-stage policy (rate limiter
  may drop a critical as a flood backstop; the writer FIFO never drops one).
- [ ] **`[FILL-AMOUNT-TRUNC]` (+ `[REFILL-MULTIPLY-OVERFLOW]`)** clamp refill `amount`
  to `burst` before `_fill_bucket` (also bounds the anchor-advance multiply).
- [ ] **`[SUPPRESS-NONE-NODRAIN]`** start the per-topic fill/scan at index 1 (slot 0 = NONE, inert).
- [ ] **`[FORMATTED-VS-MESSAGE-LEN]` / `[FORMAT-RETURN-EQ-LEN]`** reserve one byte for
  the trailing newline so an exact-fill truncation can't merge two log lines.
- [ ] **`[TX-WATCH-SINGLE]`** watch both serials' `tx_dropped` (not just the console).
- [ ] **`[FILLTIME-INIT-RACE]`** sample `current_ms()` once in the ctor.

---

## Block 4 - Terminal / pages

- [ ] **`[PAINT-SHORTWRITE]` / `[STATIC-SHORTWRITE]` / `[CLOCK-FLOOR-COUPLING]`** make
  the cold floor a guarantee: size the reserve to `MAX_FIELD_TX_BYTES` + `static_assert`
  against capacity, so a paced field/label/clock can never truncate.
- [ ] **`[DUP-PACED-PAINTER]`** extract one paced-field painter (reserve -> emit ->
  cache-what-wrote -> budget) shared by the status bar and MetricPage. Target a net
  LoC reduction; subsumes `[STATIC-ORDINAL-ROWGUARD]` / `[EMIT-DEFERRED-NORESET]`.
- [ ] **`[EOL-FLAG-LOST]`** save/restore `_hw_at_eol` alongside the other three shadows
  in the status/clock/alarm passes.
- [ ] **`[SLOT-RANGE-UNGUARDED]`** DEBUG `BOTANIK_ASSERT(slot < CACHE_SLOTS)` in `_field`.
- [ ] **`[FIELD-WIDTH-TRUNCATE]`** document the hard 31-col cell limit.
- [ ] **`[METRICS-NO-PLC-GAP]`** draw a `PLC:---` placeholder when `_ctx.plc` is null.
- [ ] **`[SGR-PCT-VIDEO]`** derive the highlight columns from named offsets shared with `_build_metrics`.
- [ ] **`[ALARMS-FIXED-80]`** derive the AlarmsPage/Status layout from `cols()`.
- [ ] **`[ALARMS-SORT-COST]`** cache the sorted index list; recompute only on a set-change
  (dirty flag from the model). Shrinks further with `[ALARM-NAMESPACE]` (real count, not 64).
- [ ] **`[HOME-VENT-PARTIAL]`** aggregate windows/blowers A-D. **`[HOME-BG-BACKLOG]`**
  drain the Home log-tail into scrollback even when backgrounded. **`[HOME-TINY-SCREEN]`**
  clamp the band height.
- [ ] **`[WRAP-MULTIBYTE]` / `[NARROW-WRAP-FANOUT]`** floor the continuation budget;
  break on a lead-byte boundary if multibyte ever in scope.
- [ ] **`[STATUS-RAIN-AGE]`** widen site `rain_age_s` to `uint32` (payload/ABI change -
  coordinate with the PLC program, keep word-alignment); fix the comment.
- [ ] **`[DRAINTAIL-DOC]`** fix the `LOG_ROWS_PER_TICK` comment.
- [ ] Parser/editor: **`[PENDING-CMD-STATE-GATE]`** (coalesce DEL consistently when
  re-entering IDLE), **`[ALT0-ENUM-ORDER-COUPLING]`** static_assert, **`[COUNT-SATURATION]`**
  guard, **`[UTF8-COLWIDTH]`** document ASCII-only, **`[BATCH-TIMEOUT-DRAIN-DEP]`** shorter
  command-flush timeout, **`[ESC-TIMEOUT-LATENCY]` / `[CSI-INTERMEDIATE-ABORT]`** doc,
  **`[DEAD-ESC-MAP]` / `[SEP-DUP-MAP]` / `[INSERT-CAP-OFFBYONE-OK]`** fix (cosmetic but do it).

---

## Block 5 - Shell / argparse

- [ ] **`[NO-ARG-PARSE]` (+ `[TWOWORD-EXACT]`)** add a bounded shared tokenizer
  (`split_args` + a checked `parse_int` wrapping `strtol`); make `logs` parse its own
  `on`/`off` arg through it.
- [ ] **`[MISSING-COMMANDS]`** add the operator command set: `uptime`, `plc`/`link`
  (status, torn count, last-ack age), `alarm list` / `alarm ack [id|all]`, `loglevel`,
  `net`/`ntp` (placeholder until the network subsystem lands), `reboot` (guarded), `mem`.
  All paced through the scrollback drain, bounded output.
- [ ] **`[LEADING-WS-NOMATCH]`** trim leading whitespace before dispatch.
- [ ] **`[HELP-DRIFT]` (+ `[HELP-NAME-TRUNC]` / `[VERSION-BUF-80]`)** replace the
  columnar help with a line-by-line help generated from the command table.
- [ ] **`[SETCMD-NO-CAP]`** assert a max command count; warn on a site name colliding
  with a built-in.
- [ ] **`[STATS-UNPACED-BURST]`** comment the per-tick drain cadence.
- [ ] `[HISTORY-KILLRING-READY]` - **deferred**; add the editor mutators + draft/ring
  storage when the TODO is taken up.

---

## Block 6 - Composition / build / config (beyond `[CHAINUP]`)

- [ ] **`[OUTPUT-DATA-PTR-CAPTURE]`** document that `output_data()` is a stable interior
  pointer that must never relocate.
- [ ] **`[CMAKE-SITE-GLOB-NONDETERMINISTIC]`** add `CONFIGURE_DEPENDS` to the site/lib globs.
- [ ] **`[CMAKE-CONFIG-ABI-GUARD-MISSING]`** note that any TU including
  `libbotanik_config.h` must link `botanik` (transitive PUBLIC define).
- [ ] **`[BAUD-CONST-DUPLICATED]`** promote console baud to `BOTANIK_CONSOLE_BAUD`.
- [ ] **`[ASSERT-LOGGER-GLOBAL-SINGLETON]`** note the single-instance assumption.
- [ ] `[NETWORK-STUB-UNWIRED-NULL-CTX]` / `[NETWORK-CONFIG-FIELD-MISSING]` - **deferred**;
  fix the ctor injection contract and design the `PlcOutputConfig_t` field with the PLC
  program before the handlers are implemented.

---

## Block 7 - Serial / text / libc reduction

- [ ] **`[TX-DROPPED-WRAP]`** make `_tx_dropped` saturating (monotone, never rolls back).
- [ ] **`[RX-OVERRUN-SILENT]`** add an `rx_overrun` counter (device FIFO loss) and
  surface it on the status page.
- [ ] **`[TRANSMIT-DSB-PLACEMENT]`** hoist the per-byte `dsb()` out of the read/transmit
  loops (or drop it - main-loop-only SPSC).
- [ ] **`[ENV-STRTOL-LIBC]` / `[FORMAT-AGE-SNPRINTF]`** replace with in-house bounded
  helpers (libc reduction, `[TODO-LIBC]`).
- [ ] **`[FORMAT-FLOAT-WIDTH-U8]`** document + assert the effective width cap.
- [ ] **`[RX-CAPACITY-255]`** correct the docs (255/4095 usable, not 256/4096).

---

## Block 8 - Docs

- [ ] **`[DOC-TIMING-MISMATCH]`** reconcile the heartbeat/timeout/watchdog prose
  (1 s/5 s/30 s) with the real config (250 ms/2500 ms/5000 ms); cite the config symbols.
- [ ] **`[CYCLE-CONTRACT-DRIFT]` / `[LOOP-TIME-BASE-SPLIT]`** add the alarms/health step
  to the `CONCURRENCY.md` cycle contract; the alarm/health step reads the beat's shared
  `timestamp_ms` (projected wrap-safe), not a fresh `uptime_ms()`.
- [ ] Fold the Block-1 doc notes into `CONCURRENCY.md` / `PLC_SYNC.md`.

---

## Deferred / keep-as-is

- Deferred to later work: `[HISTORY-KILLRING-READY]`, `[NETWORK-*]`,
  `[NETWORK-CONFIG-FIELD-MISSING]`, `[NOTIFIER-NULL-OK]` (latch `notified` on a
  successful `publish()` once the real MQTT client replaces the stub).
- Keep as-is (intended / confirmed): `[BOOT-ACTIVE-NOTIFY]` (announce pre-existing),
  `[DUTY-OVER-100]` (>100% duty is a real signal), `[LOAD-WINDOW-SPANS-SKIP]`,
  `[CURRENT-US-WRAP-DILATED]`, `[OUTQ-FIFO-DRIFT]`, `[TX-DROPPED-OVERCOUNT]`, and the
  `[PEEK-NO-LEN]` / `[POWER-OF-TWO-MASK]` / `[TX-DISCARD-TAIL-RACE]` no-action nits.
