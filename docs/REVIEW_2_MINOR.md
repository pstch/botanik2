# Expert review round 2 - low / nit findings

Minor and deferred findings from the round-2 component review, parked here for
inline response. The major / shape items are being discussed separately and the
accepted fixes are tracked toward the round-2 plan.

Format per item: `[LABEL]` (severity) - file:line - one-line issue.
Add your call under `Response:` (keep / fix / defer / wontfix / discuss).

---

## A. Mediums still needing a decision (not yet triaged)

- `[WINDOWEDMAX-NO-DECAY-IDLE]` (med) - filters.h:94-101 - the recent-peak windows rotate per *released tick*, not per wall-time, so during a long overrun a stale high peak lingers beyond LOAD_METRIC_WINDOW_MS.
  Response: true, this should probably be fixed

- `[STACK-SCAN-LIMIT-1MB]` / `[PAINT-GAP-UNINIT]` (med) - scheduler.cpp:228,262-277 - the search range (from stack_base down) doesn't match the painted range (below caller_sp), so the bisection reads never-painted live-frame memory; folds into the multibyte-sentinel rework (range == painted range).
  Response: yep

- `[SLOT-RANGE-UNGUARDED]` (med) - metric_page.h:249-252,290 - a subclass slot >= CACHE_SLOTS (32) silently falls back to uncached every-tick repaint instead of failing loudly; status page already uses slot 31. Suggest a DEBUG-only BOTANIK_ASSERT(slot < CACHE_SLOTS).
  Response: I didn't understand, please reformulate.

- `[ALARM-MODEL-MUTATES-SITE-TABLE]` (med) - alarms.h:139,143-144 - AlarmModel writes into the caller's g_alarms[] (splice + boot-init), so the "site config table" is silently mutable; folds into the namespace-separation refactor.
  Response: yep

- `[NETWORK-STUB-UNWIRED-NULL-CTX]` (med) - network.cpp:14-27 - NetworkHandler/TimeSyncHandler default _logger/_ctx to null with no injection hook; first step() will null-deref unless ctors gain (PlcSyncContext*, Logger*) like PlcLogReader. Set the contract before code lands.
  Response: yep

- `[PENDING-CMD-STATE-GATE]` (med) - parser.cpp:327-347 - a backspace/Ctrl+D arriving mid-escape-sequence is routed into the FSM instead of the chunk-coalescer and can be dropped rather than coalesced.
  Response: what ? how can this happen ? 

- `[LEADING-WS-NOMATCH]` (med) - shell.cpp:62-84 - a command line with leading whitespace fails every match and falls to _unknown; trim leading spaces before dispatch.
  Response: yep

- `[TWOWORD-EXACT]` (med) - shell.cpp:53-54,64 - "logs on"/"logs off" matched by exact strncmp, so "logs  on" / "logs on " miss; make `logs` parse its own on/off arg.
  Response: yep, this is also related to the shell's commands rework of arg parsing

- `[LATCH-WRAP]` (med, doc) - alarms.h:182-184 - alarm clock runs on 64-bit uptime_ms while the rest of the firmware uses 32-bit Millis32; document the deliberate split (or it reads as a wrap hazard).
  Response: what would be the impact of switching to 32-bit ? I think we need correct dates across 32-bit rollovers so the need for 64-bit is warranted. but maybe 
we could store it differently, like as seconds, so that we can still use 32-bit ?

- `[BATCHED-LOG-DELIVERY]` / `[TORN-LOG-SNAPSHOT]` (med, doc) - sync.h:278-284 - PLC log delivery is coupled to the RX handshake (per-cycle snapshot, not a live ring) and a torn copy of the log region is caught only by the downstream corrupt-entry/seqNum net; document both.
  Response: yep

---

## B. HAL / cross-core

- `[ORDERED-READ-ACQUIRE-LATE]` (low, doc) - opta.h:148-153 - ordered_read's DMB orders accesses *after* the load, not before; one-line doc that it's acquire (later), not a fence for prior reads.
  Response: yep.

- `[CURRENT-US-WRAP-DILATED]` (low) - simulator.h:157-159 - 32-bit current_us() wraps N x sooner under dilation than on HW; document, prefer uptime_us() for intervals.
  Response: that's ok I think. the wrap is supposed to be handled well.

- `[SIM-DSB-OVERKILL]` (low) - simulator.h:193-201 - sim DSB/DMB are both seq_cst fences (safe superset) but can't reproduce ARM barrier-*strength* bugs; note in CONCURRENCY.md.
  Response: what ? why ?

- `[WALLCLOCK-WRAP-2038]` (med->confirm) - opta.h:102-106 - (uint64_t)time(NULL)*1000 wraps in 2038 if time_t is 32-bit on the toolchain; confirm sizeof(time_t), source from RTC if so.
  Response: it WILL wrap in 2038, we need to handle it properly

- `[STACK-DEPTH-SIGN]` (med->confirm) - opta.h:58-62 - get_stack_depth underflows to ~4GB if ever called off the main thread; guard sp <= base or document main-thread-only.
  Response: yes, guard it.

- `[SLEEP-MS-OVERFLOW]` (low) - simulator.h:184-186 - (ms*1000)/scale overflows uint32 for ms > ~71min; compute in uint64. (Scheduler path is period-bounded, so latent.)
  Response: what ? explain it better

- `[RESTRICT-ALIAS]` (low) - opta.h:136 - ordered_copy forwards caller pointers to a __RESTRICT memcpy; document the no-alias precondition (dst != src).
  Response: document it AND assert it

- `[OUTQ-FIFO-DRIFT]` (low) - simulator.h:372-382 - availableForWrite mixes the kernel tty queue with the modelled 64-byte FIFO; minor sim-fidelity gap.
  Response: that's OK.

- `[BKPT-IMM-NONZERO]` / `[EMPTY-STRUCT-SIZEOF1]` (nit) - opta.h:134,176 - empty-payload ordered_move stores 1 byte vs the "word-multiple" assert message; cosmetic.
  Response: hmm I was wondering about that. how can we fix it ?

## C. PLC sync / emulator / plc-logs

- `[RX-TX-INTRA-STEP-COUPLING]` (low) - sync.h:236-237 - RX ack reaches the PLC only after the end-of-step cache clean, so ack latency is up to ~3 ticks; confirm the PLC's ENABLE-low hold tolerates it, document worst-case round-trip.
  Response:  yep

- `[HEARTBEAT-FROM-ACK]` (low) - sync.h:260,322 - idle heartbeat measures from last *ack* not last *commit*, so during an outage it pulses commits every step (seqnum climbs fast); gate off last commit.
  Response: yep

- `[TX-NO-TORN-DIAG]` (low, doc) - sync.h:285-297 - TX integrity rests entirely on the PLC honoring rxSeqNum/rxAckNum; document that no host-side detector is possible without an input-region seqlock.
  Response: hmm, can we use double buffering for this ? would it be worth ?

- `[ALIGNAS-PLC-CONTRACT]` (low) - vars.h:137-150 - add static_assert(sizeof(PlcOutput<Data>) % 32 == 0) so the 32-byte contract is enforced per-site at compile time, not left to the optional sketch asserts.
  Response: yeah

- `[CTX-RAW-CLOCK-DEREF]` (low) - sync.h:86 - faulted()/status() deref the shared 32-bit clock with no barrier; fine under the single-loop invariant, flagging only.
  Response: yeah

- `[STATUS-STRICT-GT]` (nit) - sync.h:105 - thresholds use strict `>` so boundary states persist one extra tick; keep host/emulator consistent.
  Response: what ?

- `[DILATED-CLOCK-WRAP]` / `[EMU-CLOCK-RAW32]` (low) - emulator.h:300-312 - emulator keeps raw uint32 clocks + hand-rolled `a-b>delta`; thin wrap margin under deep dilation and an easy spot to regress `now>then+delta`. Adopt Millis32 or add the [TS-DOC] warning.
  Response: yeah use Millis32

- `[JOIN-RACE-ON-RESET]` (low) - emulator.h:279-289 - stop/join assumes reset() is only entered from the main loop or the emulator thread; document that invariant.
  Response: can't we make it more resistant ?

- `[DROP-COUNT-CAP]` (nit) - logs.cpp:61-69 - a corrupt/lapped seqNum can add a near-2^32 gap to the lifetime _dropped stat; clamp to a sane bound (> buffer size => "lap").
  Response: yeah

- `[HEARTBEAT-SNPRINTF]` (nit) - emulator.h:325 - sim-only, correctly bounded; no action (confirms no portable-build leak).
  Response: ok

## D. Scheduler / filters

- `[LOAD-WINDOW-SPANS-SKIP]` (low) - scheduler.cpp:160 - on a skip, _last_release_us isn't advanced, so the next duty sample measures several real periods against one nominal period (root of the >100% reading you accepted).
  Response: well if we skipped its still time the CPU was busy

- `[EMA-FLOAT-OK-DOUBLE-LEAK]` (low) - filters.h:58, scheduler.cpp:142 - math is float-clean today; consider -Wdouble-promotion to keep it enforced on the M4.
  Response: yes we want this

- `[TPS-SIM-SAMPLED-DILATED]` (low) - scheduler.cpp:174-194 - TPS = ticks(dilated)/real-second; self-consistent, just confirm the label reads "achieved real TPS".
  Response: yeah we can use RTPS to indicate that (no place for the full "real TPS" unit)

- `[TPS-ANCHOR-ZERO-SENTINEL]` (low) - scheduler.cpp:183 - _tps_anchor_ms==0 sentinel collides with a genuine 0 clock for the first ~1ms; use a bool flag.
  Response: yes

- `[OVERRUN-LOG-WRAPSAFE]` / `[REPAINT-MARGIN-FIXED]` (nit) - scheduler.cpp:130,266 - 64-bit comparison reads like a 32-bit wrap bug (add a comment); MARGIN=256 is hand-tuned with no static check.
  Response: what ? 

## E. Logging

- `[REFILL-MULTIPLY-OVERFLOW]` (low) - logger.cpp:66 - amount*period nears uint32 ceiling for a wrap-sized gap; the FILL-AMOUNT-TRUNC clamp (accepted) also closes this.
  Response: ok

- `[SUPPRESS-NONE-NODRAIN]` (low) - logger.cpp:74-98 - the NONE topic slot (0) is filled/scanned every tick but never consumed; start the scan at index 1 or document slot 0 as inert.
  Response: yep

- `[FORMATTED-VS-MESSAGE-LEN]` / `[FORMAT-RETURN-EQ-LEN]` (low) - writer.cpp:44,81 / format.h:66 - a near-full message truncates silently, and on exact-fill truncation the trailing newline is dropped so two log lines merge on the serial sink; reserve one byte for '\n'.
  Response: yep

- `[TX-WATCH-SINGLE]` (nit) - logger.cpp:89 - only the console serial's tx_dropped is watched; document the single-watch limitation.
  Response: no we'll watch both now

- `[FILLTIME-INIT-RACE]` (nit) - logger.cpp:30 - ctor samples current_ms() twice; sample once into a local.
  Response: fix it

## F. Terminal / pages infra

- `[CLOCK-FLOOR-COUPLING]` (low) - terminal.cpp:733 - the clock path has no per-run fit check and relies entirely on the cold floor; folds into the MAX_FIELD_TX_BYTES guaranteed-floor change.
  Response: what ?

- `[METRICS-NO-PLC-GAP]` (low) - terminal.cpp:575-583 - with _ctx.plc==null the metrics field stays permanently blank; draw a "PLC:---" placeholder or document.
  Response: yes

- `[SGR-PCT-VIDEO]` (nit) - terminal.cpp:671 - the value-highlight columns are magic constants (12..15, 21..24) hand-derived from the format string; derive from named offsets.
  Response: yes

- `[FIELD-WIDTH-TRUNCATE]` (low) - metric_page.h:281 - width silently clamped to CACHE_COLS-1 (31); DEBUG-assert or document the hard 31-col cell limit.
  Response: document it

- `[STATIC-ORDINAL-ROWGUARD]` / `[EMIT-DEFERRED-NORESET]` (low) - metric_page.h:197-215,187 - ordinal consume-vs-clip order differs across helpers (latent if runtime resize is ever added); _run_static doesn't reset _emit_deferred (harmless today). Folds into the paced-painter unification.
  Response: yep

- `[DRAINTAIL-DOC]` (nit) - scrollback.h:210 - comment says SCROLLBACK_ROWS_PER_TICK, code uses LOG_ROWS_PER_TICK.
  Response: fix the comment

- `[WRAP-MULTIBYTE]` / `[NARROW-WRAP-FANOUT]` (nit) - line_buffer.h:70-79 - a multibyte glyph straddling the wrap splits into mojibake; a tiny continuation budget fans a long line into one row per char. ASCII-only today; floor the continuation budget.
  Response: yes indeed

## G. Status / Home / Alarms pages

- `[HOME-VENT-PARTIAL]` (low) - pages.h:226 - Ventilation shows only window_a/blower_a, ignoring B-D; show an aggregate.
  Response: yes

- `[HOME-BG-BACKLOG]` (low) - home.h:178-195 - the Home log-tail queue isn't drained while Home is backgrounded, so it fills/drops and replays as a burst on focus; drain into scrollback even when backgrounded.
  Response: yes

- `[HOME-TINY-SCREEN]` (low) - home.h:52-56 - on rows() <= HOME_SCROLLBACK_LINES the log band overwrites the logo with no divider; clamp the band height.
  Response: yes

- `[ALARMS-FIXED-80]` (low) - alarms.h:140 / pages.h:60 - AlarmsPage and the Status grid bake in 80 columns (safe only because construction hard-codes 80); static_assert/clamp the width or derive layout from cols().
  Response: derive from cols()

- `[ALARMS-SORT-COST]` (low) - alarms.h:302-311 - O(nvis^2) insertion sort with a model deref per compare, nvis up to 64, once per repaint; fine at the bound, switch to a partial pass if alarm count grows.
  Response: ah that's bad. what can we do here ?

- `[STATUS-RAIN-AGE-DOC]` (nit) - pages.h:156 / payload.h:72 - comment cites a "999y" worst case but rain_age_s is uint16 (~18h max); fix the comment.
  Response: make rain_age_s an uint32

## H. Alarms subsystem

- `[MASKEDBY-PLATFORM-COUPLING]` (low) - alarms.h:52,192 - masked_by is an absolute 64-bit set but a site can't name a platform alarm's (runtime-spliced) bit; expose a helper or document. Folds into the namespace refactor.
  Response: yeah alarms need a refactor

- `[XRUN-LATCH-ONLY]` (low) - firmware.h:183 / platform.cpp:20 - CPU_XRUN is a one-beat pulse relying entirely on latch_s=300 to stay visible; assert latch_s>0 for pulse-style local alarms.
  Response: what ? also note that CPU_XRUN will be set by the HealtMonitor in the near future

- `[NOTIFIER-NULL-OK]` (nit) - notifier.h:50 - on_/off_notified latch "sent" against the stub unconditionally; gate the latch on a successful publish() when the real client lands.
  Response: what ? is this about mqtt ?

- `[ALARM-DESC-EMPTY-LOG]` (nit) - alarms.h:205,218 - START/END logs carry only c.desc; fall back to c.code when desc is null.
  Response: yes

## I. Shell

- `[HELP-DRIFT]` (low) - shell.cpp:91 - the built-in help line is a hand-maintained literal duplicating BUILTINS[]; generate from the table.
  Response: yes

- `[SETCMD-NO-CAP]` (low) - shell.h:93 - set_commands takes an unbounded count and doesn't warn on a site name colliding with a built-in (silently dead); assert a cap, warn on collision.
  Response: yes

- `[STATS-UNPACED-BURST]` (low) - shell.cpp:129-176 - multi-line `stats` drains at LOG_ROWS_PER_TICK (1/tick); bounded today, comment it so nobody adds a 50-line command expecting it instant.
  Response: yes

- `[HELP-NAME-TRUNC]` / `[VERSION-BUF-80]` (nit) - shell.cpp:100,178 - site command names > 12 chars break help alignment; _cmd_version uses a bare char[80]; size from LOG_FORMATTED_LEN.
  Response: use a line-by-line help command

- `[HISTORY-KILLRING-READY]` (low) - shell.h:104-119 / shell.cpp:64-92 - key routing for history/kill-ring is in place but the data path isn't: CommandShell has no draft slot / ring storage and no editor set_line()/take_buffer() mutators. Add these before implementing the TODO.
  Response: yep that's deferred to later

## J. Serial / text

- `[RX-CAPACITY-255]` (low, doc) - serial.h:47 - a 256-byte ring holds 255 (one slot for full/empty); docs/CLAUDE.md overstate usable capacity by one byte each.
  Response: yes

- `[TX-DROPPED-OVERCOUNT]` (low) - serial.cpp:47 - tx_dropped counts "bytes that didn't fit this call", an upper bound for resuming callers; documented, contained to the watched console serial. No fix expected.
  Response: yep

- `[TRANSMIT-DSB-PLACEMENT]` (low) - serial.cpp:71,122 - per-byte HAL::dsb() in read()/transmit() now buys nothing (main-loop-only SPSC); hoist one before the loop or drop it.
  Response: yep

- `[FORMAT-FLOAT-WIDTH-U8]` / `[FORMAT-FLOAT-NEG-ZERO]` (low) - text.cpp:46,73 - width > ~38 silently under-fills (no overflow); a small negative rounding to zero prints "-0.0" (matches printf). Document/assert the width cap.
  Response: document+assert

- `[ENV-STRTOL-LIBC]` / `[FORMAT-AGE-SNPRINTF]` (low) - text.cpp:92,104 - the cleanest libc-removal wins per [TODO-LIBC]: env_u8's strtol/getenv (sim-only) and format_age's snprintf both have in-house replacements.
  Response: yes, good replacements. keep them. libc is bad.

- `[PEEK-NO-LEN]` / `[SCAN-SEGMENT-SIZET-MIX]` / `[TX-DISCARD-TAIL-RACE]` / `[POWER-OF-TWO-MASK]` (nit) - serial.cpp / text.h - single-byte peek only; size_t/uint16 type mix (safe); belt-and-suspenders dsb on a single-store reset; power-of-two masking confirmed correct. No action.
  Response: ok

## K. Composition / build / config

- `[OUTPUT-DATA-PTR-CAPTURE]` (low) - firmware.h:102 / sync.h:220 - readers/pages cache _plc_handler.output_data() (an interior pointer); document it must never relocate, or hand out an accessor.
  Response: yes

- `[NETWORK-CONFIG-FIELD-MISSING]` (low) - network.h:17 / payload.h:55 - the network handlers' "config serial" trigger needs a PlcOutput_t field + PlcSyncContext accessor that don't exist yet (an ABI change to design with the PLC program first).
  Response: that's for later

- `[CMAKE-SITE-GLOB-NONDETERMINISTIC]` (low) - CMakeLists.txt:153,446 - site/lib sources use a non-CONFIGURE_DEPENDS GLOB; a new .cpp won't recompile until manual reconfigure. Add CONFIGURE_DEPENDS or list explicitly.
  Response: yes

- `[CMAKE-CONFIG-ABI-GUARD-MISSING]` (low) - CMakeLists.txt:110 - the config-override ABI safety rests on every TU pulling the PUBLIC define; note that any TU including libbotanik_config.h must link botanik (or stamp a config-hash symbol).
  Response: yes

- `[BAUD-CONST-DUPLICATED]` (nit) - botanik_sim/main.cpp:53 (4800) vs botanik_fw/firmware.cpp:20 (115200) - console baud is a hard-coded literal per target; promote to BOTANIK_CONSOLE_BAUD.
  Response: yep

- `[ASSERT-LOGGER-GLOBAL-SINGLETON]` (nit) - libbotanik_asserts.h:26 - set_assert_logger stores a function-local-static; fine for one instance, note the single-instance assumption.
  Response: yes

---

## Parser / editor (no-wedge confirmations, mostly nits)

- `[ESC-TIMEOUT-LATENCY]` (low) - parser.cpp:308 - bare ESC emits only after 100ms silence (by design); doc the latency.
  Response: yes

- `[CSI-INTERMEDIATE-ABORT]` / `[CSI-CONTINUOUS-NOWEDGE]` (low) - parser.cpp:148-178 - an intermediate byte (0x20-0x2F) aborts to IDLE mid-sequence and the trailing final is re-dispatched; the timeout is inter-byte not total. No bound sequences hit this today; document "intermediates unsupported".
  Response: yes

- `[ALT0-ENUM-ORDER-COUPLING]` (low) - parser.h:56 / terminal.cpp:119 - page switching relies on ALT_1..ALT_9 being contiguous; add a static_assert.
  Response: yes

- `[COUNT-SATURATION]` (low) - parser.cpp:332 - _pending_cmd_count uint16 increments unboundedly while coalescing (practically unreachable, downstream clamps anyway); add a saturation guard.
  Response: yes

- `[UTF8-COLWIDTH]` (low) - editor.cpp:64 / ansi.cpp:58 - input is ASCII-only (0x20-0x7E); document it, since a future 8-bit change would corrupt the byte-counted reconcile cursor.
  Response: yes

- `[BATCH-TIMEOUT-DRAIN-DEP]` (low) - parser.cpp:100 - a held backspace under sustained backpressure flushes only at the 1s batch timeout; consider a shorter command-flush timeout than the text-batch timeout.
  Response: yes

- `[DEAD-ESC-MAP]` / `[SEP-DUP-MAP]` / `[INSERT-CAP-OFFBYONE-OK]` (nit) - parser.cpp:206,271 / editor.cpp:151 - dead ESC case in _map_control_char; undocumented SEP duplicate mappings; capacity-vs-assert one-byte slack. Cosmetic.
  Response: cosmetic but important, fix it.
