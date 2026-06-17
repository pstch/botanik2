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

- [ ] `[ENUM-COUNT]` Bound the bucket array and scan loops by the real topic
  count. Add `static constexpr uint8_t LOG_TOPIC_COUNT = STATUS + 1;` beside the
  `LogTopic` enum (`plcio/vars.h`) with `static_assert(LOG_TOPIC_COUNT <=
  Config::LOG_TOPIC_MAX)`. Size `_perTopic_buckets[]` and the `step()` loops by
  it (drops ~2 KB SRAM + a 255-iteration double scan every tick; keep
  `LOG_TOPIC_MAX` as the wire-range validator).

- [ ] `[REPORT-SLOT]` Make critical messages undroppable: never-drop
  `LOG_CRITICAL` at the writer, overwriting the oldest FIFO entry instead of
  dropping the new one (`logs/writer.cpp`).

- [ ] `[REPORT-EDGE]` Add edge-triggered, holdoff-throttled reporting for the
  three silent-loss categories, each with its own minimum interval: **first
  event reported instantly, subsequent ones only after the interval**.
  - Sink-level drops (`BufferedLogWriter::_dropped`/`_failed`).
  - TX-full truncation (`utils/serial.cpp` short writes - count and report).
  - Rate-limiter suppressions (existing path, fold into the same model).
  - Classify these meta-reports as `LOG_CRITICAL` so `[REPORT-SLOT]` guarantees
    delivery (a dedicated topic alone only escapes the rate limiter, not a full
    FIFO).
  - Distinct interval constants in `libbotanik_config.h`.

---

## Block 3 - Terminal / pages

- [ ] `[STATUS-CACHE]` Don't mark status-bar cells drawn on a truncated write.
  `emit_field_run` should return cells actually written (from `write_chars`);
  `update_status_metrics` (`terminal.cpp:600`) and `update_status_clock`
  (`terminal.cpp:640`) cache only that many. Tighten the `run + 16` reserve to
  the true worst case (`move + SGR + run`). (Latent today: TX gates mean full
  writes except if the remote hangs - fix anyway.)

- [ ] `[CSI-SAT]` Saturate CSI numeric param accumulation instead of wrapping
  `uint16_t` (`parser.cpp:148`).

- [ ] `[HEADER-96]` Add `Config::TERMINAL_MAX_COLS` (default 256) and chunk the
  reverse-video fill so a terminal wider than the local buffer paints fully
  (`home.h:104`). Keep the space-fill (not `ESC[K`) for Minitel safety.

- [ ] `[CTRLC-RESET]` Route Ctrl+C through the page: `handle_cancel()` returns
  `bool`. If a page handled it, stop; if not, `AnsiTerminal` applies the default.
  Remove the direct `HAL::shutdown()` calls from the pages
  (`metric_page.h`, `status.h`, `home.h`, `logs.cpp`). **Hardware default for an
  unhandled Ctrl+C is a no-op**; on the simulator it stays the
  SIGINT/quit-the-process path.

- [ ] `[WRAP-GUARD]` Guard the narrow-terminal underflows: `store_wrapped`
  segment budget when `cols <= plen` (`line_buffer.h:71`) and the echo
  `cap = cols - plen` (`shell.cpp:263`). Clamp `prefix_len`/`cap` to 0 when the
  terminal is narrower than the prefix.

- [ ] `[FACTOR-INGEST]` Add `Scrollback::ingest(buf, len, cols, prefix,
  prefix_len)` = `store_wrapped` + `note_pending`, and collapse the near-identical
  `write()` bodies in `pages/logs.cpp`, `pages/home.h`, `pages/shell.cpp` to a
  one-line delegation.

- [ ] `[DROP-PAGE-RESTORE]` Remove the page-index-restore feature for target
  parity (persistence is by design the PLC's Modbus params, not our flash):
  drop `HAL::save_active_page`, the `BOTANIK_PAGE` env read (`main.cpp:324`), and
  the simulator implementation.

---

## Block 4 - Assertions, utils, cleanup

- [ ] `[ASSERT-HW]` Add `src/libbotanik_asserts.h` with `BOTANIK_ASSERT`,
  compiled out in release. In debug builds, gate the trap on a debugger being
  attached - unified with the watchdog policy introduced in `f0dd653`
  ("trap under a debugger, otherwise take the production action"):
  `log(ASSERT, ...); if (HAL::under_debugger()) HAL::debug_break(); else <action>;`
  - Hardware non-debugger action: **log + continue**.
  - Simulator non-debugger action: **abort()** (loud failure in a headless/CI
    run instead of a re-exec loop).
  - `[UNDER-DBG]` Add `HAL::under_debugger()` to the HAL surface: lift the
    existing `_under_debugger()` (`hal/simulator.h`, /proc/self/status TracerPid)
    to the public interface; OPTA impl =
    `(CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0`.
  - Log under a dedicated `ASSERT` topic.
  - `[ASSERT-TOPIC]` Add `ASSERT` to the `LogTopic` enum (new value `6`,
    C-side only; PLC never emits it) and bump `LOG_TOPIC_COUNT` to 7; update the
    layout static_asserts together.

- [ ] `[ASSERT-USE]` Sprinkle `BOTANIK_ASSERT` on the load-bearing invariants
  surfaced by the review: `_hw_pos.row < _rows` / col bounds (terminal), PLC
  shared pointers non-null at boot (`firmware.cpp:20-22`), `sizeof(T) % 4 == 0`
  (already a static_assert via `[C1]`), `CACHE_COLS <= 32` (metric_page), page
  slot-collision bounds.
  - Safety note: `HAL::debug_break()` (`__BKPT`) is a HardFault on OPTA when no
    debugger is attached, so any assert/trap path that reaches it on hardware
    MUST be gated by `HAL::under_debugger()` (see `[ASSERT-HW]`/`[UNDER-DBG]`).

- [ ] `[ARCH-CONTEXT]` Tighten coupling: hand consumers `const uint32_t*` for the
  shared loop timestamp (only the scheduler writes it), and add defensive
  null-guards where the terminal dereferences `SystemContext` members.

- [ ] `[CLOCKS-STRONGTYPE]` Introduce a lightweight strong-typedef for the
  wrap-prone 32-bit PLC timestamp whose only legal operation is wrap-safe
  subtraction, so it can't be mixed with the monotonic 64-bit uptime. Forced by
  the PLC interface (it only gives us 32-bit timestamps).

- [ ] `[WALLCLOCK-SET]` Add `HAL::wall_clock_set()` now (no-op on the simulator)
  so the future NTP `TimeSyncHandler` has a primitive to discipline the RTC.

- [ ] `[FORMAT-AGE]` `format_age` (`utils/text.cpp:82`) must show years past
  365 days so the field never grows past its fixed 9-char width over multi-year
  uptime.

- [ ] `[STACK-DIV0]` Ensure a zero stack-depth measurement (introspection
  unavailable -> 0) never reaches a division - audit the load/usage math in
  `scheduler.cpp`. A zero reading is acceptable; a divide-by-zero is not.

- [ ] `[SNPRINTF-ACC]` Clamp the `n += snprintf(...)` alert accumulation in
  `main.cpp:234-239` (and any sibling pattern) so a truncating `snprintf` can't
  hand the next call a wrapped (huge) size. Unreachable today, but the unsafe
  shape should go.

- [ ] `[CMD-TABLE]` Replace the hand-rolled `if/else strncmp` shell dispatch
  chain (`shell.cpp:48`) with a small `static const` command table (name + len +
  handler) iterated in a loop - removes the magic length literals.

- [ ] `[TS-DOC]` Document that `scheduler.h:77` publishes a 32-bit `timestamp_ms`
  that wraps every ~49 days, and that all consumers must use wrap-safe
  `(uint32_t)(now - then)` subtraction (the convention we already follow).

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
