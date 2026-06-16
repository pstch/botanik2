# Editor Reconcile

How `TextEditor` (`terminal/editor.h`) repaints a multi-row line after an edit
while emitting the **fewest possible bytes** over the serial link. Because the UI
drives a low-bandwidth terminal, a naive "clear and redraw everything" repaint is
not acceptable.

## Buffer, layout, reconcile

Every edit runs the same three steps:

```
 1. mutate     edit the byte buffer (_buf) in place
 2. lay out    _pre = _layout              (snapshot the old layout)
               _rebuild_layout()           (recompute rows from _buf)
 3. reconcile  diff _pre against _layout, emit minimal terminal ops
```

A **layout** (`TextView`) is the buffer sliced into screen rows. Each `RowView`
is `{buf_start, buf_len, hard_break}` - where a row begins in the buffer, how
many content bytes it shows, and whether it is closed by a literal `\n`. Three
prompt classes start a row: **PS1** (first row), **PS2** (after a soft wrap),
**PS3** (after a hard `\n`). Auto-wrap is off - the editor owns all wrapping.

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
        _pre (before)          _layout (after inserting one char on row 0)
        ┌──────────────┐       ┌──────────────┐
 row0   │ unchanged    │       │ unchanged    │  rows above the edit: untouched
        ├──────────────┤       ├──────────────┤
 edit ▶ │ ...........  │       │ ..X........  │  edit row: splice at the cursor
        ├──────────────┤       ├──────────────┤
 row2   │ shifted      │  ───▶ │ shifted      │  absorbed tail: only displaced,
 row3   │ shifted      │       │ shifted      │  shifted by one IL/DL, no rewrite
        └──────────────┘       └──────────────┘
```

1. **Find the edit row** - the first row the edit touches (`_find_splice_span`).
   Rows above it are correct and never touched.
2. **Find the absorbed tail** - the longest bottom run of rows that are identical
   except for a constant buffer shift. They moved but their glyphs didn't change.
3. **One IL/DL** (`_reposition_tail`) slides that tail to its new rows, and (on
   insert) opens the blank rows the new content paints into. At most one
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
not with the size of the buffer - which is the whole point on a serial link
measured in hundreds of baud. See @ref md_docs_2EVENT__DISPATCH for how the bytes
that *trigger* an edit arrive in the first place.

## Bottom-pinned geometry

The editor sits at the bottom of the screen so log output can scroll above it.
When an edit grows or shrinks the row count, `_grow_editor()` / `_shrink_editor()`
scroll the editor block up or down and re-pin `_origin`, so the prompt's first
row stays anchored to the bottom. Layout is capped at `Config::EDITOR_MAX_ROWS`;
a `static_assert` in `libbotanik_config.h` guarantees the buffer always fits by
soft-wrap at the minimum width, so the cap is only reachable through hard `\n`
breaks.
