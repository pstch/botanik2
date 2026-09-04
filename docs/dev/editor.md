# Editor reconciliation {#dev_editor}

How `TextEditor` (`console/editor.h`) repaints a multi-row line after an edit
while emitting the **fewest possible bytes** over the serial link. The UI drives a
low-bandwidth terminal, so a naive "clear and redraw everything" repaint is not
acceptable.

**Where this sits.** The editor does not write to the wire. Its ops paint into the
terminal's FRONT grid, and `present()` diffs that against what is on the wire and
paces the result (see @ref dev_screen). The editor paints the way every page
does: immediate mode, the whole viewport, every edit and every motion. The diff
drops the cells that did not change, expresses a row that slid sideways as one
escape, and erases a tail that went blank with one more.

What the editor still contributes, and the reason it is not just a page, is the
**vertical** half: which rows moved up or down, and by how much. The diff cannot
infer that (a renderer's intent is ambiguous across rows), so the editor
announces it through `scroll_region()` before it paints, and the announcement's
**placement** decides whether the diff then finds a slid row or a rewritten one.

## Buffer, layout, reconcile

Every edit runs the same three steps:

```
 1. mutate     edit the byte buffer (_buf) in place
 2. lay out    _snapshot_pre()             (snapshot the old layout)
               _rebuild_layout()           (recompute rows from _buf)
 3. reconcile  announce the rows that moved, then paint the viewport
```

A **layout** (`TextView`) is the buffer sliced into screen rows (one `RowView` each).
Three prompt classes start a row: **PS1** (first row), **PS2** (after a soft wrap),
**PS3** (after a hard `\n`). Auto-wrap is off - the editor owns all wrapping.

The split is what lets a renderer distinguish a line the operator started from its
overflow: the shell uses it for `botanik> ` / `> ` / `...> `, NOTES for `"> "` on
PS1+PS3 and `"  "` on PS2. The shell's PS3 is three columns wider than its PS2,
so a hard break there moves every character of every row below it; NOTES keeps
the three the same width and a class flip is two cells.

\startuml "TextView: the buffer sliced into screen rows (one RowView per row)"
class RowView {
  buf_start : uint16_t - offset of the slice in the buffer
  buf_len : uint16_t - content bytes (no trailing newline)
  hard_break : bool - row closed by a literal newline
}
class TextView {
  rows : RowView* - borrowed, owner-supplied (EditorStorage)
  count : uint16_t
}
TextView *-- "rows" RowView
\enduml

```
 buffer:  "the quick brown fox\njumps"     (cols 12, PS1="> ", PS2="· ")

 _layout: row0  PS1 "the quick "  buf[0..10)  hard_break=false  ┐ soft-wrapped
          row1  PS2 "brown fox"   buf[10..19) hard_break=true   ┘ then hard \n
          row2  PS3 "jumps"       buf[20..25) hard_break=false
```

## The reconcile

`_reconcile()` is four announcements and a paint, in this order:

1. **Grow the band** (`_grow_editor`, bottom-anchored only). A taller text scrolls
   the content band up so the added rows fit and the prompt's last row stays on
   the bottom. Never above row 1: text taller than the band stays put there and
   the viewport follows the cursor over it, as under `Anchor::TOP`.
2. **Place the one IL/DL** (`_find_splice_span` + `_reposition_tail`). A single
   contiguous edit changes the row count by some `row_delta`, and the rows below
   some boundary are the pre-edit rows displaced by exactly that. One IL/DL at
   the boundary slides them into place, so the paint finds them where they are
   and the diff pays nothing for them.
3. **Follow the cursor** (`_follow_cursor`). If the edit pushed the cursor out of
   the viewport, scroll the viewport by the difference so the kept rows slide
   rather than repaint. A jump of a whole window or more slides nothing.
4. **Paint the viewport** (`_paint_view`): prompt, content and a clear to the
   margin for every row the window shows, the clear alone past the text.
5. **Shrink the band** (`_shrink_editor`, bottom-anchored only). The grow's mirror:
   the whole content band slides down by the rows the text gave back, so the
   editor lands on the rows already showing it and whatever stood above it
   follows. The shell then fills only the rows that opened at the top, from its
   scrollback. Measured against blanking the vacated rows and repainting at the
   new origin, the slide wins by 4 to 48 bytes.

`reset()`, `commit_loaded()` and `replace_all()` are the same sequence over a
buffer set whole by `_load()`; `commit()` alone paints without it, since the
caller has line-fed past the submitted text and the transcript owns it now.

### Placing the IL/DL

The boundary is a choice, and it is load-bearing: a scroll placed where content
did not actually move displaces rows out from under the diff's shift detection and
costs a full rewrite of each. `_find_splice_span()` scores every candidate
boundary by the **bytes that stay on the screen row already showing them**,
counted over rows on screen, and announces the best one. A row the diff can
slide is worth its length; one it has to rewrite is worth nothing however well
its slice matched.

```
 pre (3 rows)                post (4 rows, hard break at the head)
 R0  botanik> t[0..71)       P0  botanik>                <- empty, IL'd
 R1  > t[71..149)            P1  ...> t[0..75)           <- R0 slid by 4
 R2  > t[149..180)           P2  > t[75..153)            <- R1 slid by 4
                             P3  > t[153..180)           <- R2 slid by 4
```

Here the identical-suffix rule this replaced found nothing (every length
changed) and every row was rewritten: 234 bytes. The IL at the top keeps 172
bytes on their rows, each then one DCH: 89 bytes.

The scroll itself costs a pair of escapes (bounded to the band, the terminal
emits a DL and an IL), so a tail has to keep more than `SCROLL_COST` bytes to
be announced at all. Joining two short lines is cheaper as an append and an EL
than as a slide.

A `row_delta` of 0 has no boundary to choose, and the paint alone does the
edit: the diff sees each row as a prefix that changed and content that slid.

## Geometry: anchor and viewport

Where the editor's rows land on screen is two settings, not two editors.

**`Anchor::BOTTOM`** (the shell prompt) pins the LAST row to the content bottom, so log
output scrolls above it. An edit that changes the row count scrolls the whole content
band: `_grow_editor()` / `_shrink_editor()` move `_origin` and scroll rows `1..bottom` to
match. The text pushes the screen around.

**`Anchor::TOP`** (the NOTES page) fixes `_origin` and lets the **viewport** move
instead. The text stays put and a window slides over it.

The viewport is `_view_top` (the first layout row on screen) plus `_view_rows` (how many
rows of screen it may use, from `_origin`). Every paint site maps through them:

```
 _hw_row(i)  = _origin + (i - _view_top)     screen row showing layout row i
 _visible(i) = _view_top <= i < _view_top + _view_height()
```

Bottom-anchored is the degenerate case while the text fits: `_view_top` is 0 and the
height runs to the content bottom, which makes `_hw_row(i)` exactly `_origin + i`.
Once the text is taller than the band the origin clamps at row 1 and the same
viewport takes over, so the band shows the tail with the cursor's row on it and
neither the status bar nor the alarm strip is painted across. One code path.

`_follow_cursor()` is what moves the window. When the cursor leaves it, it emits
**one scroll**, and the paint behind it covers the rows the scroll uncovered.

### The kill ring

Cut and paste (`Ctrl+K` / `Ctrl+W` / `Ctrl+Y`) are the editor's, over ONE buffer for the
whole console rather than one per instance. That is deliberate: the point of a clipboard
on a terminal with no mouse is to move text between the places that hold it, and a
per-editor buffer makes each of them a dead end. It used to live on `CommandShell`, which
is why the NOTES page had no cut and paste at all.

The **`Console` owns it** (`Console::kill_ring()`, `console/kill_ring.h`) and everything
reaches it through the console it already holds - `TextEditor` through its `_console`,
a table page through `ScrollTablePage::_console`. Not a global: there is one console per
firmware, so ownership gives the same single clipboard without a free function anything
could write to, and a test fixture's own console brings its own ring, so cases cannot
leak a cut into each other.

The editor is not its only writer - `Alt+W` on a table page puts the selected cell in it
(`ScrollTablePage::_copy_selected_cell()`) - so the ring is plain storage with no editor
in it, and everything that fills it goes through `set()`.

A kill takes only what the ring can hold and leaves the remainder in the buffer, so a yank
always restores exactly what the kill removed. Removing more than was stored was the old
`kill_to_line_end()`'s bug: past 128 bytes it deleted a tail that `Ctrl+Y` could not put
back.

### The mark and the region

`Ctrl+Space` anchors a mark; the span between it and the cursor is drawn reverse-video and
`Ctrl+W` / `Alt+W` act on it. What makes that cheap enough for a 4800-baud link is that
**the highlight rides the cell diff.** A back-buffer cell is a 7-bit glyph plus
`CELL_INV` (`console/terminal.h`), so reverse-video is content as far as `present()`
is concerned: `_paint_view()` writes every row plain, `_paint_region()` inverts the
span on top, and the diff emits only the cells whose attribute flipped - one cell for
one character of selection. Nothing tracks what is highlighted; FRONT is the record.

**A live region holds the hardware cursor off the screen.** `_paint_view()` raises
`AnsiTerminal::set_cursor_inhibited()` with the mark, so the terminal keeps the page's wish
for a cursor and suppresses it while the highlight is up: the far end of the region already
says where the edit point is, and a cursor blinking on that cell only hides it. The inhibit
is a second flag rather than a call to `set_cursor_visible()` so releasing the mark puts
nothing back - the editor never has to know whether the page wanted a cursor at all.

**The mark is transient**: `insert_characters()` and `remove_characters()` drop it, as do
`_load()`, which every bulk change goes through. A mark that survived edits would
need shifting by every insert and clamping by every delete, and a region that has drifted
off its text cuts the wrong bytes silently.

### Capacity

Buffer and row array are supplied by the owner (`EditorStorage` / `EditorBuffers<BUF,ROWS>`),
so a shell prompt and a notes page are sized separately rather than both paying for the
larger. A `static_assert` per instantiation guarantees the buffer fits by soft-wrap at the
minimum width, so the row cap is only reachable through hard `\n` breaks - and an insert
that would cross it is **refused**, because rows past the cap are text nobody can see or
reach.

### Why a whole-line replace is one operation

`replace_all()` exists because the obvious spelling of a history recall is three edits:
move to the end, backspace the lot, insert the new text. Two of those change the row
count, and the shell, which refills the log rows a shrink opens, could account for
exactly one. The recalled text then landed a row off the screen while the buffer held
it, so Enter ran a command that had never been displayed.

One edit, one row-count change, and the same sequence as any edit less the tail slide,
since none of the old text is assumed to be anywhere. `commit_loaded()` is the non-edit
form for text arriving from storage: it clears the dirty flag and leaves the cursor at
the start.

## What holds it to account

Three checks, in `tests/`, and any change to this file or to `present()` is judged by
all of them:

- **`test_editor.cpp`** - an incremental edit leaves FRONT where a full repaint would.
- **`test_editor_wire.cpp`** - an independent VT102 (`vt_model.h`) replays every byte
  the terminal emitted and must land on the screen FRONT asked for, reverse video
  included; and each of twenty-seven edits has a committed byte **ceiling**. Going
  under it is the point and prints the whole ledger; going over it fails. Lower a
  ceiling in the commit that earned it. `BOTANIK_LEDGER=1` prints it regardless.
- **`test_editor_fuzz.cpp`** - seeded random edit streams over each geometry, every
  step held to the VT and every eighth to a repaint. It found a region kill the old
  splice walk lost, and an erase under an inverted pen that both grids called blank.

The ledger's text has no short period (`varied()`), because a uniform fill hid every
cross-row reflow: a character pulled up from the row below was the character it
replaced. The fixture shows the cursor, because both real pages do and the park after
every edit is part of what the link carries.
