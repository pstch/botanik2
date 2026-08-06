# Design notes

Working notes: budgets, and designs for things that are not built yet.

**Not part of the manual.** The Doxyfile excludes this directory, so nothing here
is rendered. That is the point of the split: `docs/dev/` documents mechanisms
that exist and can be read in the code, and a plan sitting next to them reads as
one - which is how a 196-line budget for an unwritten driver ended up filed as
developer reference.

Anything here is provisional. When the thing it describes gets built, fold what
turned out to be true into the manual page for it and delete the rest.

| Note | About | Status |
|---|---|---|
| `modbus-budget.md` | What a Modbus RTU transaction costs, what the plant can afford, and the four rules the client should follow | no client yet |
| `beat-cost.md` | Static per-task cycle estimates from M7 disassembly, and where the spikes are | estimates, not measurements; some already stale |
