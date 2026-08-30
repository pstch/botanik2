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

## 4. The experiment: it works

`st/` holds it. `destrat.st` compiled by `iec2c`, its `POUS.c` compiled AS C++,
driving a real `Vars::Bank` through nothing but a replaced `accessor.h`:

```
  gap  7.0 -> demand 1.00  (want 1.00) ok
  gap  5.0 -> demand 0.60  (want 0.60) ok
  gap  3.0 -> demand 0.30  (want 0.30) ok
  gap  0.5 -> demand 0.00  (want 0.00) ok
```

The whole adapter is 41 lines:

```cpp
#define __DECLARE_VAR(type, name) \
    ::Botanik::Vars::Ref< type, ::Botanik::Vars::Dir::INOUT > name;
#define __GET_VAR(name, ...)      ((name) __VA_ARGS__).get()
#define __SET_VAR(prefix, name, suffix, new_value) \
    (prefix name suffix).set((new_value), true)
```

plus the literal macros and a header of IEC type names. **No stage-4 back end,
no visitor over 323 AST classes.** A POU's `_data__` struct becomes a struct of
typed references, and `bind()` fills it exactly as a hand-written program's does.

### What it settled

- **matiec's C compiles as C++** with no edits to the generated files.
- **The `_data__` struct is a Refs struct.** That is stage 3 of the block plan,
  arriving from the other direction.
- **`Ref` keeping its `Bank*` is what makes this work unmodified.** Every access
  is `data__->FIELD.get()`, with no `io` in scope. Dropping the pointer -
  stage 1 of the plan - would need the macros to reach one, and `prefix` is
  `data__->`, so `prefix io` would work IF the struct carried it. matiec emits
  the struct, so that is a real open question rather than a detail.
- **EN, ENO and every private VAR need rows.** The FB body reads `EN` and writes
  `ENO`; unbound references are a null `io` and a segfault. Three rows per
  STAIRCASE3 instance before its interface is counted, which is the cost stage 2
  is paying and it is larger than the plan's estimate.
- **matiec already honours forcing.** Its own `__SET_VAR` refuses to write a
  variable with `__IEC_FORCE_FLAG`. Our override, in its runtime.

### The hour it cost

The site config defines must match the ones `libbotanik.a` was built with -
they size `Vars::Bank`, so a mismatch is an ODR violation. It presents as a
bank whose every row reads as the first one, which looks like a matiec problem
and is not.

## 5. What to test next, in order

1. **Generate `bind()` too.** The harness writes it by hand. matiec already
   emits the connection list into `gh_mlb41.c`, and `VARIABLES.csv` lists every
   variable with its type - so both the table and the wiring are in the output
   already, in a form a small script could turn into our declarations.
2. **Where does quality come from?** The `flags` byte exists but matiec only
   sets FORCE and RETAIN. The earlier conclusion still holds: blocks propagate
   it, ST statements do not express it. That means our block library stays
   hand-written C++ and ST describes only the laws.
3. **What happens to our existing blocks?** `Tof`, `Ewma`, `Pid` are C++
   classes; an ST program calling `TON` expects matiec's `_data__` + `_body__`
   shape. Either we write ours in ST too (but the standard ones in matiec's
   `lib/*.txt` are GPL - write our own), or the shim adapts.
4. **A whole site.** destrat is one program with one FB. The question is whether
   29 programs and 56 block instances still hold together.

## 5. What is NOT answered

- Whether the generated C compiles as C++ without edits (it is C99-flavoured).
- SFC and IL back ends are dead weight for us but are in the same tree.
- Whether `iec2c`'s output is stable enough across versions to commit.
