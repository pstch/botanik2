# Rebuilding on matiec: what the source actually says

Investigated 2026-08-30 on branch `matiec`. matiec cloned from
`github.com/beremiz/matiec`, built here, and run on a real program.

## 1. It builds, and the round trip works

flex and bison are not installed and there is no passwordless sudo, but
`apt-get download` needs neither: the `.deb`s extract into a local prefix and
`autoreconf && ./configure && make` then succeeds. Binaries: `iec2c` (the C back
end) and `iec2iec` (a pretty-printer back to IEC text).

`st/destrat.st` is `destrat_controller` written as ST - a `FUNCTION_BLOCK` for
the staircase, a `PROGRAM` for the law, and a `CONFIGURATION` naming the global
rows, the task and the wiring. **It compiled first try**, including the
configuration:

```
TASK slow (INTERVAL := T#100ms, PRIORITY := 1);
PROGRAM destrat_inst WITH slow : destrat(strat := cli_dTStrat_degC, ...);
```

which is our rate group and our `bind()`, in standard syntax.

## 2. Licence: the split is the good one

- The COMPILER (`stage1_2`, `stage3`, `stage4`, `absyntax`) is **GPLv3**.
- The runtime headers that end up in generated code (`lib/C/*.h`) are
  **LGPLv3** (`lib/COPYING.LESSER`).

So anything we write against matiec's internals is GPLv3, but it is a
DEVELOPER TOOL: under the "generate once, commit readable output" discipline it
never links into the firmware, exactly as GCC does not infect what it compiles.

**The one trap is `lib/*.txt`**: matiec's ST definitions of the standard blocks
(TON, TOF, CTU...) are GPLv3, and a translation of them is a derivative work.
Never feed those through our toolchain - we have our own `Ton`, `Tof`, `Ewma`.

## 3. The finding that changes the plan

matiec's generated C does **not** touch variables directly. Every access goes
through macros defined in the replaceable `lib/C/accessor.h`:

```c
#define __GET_VAR(name, ...)      name.value __VA_ARGS__
#define __SET_VAR(prefix, name, suffix, new_value)\
    if (!(prefix name.flags & __IEC_FORCE_FLAG)) prefix name.value suffix = new_value
```

Three things follow.

**Its value model is ours.** `__IEC_<type>_t` is `{ value; flags; }` - a value
plus a flags byte carrying FORCE and RETAIN. That is a `Slot` with a quality
byte, and `__SET_VAR` already refuses to write a FORCED variable, which is the
override mute we removed and may reintroduce.

**`prefix` and `name` are separate arguments.** A generated write reads
`__SET_VAR(data__->, DEMAND, , value)`. So a replacement macro can reach
`prefix io` - a bank pointer held once per POU instance in `data__` - which is
better than either option in the plan: not one `Bank*` per `Ref`, and not an
`io` argument threaded through every call.

**So the integration may be an `accessor.h`, not a stage 4.** Writing a back end
means a visitor over **323 AST node classes**; matiec's own pretty-printer, which
does nothing but echo its input, is 2186 lines, and `generate_c` is 17k. Whereas
replacing the accessor macros so matiec's EXISTING C back end addresses our bank
is on the order of hundreds of lines.

That is the difference between a quarter and a fortnight, and it is worth
proving before anything else is decided.

## 4. What to test next, in order

1. **Can a redefined `accessor.h` compile?** Take `out/POUS.c` from this
   experiment, write an `accessor.h` that maps `__GET_VAR`/`__SET_VAR` onto
   `Vars::Bank`, and see what breaks. This is the whole bet.
2. **Does `data__` have room for the bank pointer?** matiec emits the struct;
   we would need `__DECLARE_VAR` to add one, or the POU function to take it.
3. **What does a FUNCTION_BLOCK instance become?** Our blocks are C++ classes
   with `bind()` and `operator()`; matiec emits a `_data__` struct and a
   `_body__` function. Both are "state plus a call", but the shapes differ.
4. **Where does quality come from?** The `flags` byte exists but matiec only
   sets FORCE and RETAIN. A quality bit would be ours to add, and the earlier
   conclusion still holds: blocks propagate it, ST statements do not express it.

## 5. What is NOT answered

- Whether the generated C compiles as C++ without edits (it is C99-flavoured).
- SFC and IL back ends are dead weight for us but are in the same tree.
- Whether `iec2c`'s output is stable enough across versions to commit.
