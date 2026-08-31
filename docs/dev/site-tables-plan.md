# The site's tables stay hand-written C++

Decided 2026-08-31, having been reversed twice in one day. The reasoning is
recorded because the CSV idea is an easy one to have again.

## The decision

`vars.cpp` stays as it is: 455 rows across ten factories, `constexpr`, edited
by hand. A new `programs.cpp` carries the program roster in the same shape.
**matiec is the only generator in the tree.**

## Why not CSV, twice

The first retirement (2026-08-24) removed `scripts/` and every CSV and TOML
because the generator ran at BUILD time, which is a dependency every
contributor pays for. The checks it did became `tests/test_site.cpp`, which is
a better home for them.

The second version would have run OFFLINE with its output committed, the way
`iec2c` already works, so that objection was answered. It failed on a different
one: **the only thing it bought was a program table, and a program table is
twenty lines of C++.** Ten CSVs, a TOML root, a path resolver and a round-trip
diff, to avoid writing a `constexpr` array that looks exactly like the one
beside it.

What survives from that plan is the OBSERVATION that forced it: the program
roster has two consumers, the C++ dispatch table and `plant.st`'s
`CONFIGURATION`. They can still drift. The answer is the `.st` scanner
checking one against the other, not a generator producing both.

## What that leaves

| | |
|---|---|
| `vars.cpp` | the rows, hand-written |
| `programs.cpp` | the program roster, hand-written, same shape |
| `plant.st` | the same programs, declared to matiec; checked against the roster |
| `programs/<stage>/<name>/<name>.st` | the laws |
| `generated/` | everything generated - the tables' C++, plant.st, the roster and matiec's output - committed |

The three hand-kept counts (`LIBRARY_ROWS`, `PARAMS_COUNT`, `ROWS_COUNT`) stay
hand-kept, and stay the sharpest edge in the site: each of them silently
truncated the table once this month. They are cheap to check and expensive to
get wrong, so that is a test to strengthen rather than a reason to generate.
