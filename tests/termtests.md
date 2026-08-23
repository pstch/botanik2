# Terminal emulation tests {#termtests}

@brief Screen-level regression tests that drive the real `botanik_sim` over a
       PTY and assert its reconstructed terminal against golden fixtures.

The unit suite (@ref cpptests) checks the control laws; this suite checks what
the operator sees, and reaches more of the firmware than the unit suite does. It replays the sim's ANSI output through
[pyte](https://github.com/selectel/pyte) to reconstruct the 80x24 screen and
asserts it against committed goldens, catching rendering regressions - cursor
moves, insert/delete, IL/DL, editor reconciliation - that a source diff cannot.

Two design choices make it deterministic: a PTY (not a pipe) so the baud throttle
and TX back-pressure path are real, and a harness-driven clock so
a run is fast while every byte/event ratio matches a real 4800-baud session. The
suite is opt-in: CMake skips it unless pyte/pytest are importable.

- @subpage md_tests_2pyte_2README - the full how-and-why: dependencies, the PTY
  and the beat-bounded waits, the golden-fixture workflow, and how screens are
  normalised before comparison.
