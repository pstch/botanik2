# Local I/O {#dev_io}

The terminals on the controller itself, as against the ones on the field bus
(@ref dev_modbus). One component, `Io::LocalInputs`, and one HAL namespace,
`HAL::DigitalIn`.

The rule that shapes everything here: **a program reads a terminal by NAME**,
exactly as it reads a remote register. Nothing above the scan knows which side
of the boundary a bit came from, which is what lets a site move a contact from
the PLC to an expansion module by editing one CSV cell.

## The channel map

The Arduino OPTA has eight input terminals, I1 to I8, wired to A0 to A7. A row
declares one by naming **device 0** - `xp_of()` reads that as `XP_LOCAL` - and
putting the channel in the register column, which a local row has no other use
for.

| Column | On a field-bus row | On a local row |
|---|---|---|
| `dev` | the Modbus unit id | 0, meaning this controller |
| `fc` | the function code | unused; write 2, as a discrete input |
| `reg` | the register or coil | the terminal: 0 is I1, 7 is I8 |
| `timeout` | how long a reading stays fresh | 0: a terminal cannot go stale |

The same eight terminals can be read as **levels** or counted as **edges**, and
the two are different namespaces because they are different questions.
`HAL::DigitalIn` answers "what is it now"; `HAL::Pulse` answers "how many since
boot" and needs an interrupt, which I1 and I4 cannot both have - they share an
EXTI line. The `isr` column is what picks the second, and `vars_checks.py`
refuses the pair.

## What the scan does

`LocalInputs` walks the table once at bring-up, finds every `XP_LOCAL` +
`SCOPE_DI` row, and attaches its terminal. There is no channel table beside the
variable table, because the variable table **is** it - the defect that gets
avoided is the one where a row says I3 and the firmware attaches I4.

Per beat, at `PHASE_INPUT` so the programs run on this beat's terminals, it
publishes each row's level. Active-low wiring is folded in **here**, against
`F_INVERT`, exactly where `modbus/rtu/image.cpp` folds it in for a remote bit:
a row reads true = asserted, whatever the contact does.

## Fallbacks are a safety decision

A local row cannot time out, so its `fallback` is what it reads before the scan
has ever run - one beat at bring-up, and for ever in a unit test that does not
drive the terminal. Spell it as the SAFE side of what the row means.

`pwr_upsLowBat` is the worked case: it fails **asserted**, so a terminal nothing
has read holds every actuator. An unread terminal is not evidence of a healthy
supply, and the plant sitting still costs a morning where the alternative costs
the collection.

## What is not here

**Local outputs.** Every output gh_mlb41 has is on the bus, so there is no
`LocalOutputs` and no `HAL::DigitalOut`. The shape is settled if one is ever
needed: the mirror image, at `PHASE_OUTPUT`.

**Attaching the pulse counters.** `Site::pulse_inputs[]` is generated from the
`isr` column and `IrrigationController` samples the channel directly, but
nothing calls `HAL::Pulse::attach()` outside the tests - so on hardware the
counter never starts. It wants the site's table to reach the component, which
`SiteDescriptor` does not carry yet.
