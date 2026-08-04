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

### Capacity

Buffer and row array are supplied by the owner (`EditorStorage` / `EditorBuffers<BUF,ROWS>`),
so a shell prompt and a notes page are sized separately rather than both paying for the
larger. A `static_assert` per instantiation guarantees the buffer fits by soft-wrap at the
minimum width, so the row cap is only reachable through hard `\n` breaks - and an insert
that would cross it is **refused**, because rows past the cap are text nobody can see or
reach.
