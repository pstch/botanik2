# Which rows get a `_qlt` companion {#dev_quality_companions}

A quality is not a field of a row. It is the VALUE of a row named `<row>_qlt`,
carrying a `Vars::Quality` code, and it is the only place a quality is stored.
The generator emits the companion IMMEDIATELY AFTER the row it speaks for, so
the pair is adjacent and `F_QLT` on the first is the whole link: `Var::quality()`
is one indexed load, there is no index to store and no pairing pass to run. See
`vars.md` for the model.

So "which rows have a quality" is "which rows get a companion", and that is a
rule the generator applies rather than a judgement anybody makes per row.

## The rule

**One companion per INPUT.** Every row in `analog_in`, `digital_in`,
`modbus_in` and `pulse_in`. A reading can always stop being one: a device goes
quiet, answers with nonsense, or refuses the register.

**One companion per row an opted-in PROGRAM writes.** A program says so with
`quality = yes` in `tables/programs.csv`, and gets a companion for every row it
writes. A law that can fail to compute has to be able to say so, and the ones
that can are known per program rather than per row.

**Nothing else.** A row with no companion is good, always - which is right for
a parameter (it has a value from the moment it is declared) and for a computed
row no law hedges about. Companions themselves have none, which is what stops
the regress.

**Alarms are excluded**, even under an opt-in. An alarm is a verdict rather
than a reading: `AlarmController` takes the boolean and has nothing to
substitute, so a quality beside it would be a row nothing consults.

On `gh_mlb41` that is 127 companions.

## Three codes

| Code | Meaning | `USABLE` | `GOOD` |
|---|---|---|---|
| `Q_NONE` | nothing usable behind the row | no | no |
| `Q_GOOD` | measured, restored, or forced | yes | yes |
| `Q_HELD` | a law holds a number it stands behind | yes | no |

There were six. The four that went - stale, implausible, refused, and the
never-written zero - were DIAGNOSES rather than decisions: no reader did
anything different about them, and each has a home of its own now.

| Was | Is |
|---|---|
| `Q_STALE` | `Q_NONE`, and the stamp's AGE on the monitor page |
| `Q_IMPLAUSIBLE` | `Q_NONE`, and the value shown outside the band beside it |
| `Q_REFUSED` | `Q_NONE`, and one log line from the RTU master at the transition, naming the row and the device |
| `Q_NO_DATA` | `Q_NONE`, with `S_WRITTEN` saying whether anything ever wrote the slot |

Collapsing them is what makes the codes DECISIONS. `USABLE` is "is there a
number a law should compute on"; `GOOD` is "can the plant be seen". Those are
the only two questions any reader asks, and every code answers both.

## What an opt-in obliges

A program that opts in owes a quality for every row it writes. The companion
EXISTS for each of them, and one nothing writes sits at `Q_NONE` for the life of
the run - so the output reads unusable and nothing says why. `tables.py plant`
refuses the build on it, read off matiec's own `__SET_EXTERNAL` list, the same
way it refuses an output no program writes.

That is the whole enforcement. There is no way for a law to write a quality it
did not earn, because there is no second truth for it to contradict: the byte
it writes is the byte `Var::valid()` reads.

## Reading and writing one in ST

`plc/functions/quality.st` declares the enumeration and four functions. matiec
numbers an implicit enumeration 0,1,2... in declaration order, which is the
order `descriptor.h` declares the codes in - so the ST type and `Vars::Quality`
are the same type, and the order in either file cannot move. The accessor's
member block is GENERATED from `descriptor.h` into
`plc/runtime/quality.gen.h`, so the two cannot drift.

```
measured             := GOOD(cli_temp_hi_qlt);
cli_dTStrat_degC_qlt := WORST_OF(cli_temp_hi_qlt, cli_temp_lo_qlt);
cli_phase_qlt        := QUAL(sol_ready, FALSE);
```

Four helpers, and the shape of each is the point.

`GOOD(q)` asks MEASURED. It is what a law gating on whether the plant can be
SEEN uses, and the common read.

`USABLE(q)` asks whether there is a number behind the row worth computing on -
wider than `GOOD` by exactly `Q_HELD`. `Ref::get()` branches on this one.

`WORST_OF(a, b)` is how a derived row inherits its inputs': the weaker of the
two. Spelled as comparisons rather than as a minimum, because the ordering of an
enumerated type is not something to lean on. Two inputs; ONE input is plain
assignment (`cli_coolDem_qlt := cli_temp_in_qlt`) and wants no helper.

`QUAL(OK, SEEDED)` is what a law writes with no verdict to inherit, and it takes
SEEDED always - a one-argument form let eighteen sites skip the question and
publish "nothing here" for a row their own filter was still holding. Pass FALSE
where the law genuinely holds nothing, and let that be a claim rather than a
default.

A library block that filters or holds takes `Q_IN : QUALITY` and answers
`Q : QUALITY`, computing `QUAL` inside: `EWMA`, `DC_RAIL`, and the site's
`AC_MIRROR`, `AC_SUPPLY` and `ZONE_PROBES`. The `OK_IN` / `OK` / `SEEDED` pins
are gone, and with them the conversion lines every opted-in program used to
write around them.

## Where the substitution happens

`Ref::get()` reads the row's DECLARED value unless the companion is `USABLE`.
Once, there, for the laws.

`Q_HELD` is the code for a number a LAW stood behind that nothing measured:
`temp_rh_zone` with every interior probe gone publishes the hottest of the held
levels and the outside, and every conditioning block publishes its held average
through `QUAL(OK, SEEDED)`. Readers compute on it, because the declared 14 degC
would stop the roof venting on the day it matters most, and a fallback may only
ever ask for MORE venting. `GOOD` still says no, so the cooling loop still resets
its integral and the intake gates still shut - "is there a number worth using"
and "can the plant be seen" are different questions and this is the code that
separates them.

`Var::value()` does NOT substitute: the monitor page and the telemetry show the
number that was implausible with the quality beside it, which is what an operator
chasing a dead probe needs. A law that must have the raw one - a totaliser
difference, where a constant at either end makes the subtraction about nothing -
reads it through `Var::live()` from C++; there is no ST spelling for it, because
a law that wants the unbelievable number should say so out loud.

This is why most laws need no quality read at all. A row that is not usable
already hands a law a number somebody chose and reviewed. A companion is worth
READING only where the law must do something structurally different: shut the
roof on an unmeasured sky rather than compute on a calm one, reset a PID
integral, decline to judge a drain.
