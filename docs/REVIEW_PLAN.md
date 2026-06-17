# Expert review - action plan

Outcome of the systematic firmware review and the follow-up design discussion.
Each item carries the `[ANCHOR]` we used in the discussion so it stays
referenceable. Ordered by priority: cross-core correctness first, then
scheduling, logging, terminal/pages, then assertions/cleanup, then docs.

Conventions for this plan: no heap, no blocking, no recursion, volatile +
`HAL::dsb()`/`dmb()`/`ordered_*` instead of `std::atomic`, functions fit one
screen, classes stay small, short client-readable comments.

---

## Block 0 - Cross-core correctness (hardware-critical, do first)

- [x] `[C1]` Fix `ordered_copy` word/byte count. `hal/opta.h` passed
  `sizeof(T)` (bytes) where `ARM_MPU_OrderedMemcpy` wants a **word** count, so
  every PLC transfer copied 4x and ran off the end of both buffers on hardware.
  - DONE: now `sizeof(T) / sizeof(uint32_t)`.
  - DONE: `static_assert(std::is_empty<T>::value || sizeof(T) % sizeof(uint32_t)
    == 0)` in `ordered_copy` and `ordered_move`, in **both** backends so the
    normally-compiled sim build enforces the contract too. The `is_empty`
    exemption covers genuinely-empty payloads (copy 0 words).
  - The assert immediately caught a real latent issue: the sim's empty
    `PlcInputData_t {}` is `sizeof 1` (not word-multiple); harmless (no data) and
    now exempted. Site payloads with real data must stay word-multiple.
  - Verified: green on both `build` (SIM) and `build-opta` (OPTA lib).

- [x] `[C2-FIX]` Add an ordered read primitive and stop reading cross-core
  counters through plain loads.
  - DONE: `HAL::dmb()` added (`platform.h`, `opta.h` = `__DMB()`, `simulator.h` =
    fence).
  - DONE: `HAL::ordered_read<T>(const volatile T*)` = volatile load + `dmb()`,
    symmetric to `ordered_move`.
  - DONE: used for the four sync-counter polls (`txSeqNum`, `rxAckNum` in
    `sync.h`; `rxSeqNum`, `txAckNum` in `emulator.h`) and the host's log
    `writeIndex` read in `logs.cpp`. Bulk `data`/`logs` stay non-volatile via
    `ordered_copy`.
  - Note: only ONE cross-core log cursor exists - the emulator is the sole log
    producer (writes its own `writeIndex`, never polls `readIndex`), so the
    "two cursors" estimate was high.
  - Verified: green on both targets, 22 unit tests pass, sim reaches
    "PLC link established".

- [x] `[CACHE]` D-cache coherency for the PLC shared region. We run on the **M7**
  (`__DCACHE_PRESENT`), the region may be cacheable and we cannot rely on the
  proprietary image toolchain mapping it non-cacheable.
  - Add `HAL::cache_clean(addr, len)` and `HAL::cache_invalidate(addr, len)` to
    the HAL: `SCB_CleanDCache_by_Addr` / `SCB_InvalidateDCache_by_Addr` on OPTA,
    **no-op on the simulator**. Guard on `__DCACHE_PRESENT` so a future M4 build
    compiles them away.
  - `[CACHE-ALIGN]` Do **both**: (a) `alignas(32)` + pad the shared structs to a
    32-byte multiple in `plcio/vars.h` (C++ rounds `sizeof` up for free), and
    (b) round the address span out to 32-byte lines **inside** the cache helper.
    Belt-and-suspenders so a partial line can never clobber a neighbour.
  - Discipline (the asymmetry that keeps it safe):
    - **Invalidate the PLC-output region before reading it** (`_rx_memcpy`, and
      the output-side counter polls).
    - **Clean the PLC-input region after writing it** (`_tx_memcpy`,
      `_tx_commit`, `_rx_acknowledge`).
    - Never invalidate a region we write to; read-region and write-region are
      disjoint (`PlcOutput` vs `PlcInput`).
  - DONE: `HAL::cache_clean`/`cache_invalidate` (OPTA `SCB_*DCache_by_Addr`
    guarded on `__DCACHE_PRESENT`, CMSIS rounds to lines; no-op on sim).
    `alignas(32)` on `PlcInput`/`PlcOutput` in `vars.h`. Applied coarsely at the
    component `step()` boundaries: `PlcSyncHandler::step()` invalidates `_plcout`
    first / cleans `_plcin` last; `PlcLogReader::drain()` does the same for the
    log sub-regions with a cheap empty-exit. Updated the `PlcOutput` size pin
    (8648 -> 8672) and `test_sizes.cpp`'s combined-struct invariant (now
    sum-of-parts rounded up to 32).
  - Verified: both targets build (OPTA now compiles `botanik_fw`, exercising the
    SCB calls + handler), 22 tests pass, sim reaches "PLC link established".

- [x] `[C3-FIX]` Torn-read diagnostic (no seqlock, no double buffer). The PLC
  upholds mutual exclusion via the handshake (MOV block, ENABLE low until it
  observes our ACK).
  - DONE: `_rx_memcpy()` now reads `txSeqNum` before the copy (which also serves
    as the acquire), copies, invalidates the sync line, and re-reads `txSeqNum`;
    on a mismatch RX_READ increments `_rx_torn`, logs a `PLC_IO` warning
    (rate-limited by the logger), and returns to RX_IDLE without acking. Exposed
    via `rx_torn_count()`. Diagnostic only, no retry loop.
  - Verified: both targets build, 22 tests pass, sim link establishes with **zero
    torn-read false positives** (the emulator honors the handshake).

- [x] `[C4]` Resilience to corrupted log entries (DMA, no integrity field by
  design). A bad `LogEntry_t` must never crash or wedge us:
  - DONE: `_push_plc_entry` already clamps `length` to the buffer and NUL-
    terminates; the read index is masked `% LOG_PLC_BUFFER_SIZE`, so a corrupt
    cursor can't index out of bounds. Added a clamp on the `snprintf` return in
    `_detect_dropped_entries` so a truncation can't record a length past the
    buffer.
  - DONE `[DRAIN-BOUND]`: the drain loop is now capped at `LOG_PLC_BUFFER_SIZE`
    entries per `step()` (one full ring = the max legitimate backlog), so a
    runaway/corrupt `writeIndex` can't burn the tick budget; real backlog is
    caught next tick.
  - Verified: both targets build, 22 tests pass, sim drains PLC logs normally.

- [x] `[ARCH-CONCURRENCY]` / `[SERIAL-DOC]` Pin down and document the concurrency
  model. Our code is single-threaded except for the **one** async boundary: the
  PLC shared memory (the HW watchdog is fire-and-forget). We have **no serial
  ISRs** in our code - `receive`/`transmit`/`step` are main-loop only.
  - DONE: deleted the incorrect "may be called from an ISR" claims in
    `utils/serial.h` (class doc, `tx_discard` `@warning`, `step` `@note`); marked
    `BufferedSerial` and `BufferedLogWriter` explicitly main-loop-only / not
    ISR-safe.
  - `tx_discard()` is safe as-is (single-store reset, all access main-loop); no
    code change needed once the ISR premise is gone.
  - The repo-level concurrency-model writeup remains `[CONCURRENCY-DOC]` in
    Block 5.
  - Verified: both targets build (comment-only changes).

---

## Block 1 - Scheduler / watchdog / timing

- [x] `[JITTER-CLAMP]` Defend the jitter sample against unsigned underflow.
  DONE: `scheduler.h` now computes `grid_us = _next_tick_ms*1000` and records
  `now_us > grid_us ? now_us-grid_us : 0` (`_next_tick_ms` is `uint64_t`, so the
  multiply is safe; only the subtraction could wrap). One bad sample can no
  longer poison the all-time max.

- [x] `[BUSYWAIT]` Keep the 1 ms busy-wait tail hardcoded (1 ms is the minimum
  sleep granule; the spin is bounded by a monotonic-clock comparison).
  DONE: added class-scope `static_assert(Config::TASK_PERIOD_MS >= 2)` beside
  `_period_ms` so a future retune can't collapse the sleep/spin split.

- [x] `[WATCHDOG-LOG]` When the kick is withheld because the PLC link faulted,
  emit a prominent log instead of a silent reboot.
  - DONE: `faulted()` is exactly `status() == PLC_FAULT`, and that transition is
    already detected edge-triggered in the context's `_log_status_change`. So the
    one clean spot is there: the `PLC_FAULT` case is now `LOG_CRITICAL`
    ("PLC watchdog fault: withholding kick, rebooting"). Shared by both targets,
    won't flood (fires once per transition), and needed no touch to the in-flight
    `firmware.cpp` loop.
  - Open question to revisit later (not in this pass): should a *transient* PLC
    fault really reboot the whole HMI/logging firmware, or should the HMI stay up
    and only surface the fault? Left the current behaviour, just made it loud.

- [x] `[WD-SNAPSHOT]` Harden the simulator `watchdog_poll` (`hal/simulator.h`),
  surfaced reviewing `f0dd653`. DONE: clocks are `uint64_t` (no 49-day wrap),
  `last_kick` is read **before** `uptime` (a concurrent `watchdog_kick()` can no
  longer make `last_kick` newer than `uptime`), and the test collapsed to
  `uptime - last_kick > timeout`. Verified: sim runs the full duration with no
  spurious reset, link establishes.

---

## Block 2 - Logging / flood protection

- [x] `[ENUM-COUNT]` Bound the per-tick scan loops by the real topic count.
  DONE: added `LOG_TOPIC_COUNT` (`= ASSERT + 1` after `[ASSERT-TOPIC]`) beside the
  `LogTopic` enum; the three `step()`/ctor loops and the `push_entry` bound use
  it, so the per-tick double scan went 255 -> 7. Final decision (revised in Block
  4): the bucket *array* stays sized by `LOG_TOPIC_MAX` so the `Logger` footprint
  is fixed regardless of how many topics the enum defines (no size-pin churn when
  a topic is added) - the CPU win is kept, the ~2 KB is traded for a stable
  contract.

- [x] `[REPORT-SLOT]` Make critical messages undroppable: never-drop
  `LOG_CRITICAL` at the writer, overwriting the oldest FIFO entry instead of
  dropping the new one (`logs/writer.cpp`).
  - DONE: refactored `drain()` to consume `_readIndex` at format time, so the
    outgoing `_message` is fully decoupled from the queue; that makes eviction
    safe. `push()` now, on a full queue, evicts the oldest entry to seat a
    `LOG_CRITICAL` (still counts the eviction as a drop); lower levels drop as
    before. Verified: both targets build, 22 tests pass, logs flow normally.

- [x] `[REPORT-EDGE]` Add edge-triggered, holdoff-throttled reporting for the
  silent-loss categories, first event instant then per-category holdoff, all
  classed `LOG_CRITICAL` so `[REPORT-SLOT]` guarantees delivery.
  - DONE - sink-level drops: `LogWriter::loss_count()` (override returns
    `_dropped + _failed`); `Logger::step()` sums it across writers and reports via
    a `LossReporter_t` helper (first-instant + holdoff).
  - DONE - TX-full truncation: `BufferedSerial` counts unwritten bytes
    (`tx_dropped()`); `Logger::watch_tx()` registers the **console** serial (the
    terminal reserves space, so only non-resuming callers actually drop), polled
    in `step()`. Wired in `botanik_sim/main.cpp`; a one-line
    `_logger.watch_tx(&_console_serial)` was also added to the in-flight
    `botanik_fw/firmware.cpp` (left for that file's own commit).
  - DONE - rate-limiter suppressions: kept the existing **per-topic** report
    (more useful than an aggregate) but reclassified it `LOG_CRITICAL` so it
    survives the storm it summarizes. (Granularity call `[Q-RL-GRANULARITY]`:
    kept per-topic.)
  - Distinct interval constants added to `libbotanik_config.h`
    (`LOG_SINK_DROP_REPORT_PERIOD_MS` 5 s, `LOG_TX_DROP_REPORT_PERIOD_MS` 10 s).
  - Verified: both targets build, **25** tests pass incl. 3 new
    `tests/test_logger.cpp` cases (drain-in-order, critical eviction,
    edge-triggered sink report); sim shows no false loss reports.

---

## Block 3 - Terminal / pages

- [x] `[STATUS-CACHE]` Don't mark status-bar cells drawn on a truncated write.
  DONE: `write_chars` now returns bytes-on-wire and `emit_field_run` returns the
  contiguous cells written; `update_status_metrics` and `update_status_clock`
  cache only that many and stop (leaving the rest stale to repaint next refresh)
  on a short write. Reserve tightened `run + 16` -> `run + 18`. Verified: status
  bar (clock + metrics) renders/updates correctly in the sim.

- [x] `[CSI-SAT]` Saturate CSI numeric param accumulation instead of wrapping
  `uint16_t`. DONE: accumulate in a `uint32_t` and clamp to `0xFFFF` (`parser.cpp`).

- [x] `[HEADER-96]` DONE: added `Config::TERMINAL_MAX_COLS` (256) as a sanity
  bound and chunked the reverse-video fill in `draw_log_header` so a terminal
  wider than the local buffer paints fully. Kept the space-fill (Minitel-safe).

- [x] `[CTRLC-RESET]` Route Ctrl+C through the page. DONE: added
  `Page::handle_cancel()` (returns bool, default false). The terminal intercepts
  `CTRL_C`, calls `handle_cancel()`, and on false applies the default: quit under
  `#ifdef TARGET_SIMULATOR`, a **no-op on hardware** (a stray Ctrl+C can't reboot
  the controller). `ShellPage::handle_cancel` cancels the input line (true);
  removed the direct `HAL::shutdown()` from `metric_page.h` and `logs.cpp`.
  Verified: Ctrl+C on the home page cleanly quits the sim (exit 0).

- [x] `[WRAP-GUARD]` Guard the narrow-terminal underflows. DONE: `store_wrapped`
  drops a prefix that can't fit the segment (`prefix_len >= seg_cap -> 0`,
  `line_buffer.h`); the shell echo clamps `cap` to >= 1 when `cols <= plen`
  (`shell.cpp`) so it can't underflow and the loop always makes progress.

- [x] `[FACTOR-INGEST]` DONE: added `Scrollback::ingest(buf, len, cols, prefix,
  plen, active)` (= `store_wrapped` + conditional `note_pending`); the three page
  `write()` bodies in `logs.cpp`, `home.h`, `shell.cpp` collapsed to a one-line
  delegation.

- [x] `[DROP-PAGE-RESTORE]` DONE: removed `HAL::save_active_page` (decl +
  both backends), its call in `switch_to`, and the `BOTANIK_PAGE` env read; both
  targets now boot on the home page after a reset. (env_u8 left as a generic
  util.)

---

## Block 4 - Assertions, utils, cleanup

- [x] `[ASSERT-HW]` DONE: `src/libbotanik_asserts.h` is header-only (avoids a
  CMakeLists edit) - `BOTANIK_ASSERT` compiles out unless `DEBUG`; on failure it
  logs `LOG_CRITICAL` under the `ASSERT` topic (via a `set_assert_logger`-
  registered logger, function-local static keeps it header-only), then
  `if (HAL::under_debugger()) debug_break()` else `abort()` on sim / log+continue
  on hardware. Wired `set_assert_logger(&logger)` in `main.cpp`.
  - DONE `[UNDER-DBG]`: `HAL::under_debugger()` added - sim renamed from
    `_under_debugger` (/proc TracerPid), OPTA = `CoreDebug->DHCSR & C_DEBUGEN_Msk`
    (validated by the OPTA build).
  - DONE `[ASSERT-TOPIC]`: `ASSERT` (=6) added to `LogTopic`, `LOG_TOPIC_COUNT`
    is `ASSERT + 1` (7), `format_log_topic` renders "ASSERT". Note: per the
    user's call the bucket *array* stays sized by `LOG_TOPIC_MAX` (fixed
    footprint) and only the *scan loops* use `LOG_TOPIC_COUNT`, so adding a topic
    no longer churns the `Logger` size pin.

- [x] `[ASSERT-USE]` DONE: PLC shared pointers + timestamp non-null in the
  `PlcSyncHandler` ctor (catches a mis-linked/early PLC region; also gets the
  asserts header OPTA-compiled); `static_assert(CACHE_COLS <= 32 && CACHE_SLOTS
  <= 32)` in `metric_page.h` (the uint32 diff-mask invariant). `sizeof(T)%4==0`
  already covered by `[C1]`; terminal cursor bounds are clamped in `move_to`
  (an input assert would false-positive), and `add_page` already guards its bound.
  - Safety note: `HAL::debug_break()` (`__BKPT`) HardFaults on OPTA with no
    debugger, so the trap is gated on `HAL::under_debugger()`.

- [x] `[ARCH-CONTEXT]` DONE: the shared loop timestamp is now `const uint32_t*`
  through its consumers (parser, terminal, `PlcSyncContext`/handler) so only the
  scheduler can write it - call sites are unchanged (implicit `uint32_t*` ->
  `const uint32_t*`). Added a null-guard in `_build_metrics` where the terminal
  read `_ctx.scheduler->...` (it already guarded `_ctx.plc`). Sim-validated;
  OPTA-clean (the const edits compiled).

- [ ] `[CLOCKS-STRONGTYPE]` Introduce a lightweight strong-typedef for the
  wrap-prone 32-bit PLC timestamp whose only legal operation is wrap-safe
  subtraction, so it can't be mixed with the monotonic 64-bit uptime. Forced by
  the PLC interface (it only gives us 32-bit timestamps).

- [x] `[WALLCLOCK-SET]` DONE: `HAL::wall_clock_set(uint64_t unix_ms)` added -
  no-op on the simulator, a TODO RTC-write stub on OPTA - so the future NTP
  `TimeSyncHandler` has a primitive to discipline the RTC.

- [x] `[FORMAT-AGE]` DONE: `format_age` rolls up to years past 365 days; a uint32
  of seconds tops out at ~136 years, so the value stays <= 3 digits and the field
  keeps its fixed 9-char width.

- [x] `[STACK-DIV0]` DONE (verified, no change needed): the only stack division
  (`scheduler.cpp:239`) already guards `_stack_depth > 0 ? ... : 0.0f`; pages
  display the pre-computed metrics and never divide. A zero reading is safe.

- [x] `[SNPRINTF-ACC]` DONE: the alert composition in `main.cpp` now goes through
  an `add_alert` lambda that clamps the running offset to `sizeof(buf)`, so a
  truncating `snprintf` can't hand the next call a wrapped size.

- [x] `[CMD-TABLE]` DONE: `_builtin` is now a `static const {name, handler}` table
  iterated in a loop (widths from `strlen`, no magic length literals); each
  command is its own small `_cmd_*` method, with `_unknown`/`_println` helpers.
  Verified in the sim: `stats` and dispatch work.

- [x] `[TS-DOC]` DONE: the `timestamp_ms` member doc now carries a `@warning`
  that it's 32-bit (wraps ~49 days) and that consumers must use wrap-safe
  `(uint32_t)(now - then) > delta` subtraction, never `now > then + delta`.

---

## Block 5 - Docs

- [ ] `[ARCH-PIPELINE]` Add a **concise** per-tick cycle-order contract comment at
  the (duplicated, by design) main loop: receive -> plc -> plc_logs -> logger ->
  terminal -> monitor.drain -> transmit -> watchdog-kick, and why the order
  matters. The two composition roots (`firmware.cpp`, `main.cpp`) stay separate.

- [ ] `[CONCURRENCY-DOC]` Write the concurrency model into the docs: single main
  loop, one async boundary (PLC shared memory), no serial ISRs in our code, the
  volatile + barrier + cache-maintenance discipline and its scope.

- [ ] `[PLC-CONTRACT-DOC]` Document the PLC sync contract in `docs/PLC_SYNC.md`:
  single shared buffer + mutual exclusion by handshake (not hardware
  double-buffering), the ENABLE-low-until-ACK rule the PLC side must uphold, and
  the `[C3-FIX]` diagnostic that detects a violation.

- [ ] `[TODO-LIBC]` Add to `TODO.md`: "audit libc usage and reduce dependency on
  libc functions."

- [ ] `[ARCH-HEALTH]` Add to `TODO.md`: future `HealthMonitor` - a fault-state
  aggregator subsystems report into, read by the Status page and (future) the
  TCP fault-alert sender; gives the watchdog a principled health input instead of
  the single hardcoded `faulted()`.

---

## Explicitly out of scope / keep as-is

- The 1 ms busy-wait is **bounded** (monotonic-clock comparison) - the original
  "unbounded CPU" finding was wrong. Keep the loop.
- Defensive constructor `memset`s of the serial ring buffers - keep.
- `LogBucket_t::count` (unread today) - keep, may be used later.
- Redundant `entry.length` on the emit path - keep, defensive.
- No MPU reconfiguration (we don't own it / can't assume the core); cache
  coherency is handled by `[CACHE]` instead.
- No `std::atomic`, no double-buffering of the PLC payload, no seqlock.
