# AlarmsPage row-level diffing (Phase 1: unified {idx, sig} shadow)

Date: 2026-06-21
Scope: `src/libbotanik/pages/alarms.h` only. No terminal changes.

## Problem

A full repaint of the Alarms table retransmits the entire table every time, with
no comparison against what is already on screen. The terminal does no cell- or
frame-level diffing (`AnsiTerminal::write_chars` emits every byte; only cursor
moves are skipped when already in position). At the simulator console rate
(4800 baud = 480 B/s) a ~2 KB table takes ~4 s to clock out, so any full repaint
crawls. An incremental UP/DOWN scroll is fast only because it sends one
`scroll_region` shift plus a single row (~100 B).

The painful case is the in-place repaint: while sitting on the page, an alarm
firing or clearing trips `_changed()` on the 1 s re-check and repaints all ~23
elements even though typically only a few rows, and within them only the ON /
LAST EVENT cells, actually differ.

## Goal

On a repaint, emit only what actually changed on screen:

- skip any row whose content is unchanged (structural rows and data rows whose
  alarm and rendered state are identical to what is already shown);
- for a data row that still shows the same alarm but whose state changed, rewrite
  only the **ON** and **LAST EVENT** cells, leaving **CODE** and **DESCRIPTION**
  untouched. Those two columns come from the static alarm table and never change
  for a given alarm, so they are excluded from both the change signature and the
  redraw.

### Explicitly out of scope (deferred to Phase 2)

Reorder optimization. An alarm transition reorders the list (a firing alarm jumps
to the top; the run below shifts down by one), so the shifted screen rows show
*different* alarms (different CODE/DESC) and are redrawn in full by this phase. A
later phase can detect a single logical move and express it as one `scroll_region`
sub-region shift (the terminal's `scroll_region`, terminal.cpp:423, is already an
IL/DL pair), with this phase's cell patching cleaning up the residual. Not built
here.

Also out of scope: the page-switch (focus) case. The screen is shared by all
pages and cleared on switch, so there is nothing to diff against and a switch to
the page always repaints in full.

## What this phase wins (and does not)

Wins: structural rows (margins, header, rules, blank rows) cost nothing on an
in-place repaint; data rows whose alarm did not move cost nothing; a same-alarm
state change (e.g. an alarm already at the top is acknowledged, flipping only its
ON badge) costs the two variable cells (~18 B) instead of a full row (~85 B).

Does not win: the rows a reorder shifts still redraw in full (that is Phase 2).
This phase still removes the structural and unchanged-row bytes from every
repaint, which is most of a quiet refresh.

## One unified shadow, indexed by absolute screen row

```c
struct RowShadow {
    uint8_t  idx;   // alarm table index shown; 0xFE = structural row; 0xFF = blank
    uint32_t sig;   // data row: packed (ON state | event minute); structural: FNV(line)
};
RowShadow _row[MAX_ROWS];   // MAX_ROWS e.g. 64
```

`MAX_ROWS` bounds the array; rows beyond it bypass the diff and are always drawn,
so the bound is a memory/coverage tradeoff, never a correctness risk. One concept
covers every row; `idx` discriminates the three kinds. A given screen row is
always structural or data (disjoint index ranges: data rows are
`[DATA_TOP, _data_bottom()]`), and a terminal resize is covered by the focus-style
full invalidate, so the `idx` tag never has to change kind in place without a
focus.

### Structural rows - `idx = 0xFE`, `sig = FNV(line)`

A single funnel for the structural draw helpers:

```
void _emit(uint16_t row, const char* line, uint16_t n, int16_t inv_col);
```

1. `sig = _hash(line, n)` (FNV-1a over `line[0..n)`, masked to 31 bits); if
   `inv_col >= 0`, fold a constant in so a badge-inverted line differs.
2. If `row < MAX_ROWS && _row[row].idx == 0xFE && _row[row].sig == sig`: return.
3. Else `move_to({row,0})`, `set_inverted(false)`, write the line (split for the
   inverted span when `inv_col >= 0`), `clear_to_eol()`; store
   `_row[row] = {0xFE, sig}`.

`n == 0` (a cleared row) hashes to a fixed blank value, so blank structural rows
are diffable too. Rewired through `_emit`:
- `_draw_grid_row` (the header row) -> `_emit(row, line, n, -1)`.
- `_draw_rule` (both rules) -> `_emit(row, line, n, -1)`.
- `_clear_row` (top margin, the two blank rows) -> `_emit(row, "", 0, -1)`.

`_draw_footer` is left always-redrawn: its split layout (inverted mode badge on
the left, right-aligned SPACE hint at a far column) is two screen regions on one
row and does not fit the one-line-content model. It is one row, changes only on a
mode/count change, and never calls `_emit`, so its row index is never shadowed or
consulted.

The hash is computed only for the ~6 structural rows. It can never collide into a
data or blank slot because those carry `idx != 0xFE`, which is compared first.

### Data rows - `idx = alarm index`, `sig` = exact packed state

The variable part of a data row is exactly the ON state (4 values) and the LAST
EVENT minute, so it is packed into `sig` with no hashing and zero collision risk:

```
on  = _on_state(c)        // 0 blank, 1 "OFF", 2 "ON" normal, 3 "ON" inverted
ev  = (!c.active && c.edge_ms == 0) ? EVENT_EMPTY            // 0x3FFFFFFF
                                    : (uint32_t)((c.edge_ms / 60000u) & 0x3FFFFFFF)
sig = ((uint32_t)on << 30) | ev
```

Minute buckets equal the render resolution ("YY/MM/DD HH:MM"), so equal `sig`
guarantees identical ON and EVENT cells. 30 bits of minutes covers past year 4000.

`_draw_data_row(row, vi)` becomes:

```
new_idx = (vi < _nvis) ? _vis[vi] : 0xFF
if new_idx == 0xFF:                              // row past the set -> blank
    if _row[row].idx != 0xFF: _clear_raw(row); _row[row] = {0xFF, 0}
    return
c   = condition(new_idx)
sig = _pack(c)
if _row[row].idx == new_idx:                     // same alarm: CODE/DESC still valid
    if _row[row].sig != sig && _cells_fit():
        _patch_cells(row, c)                     // rewrite ON + EVENT only
        _row[row].sig = sig
    else if _row[row].sig != sig:                // too narrow to patch safely
        _draw_full_row(row, c); _row[row] = {new_idx, sig}
    return                                        // sig equal -> skip entirely
_draw_full_row(row, c); _row[row] = {new_idx, sig}   // identity changed -> full row
```

- `_draw_full_row(row, c)` is the current `_draw_data_row` body: build the line
  with `_build_grid_row` and write it, splitting the 2-char "ON" badge to
  reverse-video when `c.active && !c.acknowledged`.
- `_patch_cells(row, c)` rewrites both variable cells (no per-cell test - on any
  variable change both are redrawn; together ~18 B, still far under a full row):
  - `move_to({row, STATE_TEXT_COL})`, write the 3-wide ON cell for `on`: `"ON"`
    (2 chars reverse-video) + `" "` for state 3; `"ON "` for 2; `"OFF"` for 1;
    three spaces for 0. Fixed width, fully overwritten, no `clear_to_eol`.
  - `move_to({row, EVENT_TEXT_COL})`, format the event into a 15-wide padded field
    and write it. Fixed width, no `clear_to_eol`.
- `_clear_raw(row)` blanks the row without touching the structural `_emit` path
  (it carries `idx = 0xFF`, not `0xFE`).
- New constants beside `STATE_TEXT_COL`: `EVENT_TEXT_COL = STATE_TEXT_COL +
  (STATE_W - 1) + 2 = 20`; ON inner width `STATE_W - 1 = 3`; event inner width
  `EVENT_W - 1 = 15`.
- `_cells_fit()`: `_terminal.cols() >= EVENT_TEXT_COL + (EVENT_W - 1) + 2`. On a
  too-narrow terminal the fixed columns may be clipped, so patching is unsafe;
  fall back to a full row redraw (which already clamps via `_desc_w`).

## Lifecycle / keeping the shadow honest

- **`focus()`** - the page switch already cleared the screen. Set every `_row`
  slot to `{0xFF, 0}` (blank/unknown), alongside the existing `_painted_data = 0;
  _request_repaint();`. The next repaint redraws all visible content; rows that
  should stay blank already match.

- **`_drain_scroll()`** - `scroll_region(DATA_TOP, data_bottom, up=down, 1)`
  shifts the data band on screen without redrawing, so shift `_row` across the
  band in lockstep before painting the newly exposed row:
  - down (content up): `_row[i] = _row[i+1]` for `i` in `[DATA_TOP, data_bottom)`.
  - up (content down): `_row[i] = _row[i-1]` for `i` in `(DATA_TOP, data_bottom]`
    descending.
  The exposed row is painted by the existing `_draw_data_row` call (updates its
  own slot). A rule-arrow flip in the same path routes through `_draw_rule` ->
  `_emit` and updates its structural slot.

- **Bulk clear in `_paint_step`** (set shrank, `_terminal.clear_rows(...)`) - those
  data rows are blanked on screen without `_draw_data_row`; set their `_row` slots
  to `{0xFF, 0}` so the staged repaint sees them as already blank and skips the
  redundant re-clear.

## Data flow

```
_paint_step / _drain_scroll / _page_scroll
        |
        +-- structural: _draw_grid_row / _draw_rule / _clear_row
        |        -> _emit -> FNV vs _row[row] (idx 0xFE) -> skip or redraw line
        |
        +-- data: _draw_data_row(row, vi)
                 new_idx vs _row[row].idx
                 ==  : sig changed? rewrite ON + EVENT cells only (else skip)
                 !=  : _draw_full_row (CODE/DESC changed) + reset _row[row]
```

## Correctness notes

- The diff is purely an output optimization. `_collect()`, `_changed()`,
  `_snapshot()`, scroll math, and the staged-paint budget are unchanged, so what
  the table shows is identical; only redundant bytes are suppressed.
- Identity is keyed on `idx`, compared directly - never folded into a hash - so a
  reorder that swaps two alarms with a coincidentally equal ON/event sig is still
  caught (their `idx` differs) and triggers a full CODE/DESC redraw.
- The data `sig` is an exact pack of the only two mutable cells (ON enum +
  minute-bucketed event), so a same-identity patch never misses a visible change
  and never makes a spurious one - no collision reasoning needed on data rows.
- The structural FNV hash could in theory collide (skipping a needed redraw of a
  header/rule), but with a 31-bit hash over short ASCII this is vanishingly
  unlikely, and a focus fully refreshes, so any stale structural row self-heals on
  the next page switch.
- The `idx` tag (data idx / 0xFE structural / 0xFF blank) keeps the three row
  kinds from ever matching each other's signatures.

## Testing

No automated test suite exists in the repo. Verification is manual on the
simulator:

1. Build `botanik_sim` clean.
2. On the Alarms page, drive alarm transitions (mock PLC `fill_output`) and
   confirm the table matches today's behaviour: re-sort order, ON badge video
   (active/unack vs ack vs OFF vs blank), LAST EVENT timestamps, footer counts,
   and the rule scroll arrows.
3. Exercise UP/DOWN/PAGE_UP/PAGE_DOWN/ENTER/SPACE (mode cycle) and a page switch
   away-and-back; confirm no stale or missing rows or cells.
4. Narrow-terminal check: shrink width below `EVENT_TEXT_COL + EVENT_W` and
   confirm rows still render (full-redraw fallback engaged).
5. Sanity-check the win with a temporary counter (rows redrawn, cells patched,
   rows skipped); confirm a quiet refresh skips the structural and unmoved rows
   and patches only the changed cells. Remove the counter before finishing.

## LoC estimate

~60-75 net added lines in `alarms.h`: constants + `RowShadow` + `_row[]` + `_hash`
(~16), `_emit` (~14), structural rewiring (~+4), `_draw_data_row` cell logic with
inline patches + `_on_state` / `_pack` / `_cells_fit` (~30), focus invalidate
(~2), `_drain_scroll` shadow shift (~6), bulk-clear shadow reset (~2). The actual
delta is reported after implementation (the measurement that motivated doing this
on a branch).
