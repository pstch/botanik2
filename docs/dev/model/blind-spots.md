# Blind spots {#dev_blind_spots}

<!-- source of truth: the list of things we chose not to check -->

## What this list is for

Some things the plant cannot see are DECISIONS, not gaps. An actuator reports no
fault because no fault contact is wired. `cli_phase_qlt` says where a phase came
from rather than whether it is legal. Neither is an oversight, and both look like
one to a reader who finds no check.

So this page is the answer to "why is there no check for X" when the answer is
"we chose not to". A missing entry here is the thing to be suspicious of: an
absent check with no absent-check note is more likely forgotten than decided.

Add to this list when a review asks the question and the answer is a decision.

## The plant

- **An actuator's position is commanded, not measured**, unless the site wires a
  feedback row and a law that reads it. A roof that reports "open" reports what
  it was told, and the only contradiction the unit can raise is a travel timeout.
- **No fault contact is assumed.** Where a device offers one, the site declares a
  row for it; where it does not, the absence of a fault is not evidence of
  health. That is why a program's own `_health` row exists: a law that cannot see
  its plant says so rather than leaving a reader to infer it.

## The console

- **No per-point access control.** Anyone who reaches the console can force any
  row, a relay included. The act is logged before the write, so the displaced
  value is recorded, and the override is visible on the PARAMS page and over
  Modbus - but nothing gates it by operator, and there are no roles.

  The security boundary is the console port and the plant network, not the row.
  A unit whose console is reachable by somebody who should not drive the plant is
  already mis-sited. Stated because "it is logged" is easy to mistake for "it is
  controlled". See @ref vars_blind_spots.
- **The shell does not confirm a force.** `overrides set` takes effect on the
  next beat. The destructive commands that DO confirm (`reboot`, `halt`, `reset`,
  `exit`) confirm because they end the cycle, not because they change a value.

## The field bus

- **The RS-485 line format is compile-time.** Baud, parity, data bits and stop
  bits come from the build, not from a parameter row, while timeout, retries,
  backoff and the write cadence are all live. Commissioning a segment is exactly
  when the line format is least settled, so this is the gap most likely to be
  felt. It is not free to close: the planner's cost model is derived from the
  baud, so changing it has to re-plan the sweep.
- **FC15 (write multiple coils) on the TCP server answers
  `EX_ILLEGAL_FUNCTION`.** No site declares a run of boolean parameters for a
  master to write as a block, so the function has no rows to serve. It is a scope
  decision, not an omission.
- **No peer allowlist on the TCP server.** Any host that can open the port and
  speak Modbus can read the map, and write it where `writable` says so. The
  connection ceiling is a small fixed number and a refused connection is counted,
  so the failure is visible - but the plant network is the boundary, as above.
- **A quiet device is not distinguished from a quiet segment** except by count: a
  single unit that stops answering is a device fault, and every unit going quiet
  at once is reported as a trunk, termination or transceiver problem. There is no
  separate check that says which, because the roll-call already shows it.
