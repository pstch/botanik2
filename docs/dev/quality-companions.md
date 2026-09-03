# Which rows get a `_qlt` companion {#dev_quality_companions}

A quality is not a field of a row. It is the VALUE of a row named `<row>_qlt`,
carrying a `Vars::Quality` code, and it is the only place a quality is stored.
`Bank::freeze()` pairs the two by name; `Var::quality()` is one indirection
through `Slot::qlt_row`. See `vars.md` for the model.

So "which rows have a quality" is "which rows get a companion", and that is a
rule the generator applies rather than a judgement anybody makes per row.

## The rule

**One companion per INPUT.** Every row in `analog_in`, `digital_in`,
`modbus_in` and `pulse_in`. A reading can always stop being one: a device goes
quiet, answers with nonsense, or refuses the register. Nothing else can put
those three codes anywhere.

**One companion per row an opted-in PROGRAM writes.** A program says so with
`quality = yes` in `tables/programs.csv`, and gets a companion for every row it
writes. A law that can fail to compute has to be able to say so, and the ones
that can are known per program rather than per row.

**Nothing else.** A row with no companion is valid, always - which is right for
a parameter (it has a value from the moment it is declared) and for a computed
row no law hedges about. Companions themselves have none, which is what stops
the regress.

**Alarms are excluded**, even under an opt-in. An alarm is a verdict rather
than a reading: `AlarmController` takes the boolean and has nothing to
substitute, so a quality beside it would be a row nothing consults.

On `gh_mlb41` that is 127 companions - 82 inputs and 45 program outputs.

## What an opt-in obliges

A program that opts in owes a quality for every row it writes. The companion
EXISTS for each of them, and one nothing writes sits at `Q_NO_DATA` for the
life of the run - so the output reads unbelievable and nothing says why.
`tables.py plant` refuses the build on it, read off matiec's own
`__SET_EXTERNAL` list, the same way it refuses an output no program writes.

That is the whole enforcement. There is no way for a law to write a quality it
did not earn, because there is no second truth for it to contradict: the byte
it writes is the byte `Var::valid()` reads.

## Reading and writing one in ST

`plc/functions/quality.st` declares the enumeration and three functions.
matiec numbers an implicit enumeration 0,1,2... in declaration order, which is
the order `descriptor.h` declares the codes in - so the ST type and
`Vars::Quality` are the same type, and the order in either file cannot move.

```
measured             := GOOD(cli_temp_hi_qlt);
cli_dTStrat_degC_qlt := WORST_OF(cli_temp_hi_qlt, cli_temp_lo_qlt);
cli_phase_qlt        := QUAL(sol_ready);
```

`WORST_OF` is the one worth reaching for: it carries the REASON downstream, so
a law three rows away learns that a probe went stale rather than only that the
number is not to be trusted. `QUAL` is for a law with no input verdict to
inherit - a block output, a clock that has not been set.

## Where the substitution happens

`Ref::get()` reads the row's DECLARED value when the companion is not
`Q_VALID`. Once, there, for the laws. `Var::value()` does NOT substitute: the
monitor page and the telemetry show the number that was implausible with the
quality beside it, which is what an operator chasing a dead probe needs, and
`Ref::held()` is the same read for a law that must have the raw one - a
totaliser difference, where a constant at either end makes the subtraction
about nothing.

This is why most laws need no quality read at all. A row that is not believable
already hands a law a number somebody chose and reviewed. A companion is worth
READING only where the law must do something structurally different: shut the
roof on an unmeasured sky rather than compute on a calm one, reset a PID
integral, decline to judge a drain.
