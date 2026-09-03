# How an ST program reaches our rows

Settled 2026-08-30, by generating both spellings and compiling them.

**Use `VAR_EXTERNAL`. The formal-parameter connection list is unusable.**

## The connection list does not work

The IEC spelling that looks like `bind()` is a CONFIGURATION connecting a
program's formal parameters to globals:

```
PROGRAM roof WITH fast : roof_lockout(rain_mmph := cli_rain_mmph, ...);
```

matiec parses it and emits, per row, per scan:

```c
{extern REAL *CLI_RAIN_MMPH; ROOF.RAIN_MMPH = *CLI_RAIN_MMPH;}
ROOF_LOCKOUT_body__(&ROOF);
{extern BOOL *CLI_RAINLOCKOUT; *CLI_RAINLOCKOUT = ROOF.RAIN_LOCK;};
```

Two things are wrong with it. It is a COPY IN and COPY OUT every scan rather
than a binding, which is the IEC semantic but not one we want to pay for. And
it does not go through the accessor macros at all - that bare `=` assigns a
value to a `__IEC_REAL_t`, so **it does not compile against matiec's own
runtime either**:

    error: incompatible types when assigning to type '__IEC_REAL_t'
           from type 'REAL' {aka 'float'}

So it is a defect, not a fit we could adapt to. Beremiz does not exercise this
path; it generates its own glue.

## VAR_EXTERNAL is the path

```
PROGRAM roof_lockout
VAR_EXTERNAL
  cli_rain_mmph     : REAL;
  cli_windowLockout : BOOL;
END_VAR
```

matiec then emits, once, at init:

```c
__INIT_EXTERNAL(REAL,CLI_RAIN_MMPH,data__->CLI_RAIN_MMPH,retain)
```

which is the whole of `bind()` - one resolve per row - and the resource body
becomes a single call with no copying:

```c
void M7_run__(unsigned long tick) { if (FAST) ROOF_LOCKOUT_body__(&ROOF); }
```

Reads and writes go through `__GET_EXTERNAL` / `__SET_EXTERNAL`, which are
ours. A program binding rows by NAME is exactly what `VAR_EXTERNAL` means, so
this is not a workaround: it is the spelling that matches what our programs
already do.

## The split that removes anonymous rows

| ST declaration | becomes | costs |
|---|---|---|
| `VAR_EXTERNAL` | `Vars::Ref` into the bank | the row the site already declares |
| `VAR`, `VAR_TEMP`, and an FB's `VAR_INPUT`/`VAR_OUTPUT` | a plain C++ member | nothing |

Only a name the site declares becomes a row. Everything internal to a POU
stays a member, exactly as `Plc::Tof` holds its state today - no row, no
descriptor, no pool. **The anonymous-row problem the block-model plan was
built around does not arise.**

## Case

IEC identifiers are case-insensitive and matiec upper-cases every one, so a
row arrives as `CLI_RAIN_MMPH` where the table spells it `cli_rain_mmph`. The
original case survives nowhere, `VARIABLES.csv` included, so the shim folds
case when it resolves. All 425 site rows stay unique under folding, and a test
should keep that true.

## What is proven

`harness_ext.cpp` - an exploratory harness, since deleted - ran the generated
`roof_lockout`: its bind, its
law and its nested `TOF_MS` - against a real `Vars::Bank`, with no
hand-written binding table. All six cases pass, the two dead-station ones
included.
