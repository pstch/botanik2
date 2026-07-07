# Transactional multi-queue TX path — design proposal

Status: **draft for review** (no code yet).

## 1. Why

The console renders through a single 4096-byte TX ring (`utils/serial.h`
`BufferedSerial`). `write()` silently **drops** bytes when the ring is full.
To avoid drops, ~95 sites across 14 files cooperate: each renderer checks
`AnsiTerminal::tx_below_cold()` and self-budgets (`TX_{STATUS,HOT,COLD}_FIELDS_PER_TICK`),
and every table stages its full repaints a few rows per tick
(`_paint_step`/`_paint_i`/`_request_repaint`). Page switches use a **destructive**
`tx_discard()` that clears the ring and desyncs the terminal shadow.

Three structural problems fall out of this, all observed:

1. **No priority (the interactivity bug).** A scroll and a telemetry value update
   compete for the same ring with no ordering. Value churn keeps the ring above
   the cold floor, so a scroll can't fit its atomic `scroll_region` (DL/IL) and
   **falls back to a full-window repaint** (`monitor.h` `_scroll`, `alarms.h`
   `_move_selection`). This is the "scrolling is delayed / tables get fully
   rewritten" symptom.
2. **Bolted-on, lossy atomicity.** `scroll_region()` is best-effort and tears if
   its DL/IL can't fit; `write_chars()` truncates a row mid-way on a full ring and
   the page caches it as fully painted (stale torn row until the value next
   changes). Two latent bugs, same origin.
3. **Destructive discard.** `switch_to()` drops queued bytes and desyncs the SGR
   shadow (hence the `force_inverted` hack and `invalidate()` calls), forcing a
   from-scratch full repaint.

The full-redraw audit confirmed: the *only* avoidable full redraw is the hot-ring
scroll fallback (#1). Everything else is either a necessary paint onto a
physically-cleared screen (page switch / Ctrl-L) or already per-row diff-gated.

**Conclusion:** priority is the missing primitive. A transactional multi-queue
TX path adds it, and in doing so deletes the cooperative machinery that exists
only to work around a droppable, unprioritised ring.

## 2. Model

Insert a scheduler between renderers and the serial ring:

```
renderers ──submit(transaction)──▶  TxScheduler ──drain(budget)──▶  TX ring ──transmit()──▶ wire
             (priority + key)        (lanes, coalesce)               (unchanged)
```

### 2.1 Transaction

A transaction is a **self-contained, idempotent** paint unit: an already-serialised
run of terminal ops that (a) is valid regardless of what ran before it and (b)
leaves the shared remote state exactly as it found it. Plus metadata:

- `lane` — priority (see 2.4).
- `key` — optional coalescing id (see 2.5).
- `len`, buffer handle.

These properties are what make transactions safe to **reorder, coalesce, and
drop**, and they are the point of the note — *skipping any one transaction
must never corrupt the screen, only leave stale content on the rows it would have
painted.* See 2.2.

### 2.2 Remote state and idempotency (the critical invariant)

The remote terminal carries three pieces of shared state that a naive byte stream
mutates incrementally: **inversion** (SGR reverse video), **cursor visibility**,
and **cursor position**. If transactions can be dropped/reordered, none of these
may be left half-set across a transaction boundary — a "turn inversion on" whose
matching "off" lives in a *different* transaction corrupts every row after it the
moment either is skipped.

Rules that make a transaction idempotent and skip-safe:

1. **Absolute cursor move first.** A transaction never assumes the cursor's
   position; it opens with an absolute `CUP`. So position is never inherited, and
   re-applying or reordering a transaction paints the same cells.
2. **Balanced inversion, baseline = normal.** Inversion is toggled only *within* a
   transaction, every `on` matched by an `off`, so a transaction always exits in
   normal video. On entry it may assume normal (the previous *emitted* transaction
   restored it; a *dropped* one never ran, so it changed nothing). Inversion never
   crosses a transaction boundary. (Belt-and-suspenders: the recorder asserts
   `normal` at commit if the body left it on.)
3. **Cursor is scheduler-owned, not transaction-owned (see 2.6).** No content
   transaction touches cursor visibility. The scheduler re-asserts the live cursor
   (position + on/off) as a trailing op after every drain, so dropping or
   reordering content transactions can never strand the cursor.
4. **One complete visual unit per transaction.** A transaction is a whole row / cell
   / rule — never a fragment like "move + SGR-on" split from "text + SGR-off".
   Granularity = the smallest thing that is self-consistent on its own.

Net guarantee: **drop, coalesce, or reorder any set of transactions and the worst
outcome is stale content on some rows — never leaked inversion, never a lost or
mispositioned cursor.** This is stronger than "self-contained": it is what lets the
scheduler freely prioritise and shed low-lane work.

Cost: a few bytes per transaction (the leading absolute `CUP`, and an occasional
explicit SGR the old cross-op shadow would have elided). Paid back by never
re-emitting unchanged rows and by coalescing superseded updates.

### 2.3 Building a transaction (terminal recording mode)

`AnsiTerminal` gains a recording mode so renderers keep using the same ops:

```cpp
term.begin(lane, key);          // shadow -> {pos: unknown, inverted: false(baseline)}
term.move_to({row, col});       // absolute move emits (pos was unknown)
term.set_inverted(true);        // emits (baseline was normal)
term.write_chars(...);
term.set_inverted(false);       // balanced: back to baseline before commit
term.clear_to_eol();
Txn* t = term.commit();         // asserts normal if the body left inverted on; hand off
```

`begin()` seeds the recorder's shadow to the **baseline** (position unknown so the
absolute move always emits; inverted = normal). Within the transaction the existing
shadow-minimisation still applies, so internal SGR toggles stay minimal; `commit()`
enforces the 2.2 invariant (exits in normal video) and never emits a cursor
visibility op (2.6 owns that). Outside recording (during migration) the ops write
straight to the ring as today.

### 2.4 Lanes (priority)

Drain order, highest first:

| Lane | Carries |
|------|---------|
| `L0_INPUT`   | keystroke echo, the shell editor's per-keystroke reconcile, cursor placement |
| `L1_STRUCT`  | scrolls (DL/IL), headers, rules, page-switch cl/structure |
| `L2_VALUE`   | data-row value updates, telemetry, clock/metrics |

Three lanes to start; tunable. The point: a scroll (`L1`) and a keystroke (`L0`)
always drain before telemetry (`L2`) — priority inversion gone.

### 2.5 Keyed coalescing

Each transaction may carry a key; submitting a key that already has an un-emitted
transaction in its lane **replaces** it. Keys are by **screen position** so a
superseding update is unambiguous:

- data rows: `p<page>:r<screenRow>`
- status bar: `clock`, `metrics`, `tabs`
- scroll: `scroll` (not coalesced across a same-tick paint — see 2.7)

Fast value churn on the same row coalesces to the latest; intermediate values
never reach the wire. This is the second half of the interactivity win: the ring
carries only *current* state.

### 2.6 Budget and draining

One central budget replaces every `tx_below_cold()` + `*_FIELDS_PER_TICK`:

```
budget = max(0, cold_depth - tx_used())     // cold_depth from TX_COLD_TARGET_MS, as today
for lane in [L0, L1, L2]:
    while lane not empty:
        t = lane.peek()
        if t.len <= budget and t.len <= ring_room:
            emit(t); budget -= t.len; lane.pop_and_retire()
        else if t.len > cold_depth and t.len <= ring_room:
            emit(t); budget = 0; lane.pop_and_retire()   // oversized single txn: emit once room exists
        else:
            break            // atomic: never partial-emit; leave queued for next tick
    if budget == 0: break
```

Everything is row-sized (a data row is ~80-120 bytes; `cold_depth` at 115200 baud
is ~1.4 kB), so the oversized branch is a safety valve, rarely taken. Pacing is
now a single loop, not per-component staging.

**Cursor re-assertion (scheduler-owned state).** The scheduler tracks the desired
live cursor — `{visible, row, col}` — set by whoever owns input focus (the shell
editor sets it to its edit point; every other page sets it hidden). After draining
the lanes, the scheduler emits one trailing op: move to the desired position and
apply visibility, but *only if it changed* since last drain (idempotent, ~0 bytes
on a steady screen). Because this runs after every drain, dropping or reordering
any content transaction can never strand the cursor — it is re-homed each tick from
a single owner. Content transactions therefore never touch cursor visibility (2.2
rule 3).

### 2.7 Scrolling (the one hard case)

A DL/IL scroll is stateful and order-dependent: it must apply to the *old* screen,
before any row paints of the *new* screen, and it invalidates pending value
updates for the rows it shifts. Rule:

1. On a scroll the page submits the `scroll` DL/IL transaction on `L1` **and**
   `scheduler.drop("p<page>:r*")` — dropping its own pending `L2` row updates,
   which now target the wrong content.
2. It then submits fresh transactions for the newly-revealed rows only (the kept
   rows moved with the DL/IL and are not re-emitted).
3. Lane order guarantees the `L1` DL/IL emits before the `L2` revealed rows in the
   same drain. If the DL/IL doesn't fit this tick it stays queued (atomic) and the
   revealed rows wait behind it — no tear.

This is exactly today's DL/IL logic, but the scheduler (not a `tx_below_cold()`
guard) decides timing, and the scroll gets priority over value churn — so the
hot-ring full-repaint fallback disappears.

The shell editor's reconcile (also stateful/order-dependent) submits its whole
per-keystroke op batch as **one** compound `L0` transaction: atomic, highest
priority, exactly the semantics an echo wants.

### 2.8 Page switch (non-destructive)

`switch_to()` becomes: `scheduler.drop("p<old>:*")`, submit an `L1` clear-band
transaction, `focus()` the new page (which submits its structure + rows). No
`tx_discard`, no shadow desync, no `force_inverted`. The new page still fully
paints (its screen band was cleared — necessary), but paced by the scheduler, so
pages drop `_paint_step` entirely.

## 3. What pages become

Change detection stays in the page (the `_vcache`/`RowShadow` diff-gates decide
*what* changed); the page loses all *pacing*.

Before (monitor value tick, today):
```cpp
if (_painting()) { _paint_step(); return; }         // staged, tx_below_cold-gated
uint64_t now = ...; if (now - _last < MONITOR_REFRESH_MS) return;
_repaint_changes();                                 // walks rows, writes directly, may be throttled/starved
```
After:
```cpp
for (vi : visible) if (value_changed(vi)) {
    term.begin(L2_VALUE, key(vi)); draw_row(vi); term.commit();   // submit; scheduler paces + coalesces
}
```
No `_paint_step`, `_paint_i`, `PAINT_IDLE`, `_request_repaint`, `tx_below_cold`,
`MONITOR_REFRESH_MS` throttle (coalescing replaces it). Config and the bars shrink
the same way.

## 4. Deletions vs additions (the LoC question)

**Deleted:** `tx_below_cold` and its call sites; `_tx_cold_depth` migrates to the
scheduler; `force_inverted`; `TX_STATUS/HOT/COLD_FIELDS_PER_TICK` (→ one budget
from `TX_COLD_TARGET_MS`); `MONITOR_REFRESH_MS`; the `_paint_step`/`_paint_i`/
`_request_repaint` staging in config/alarms/monitor/metric_page; the status bar's
round-robin field budget; the scrollback throttle; the `tx_discard`-on-switch dance.

**Added:** `TxScheduler` (lanes + pool + drain + coalesce + drop), the terminal
`begin/commit` recording, per-renderer `begin/commit` wrapping.

**Honest read:** likely net-neutral-to-modestly-down on raw lines, but a *large*
reduction in coupling — the cooperative dance across 14 files collapses into one
tested component, and the pages get materially simpler. I would not sell this on
line count; I'd sell it on interactivity + correctness (atomicity kills the two
latent tear bugs) + one place to reason about pacing.

## 5. Memory

Add a bounded transaction pool for *queued* (not-yet-scheduled) transactions,
separate from the 4096-byte ring (which stays as the wire staging buffer). A row
transaction is ~128 bytes; a full 24-row screen + bars ≈ 30 transactions. Proposal:
a fixed arena of `N` fixed-size slots (e.g. 48 × 160 B ≈ 7.5 kB) with a free list,
or a byte-pool + descriptor ring. Overflow policy: drop the lowest-priority queued
transaction (counted, like `_tx_dropped` today) — deterministic, no unbounded
growth. ~8 kB extra RAM; negligible on the H747, but called out for the budget.

## 6. Migration (phased, gate green at each step)

1. **Scheduler + recording, no behaviour change.** Add `TxScheduler`, the pool,
   `AnsiTerminal::begin/commit`, and `scheduler.drain()` feeding the ring. Keep the
   direct-write path for un-migrated renderers. Add doctest unit tests: priority
   ordering, keyed coalescing, budget pacing, atomic (no partial emit), drop-on-full,
   drop-by-key.
2. **Bars.** Migrate status bar + alarm bar; delete their budgets/round-robin.
3. **ConfigPage** (no scroll — simplest). Delete its staging.
4. **Monitor + Alarms.** Scroll transactions + drop-on-scroll (2.7). Delete staging
   + `MONITOR_REFRESH_MS`.
5. **Editor.** Wrap its reconcile as one `L0` compound transaction.
6. **Remove dead cooperative machinery + config knobs.** Final LoC accounting.

pyte goldens are the regression net throughout; add a pyte test that scrolls under
heavy value churn and asserts no tear + bounded bytes.

## 7. Risks / open questions

- **Scroll ↔ pending-value ordering (2.7)** is the sharp edge; needs dedicated unit
  tests for "scroll drops stale row updates."
- **Self-contained overhead**: measure the extra bytes/transaction vs today's
  cross-op shadow elision; expected small and net-positive after coalescing.
- **Lane count** (3 vs 4: split cursor from scroll) — start 3, revisit.
- **Pool sizing / overflow policy** — 8 kB and drop-lowest-priority proposed; confirm.
- **Editor fit**: the byte-critical reconcile as one compound transaction — validate
  its byte count doesn't regress.
- **Blast radius**: terminal + all pages + both bars + scrollback + config. Own
  branch, staged, goldens green per phase.

## 8. Recommendation

Proceed, phased, starting with phase 1 (scheduler + tests) so the design is
exercised in isolation before touching a single renderer. Decide §5 (pool size)
and §2.4 (lane count) here; everything else can be validated phase by phase.
