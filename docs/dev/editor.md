# Editor reconciliation {#dev_editor}

How `TextEditor` (`console/editor.h`) repaints a multi-row line after an edit
while emitting the **fewest possible bytes** over the serial link. The UI drives a
low-bandwidth terminal, so a naive "clear and redraw everything" repaint is not
acceptable.

**Where this sits.** The editor does not write to the wire. Its ops paint into the
terminal's FRONT grid, and `present()` diffs that against what is on the wire and
paces the result (see @ref dev_screen). So the per-row splicing
below is byte-neutral on its own - a spliced row and a fully repainted row leave
FRONT identical, and the diff emits the same either way.

What the editor still contributes, and the reason it keeps this machinery when
every other renderer dropped its own, is **where the single IL/DL goes**. It is
the one renderer that reflows content across rows *and* changes its row count in
the same edit, and a scroll placed where content did not actually move is worse
than no scroll at all: it displaces rows out from under the terminal's per-row
shift detection and costs a full rewrite of each. `_find_splice_span()` computes
that placement; a heuristic in its place measured 195 bytes where the search
costs 25.

## Buffer, layout, reconcile

Every edit runs the same three steps:

```
 1. mutate     edit the byte buffer (_buf) in place
 2. lay out    _snapshot_pre()             (snapshot the old layout)
               _rebuild_layout()           (recompute rows from _buf)
 3. reconcile  diff _pre against _layout, emit minimal terminal ops
```

A **layout** (`TextView`) is the buffer sliced into screen rows (one `RowView` each).
Three prompt classes start a row: **PS1** (first row), **PS2** (after a soft wrap),
**PS3** (after a hard `\n`). Auto-wrap is off - the editor owns all wrapping.

The split is what lets a renderer distinguish a line the operator started from its
overflow: the shell uses it for `botanik> ` / `> ` / `...> `, NOTES for `"> "` on
PS1+PS3 and `"  "` on PS2. Widths must match across the three, or the wrap column
would move between rows (`_row_cap()` subtracts the prompt).

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

## The reconcile diff

`_reconcile()` diffs the two same-typed layouts instead of repainting. A single
contiguous edit changes the screen in a very constrained way, which it exploits:

```
 _pre (before)        _layout (after inserting one char)
 row0  ...........    row0  ...........   above the edit: untouched
 edit  ...........    edit  ..X........   splice at the cursor
 row2  shifted    ->  row2  shifted       absorbed tail: displaced by one
 row3  shifted        row3  shifted       IL/DL, glyphs not rewritten
```

1. **Find the edit row** - the first row the edit touches (`_find_splice_span`).
   Rows above it are correct and never touched.
2. **Find the absorbed tail** - the longest bottom run of rows identical except
   for a constant buffer shift. They moved but their glyphs didn't change.
3. **One IL/DL** (`_reposition_tail`) slides that tail to its new rows and (on
   insert) opens the blank rows the new content paints into - at most one
   insert-lines / delete-lines for the whole edit.
4. **Forward per-row splice walk** from the edit row down to the absorbed tail
   (`_splice_row`). Each affected row is fixed in place:
   - the **edit row** splices the inserted/removed run at the cursor column;
   - **rows below** get a cheap left/right splice by the displacement, cascading
     the pushed-off character into the next row;
   - a row whose old glyphs are unrelated to the wanted content is full-painted
     (`_new_row`) - byte-optimal when a cheap splice wouldn't be.

## Why it pays off

Typing one character mid-line emits roughly *one insert char + one cursor move*,
not a row (or screen) repaint. The byte cost scales with what actually changed,
not with the buffer size - the whole point on a serial link measured in hundreds
of baud. See @ref dev_input for how the bytes that *trigger* an
edit arrive in the first place.

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

Bottom-anchored is the degenerate case: `_view_top` is 0 and the height runs to the
content bottom, which makes `_hw_row(i)` exactly `_origin + i` and `_visible(i)` exactly
the old clip against `rows()`. One code path, two behaviours.

`_follow_cursor()` is what moves the window, and only under `Anchor::TOP` - bottom-anchored,
`_grow_editor()` already moves the text, and doing both would double up. When the cursor
leaves the window it emits **one scroll plus the rows that scroll uncovered**; a jump
further than the window height clears and repaints instead, since nothing on screen
survives it either way.

### The pre-visibility rule

The forward walk maps post row *i* onto the pre row physically present after the IL/DL,
and splices against what that row *shows*. Once the window can move, "present" is not the
same as "in the layout": after a DL, the rows sliding up into the bottom of the window
came from **below** it, so the screen row is blank, not stale. Splicing against glyphs it
never had corrupts it.

`_was_visible()` (against the pre-edit `_pre_view_top` / `_pre_view_height`, snapshotted with
the layout) is the guard. A row that fails it drops to `_new_row()` - a repaint, which is
what it needed anyway.

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
`Ctrl+W` / `Alt+W` act on it. Two things make that cheap enough for a 4800-baud link.

**The highlight rides the cell diff, not the splice.** A back-buffer cell is a 7-bit glyph
plus `CELL_INV` (`console/terminal.h`), so reverse-video is content as far as `present()`
is concerned: re-stamp the visible rows through `_new_row()` and the diff emits only the
cells whose attribute flipped - one cell for one character of selection. `_write_row_content()`
splits a row into up to three pen runs, the same shape as
`ScrollTablePage::_draw_selected_line()`.

The region is deliberately NOT taught to `_splice_row()`. That path writes partial runs with
`insert=true`, which slides `CELL_INV` bits sideways along with the glyphs, and its
`delete_chars` / `clear_to_eol` stamp blanks with whatever pen is current. A highlight
cannot survive it, and making it try would mean reopening the reconciliation.

**`_sync_region()` is a no-op unless the region moved.** It compares the region against
`_painted_from`/`_painted_to`/`_painted_active` and returns without touching a cell when
they agree - which is every call with no mark, so editing without one costs exactly what it
did before the region existed. It hangs off two places: the end of `_settle_cursor()` (a
motion is the only thing that moves the region's far end, and nothing else on that path
repaints anything) and the end of `_reconcile()` (after `_grow_editor()`/`_shrink_editor()`
have done their `scroll_region()`, so the screen matches `_layout` on return - the invariant
the next `_snapshot_pre()` assumes).

**The mark is transient**: `insert_characters()` and `remove_characters()` drop it, as do
`_reset_state()` and `commit_loaded()`. A mark that survived edits would need shifting by
every insert and clamping by every delete, and a region that has drifted off its text cuts
the wrong bytes silently. `tests/test_editor_bytes.cpp` pins both halves of the cost claim.

### Capacity

Buffer and row array are supplied by the owner (`EditorStorage` / `EditorBuffers<BUF,ROWS>`),
so a shell prompt and a notes page are sized separately rather than both paying for the
larger. A `static_assert` per instantiation guarantees the buffer fits by soft-wrap at the
minimum width, so the row cap is only reachable through hard `\n` breaks - and an insert
that would cross it is **refused**, because rows past the cap are text nobody can see or
reach.
