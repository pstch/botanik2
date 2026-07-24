# The console screen back-buffer

How the console gets a whole screen's worth of intent onto a slow serial link:
one diff, in one place, paced under a per-tick byte budget.

Status: **shipped**. (This replaces an earlier "transactional multi-queue"
proposal; see §6 for why that was abandoned.)

## 1. The problem

The console renders over a slow serial link with a small TX ring
(`utils/serial.h` `BufferedSerial`). `write()` silently **drops** bytes when the
ring is full, so every renderer has to (a) emit only what changed and (b) pace
itself so it never overruns the ring. The hard constraints:

- **Wire bytes are scarce.** Re-sending an unchanged row is waste we can't afford.
- **No torn frames.** A dropped mid-row write, or a scroll whose DL/IL didn't
  fit, must never corrupt the screen.
- **Priority.** A scroll or keystroke echo must beat telemetry churn for the ring.

The earlier design met these by making *each renderer* reason about a paced,
coalescing, droppable queue - per-page "what's on the wire vs. what's queued"
shadows, promotion, scroll reconciliation, replicated across every page. That
bookkeeping was the complexity, and it was where the bugs lived.

## 2. The model: one diff, dumb renderers

The terminal owns a **cell back-buffer** and does the diffing once, for everyone.

```
renderers ──draw (move_to/write_chars/set_inverted/scroll_region)──▶  FRONT grid
                                                                         │
Console::step() ── present() ── diff FRONT vs BACK ── minimal paced ANSI ─▶ wire
                                     │
                                     └── copy emitted cells ──▶ BACK grid
```

- **FRONT** = the desired screen. It is **retained**, not per-frame: it holds
  whatever was last drawn there and is only overwritten where a renderer draws
  again. A tick that draws nothing leaves FRONT (and so the screen) alone.
- **BACK** = what `present()` has actually put on the wire.

Renderers draw **immediate-mode** through the usual ops; those ops now stamp
cells into FRONT instead of emitting bytes. Once per tick the Console calls
`present()`, which walks FRONT vs BACK and emits the minimal ANSI to reconcile
them, then copies the emitted cells into BACK.

The pairing is what makes "redraw everything, every tick" cheap: renderers are
immediate-mode (no memory of what they drew), while the *screen* is retained-mode
(FRONT remembers), and the diff between the two is the wire traffic.

A cell is one byte: a 7-bit glyph with the top bit (`CELL_INV`) for reverse
video. Two grids sized for the largest terminal (`GRID_ROWS x GRID_COLS`) cost
~8 kB - it *replaced* the ~9 kB transaction pool, so RAM is flat.

### What this buys, for free

- **"Emit only what changed" is automatic and per-cell** - the diff drops any
  cell that didn't move, for every renderer, with no page-side cache. Redrawing
  an unchanged row costs zero bytes.
- **No torn frames.** Each cell carries its own attribute, so inversion can't
  leak across a boundary; `present()` emits whole cells, so a row is never
  half-written. The paced budget (below) guarantees the ring never overruns.
- **Coalescing is free.** If a value changes twice before `present()`, FRONT
  just holds the latest; intermediate values never reach the wire.
- **Renderers keep no shadow state.** No `_row`/`_sig`, no promote, no scroll
  reconciliation. Pages are layout + "render current state."

The one surviving page-side cache is `MetricPage`'s `_vcache`, and it is *not* a
wire shadow: it answers "did this value change since last tick" so the page can
apply the watch-style highlight. That's a semantic question about the data, which
the terminal can't answer - it only knows which cells differ, not why.

## 3. `present()` - the diff (`console/terminal.cpp`)

1. **Pending scrolls first.** `scroll_region()` shifts the FRONT grid and records
   a DL/IL. `present()` emits the DL/IL and, *only if it fit*, shifts BACK to
   match - so a kept row's FRONT==BACK and the diff skips it. If the DL/IL can't
   fit, BACK is left unshifted and the cell diff simply repaints the band
   (correct, just more bytes). A scroll can therefore never desync the screen.
2. **Paced cell diff.** Row by row, find each run where FRONT≠BACK and emit it:
   position the cursor with the minimal relative/absolute move (`_go`), toggle
   reverse video per run, write the glyphs, copy the run into BACK (`_put_run`).
   The leading move + SGR + glyph byte-minimization is the same tuning the old
   direct-write path had.
3. **Cursor.** Park the real cursor: at the edit point when a renderer asked for
   a visible cursor (the shell editor), otherwise idle in the **bottom-right
   corner**, out of the content. One owner, re-homed each tick, so a value update
   elsewhere can't strand it.

   The park is skipped when the diff ran out of budget and left work for the next
   tick: mid-repaint the frame is inconsistent anyway, and parking would spend a
   move on a cursor that the next tick immediately moves again. It happens on the
   tick the screen finally settles.

These are the three phases of `present()`, and they are named as such in the
code: `_emit_scrolls()`, `_emit_diff()` (returns whether the frame is fully
reconciled), `_park_cursor()`.

### Horizontal shifts

A cell diff is worst exactly where a renderer slid a row sideways (the editor
inserting a character, a column widening): every cell past the edit differs, so a
naive diff rewrites the whole tail. Before the run loop, `_shift_row()` checks
whether the row is simply BACK shifted by a few columns, and if so sends one
escape - SM4/RM4 insert mode to open cells, DCH to close them - which shifts the
terminal's own cells, attributes and all. It only does so when that beats
rewriting the changed span, and anything it can't express is left to the run loop
behind it. A one-column slide of a 61-column row costs ~19 bytes instead of 80.

This is the *only* structural op the diff infers. Vertical slides can't be
inferred the same way (a renderer's intent is ambiguous across rows), which is
why `scroll_region()` stays an explicit call.

### Pacing / budget

`present()` emits at most `_tx_cold_depth` bytes above what's already queued in
the ring (`depth_for_baud()` from `TX_COLD_TARGET_MS`), and never starts a run it
can't fit (checked against `availableForWrite()`, so a run is all-or-nothing  - 
no partial emit). What doesn't fit this tick stays in FRONT and is re-diffed next
tick - no per-renderer staging, no state between ticks. This is the whole "handle
the restricted TX budget" story, in one place.

Priority falls out of draw order + the diff: keystroke echo and structure are
drawn before telemetry within a tick, and the scroll DL/IL is emitted before the
cell diff. (There are no explicit lanes; the earlier design's priority queue was
subsumed by "draw the frame, then diff it.")

## 4. Scrolling (the one stateful op)

A DL/IL scroll is order-dependent, so it's the only thing `present()` treats
specially (§3.1). A page that scrolls calls `scroll_region(top, bottom, up, n)`
and then just redraws its rows; the terminal shifts FRONT, emits the DL/IL,
shifts BACK on success, and the diff repaints exactly the revealed rows. The page
keeps no shifted-shadow of its own.

## 5. What a page looks like now

```cpp
void tick(bool active) {
    if (!active || !_dirty) return;
    _collect();
    if (scrolled) terminal.scroll_region(DATA_TOP, band_bot, up, n);
    draw_structure();
    for (row : window) draw_row(row, data, row == selected);   // full rows
    _dirty = false;
}
```

No `_sig`, no `_promote`, no `_reconcile_band`, no keyed transactions. The three
tables (alarms / monitor / config) share the same shape: a scroll band + a
selection cursor, redrawn immediate-mode, pared by the diff.

## 6. Why not the multi-queue

The abandoned proposal put a priority-laned, coalescing, atomic transaction
*queue* (`TxScheduler`) between renderers and the ring. It worked, but it pushed
the "on the wire vs. queued" reasoning **into every renderer** (each needed a
drawn-vs-submitted shadow + promotion to diff and to reconcile scrolls). That
per-page machinery - replicated three-plus times - was the actual complexity and
the source of a run of stuck-highlight / redraw-pileup bugs.

The back-buffer moves that reasoning into **one** place the renderers never see.
It subsumes coalescing (FRONT holds the latest), atomicity (per-cell attributes,
whole-cell emits), and pacing (one budget in `present()`), and deletes the queue,
the shims, and every per-page shadow. Net: ~1.3k fewer lines, `AnsiTerminal`
smaller, and the bug classes gone by construction. The one thing the queue did
that the diff doesn't is *explicit* lane priority; in practice draw-order +
emit-order cover it.
