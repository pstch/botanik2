# Developer manual {#development}

@brief Working on @c libbotanik itself - the mechanisms, and why they are the way
       they are.

This part is for changing the library, not using it. Each page takes one mechanism
and explains how it actually works.

The order below is the dependency order: every page depends only on the ones above
it, which is the same layering @ref dev_metrics_coupling states. Read
**Orientation** once before your first change, then the section you are touching.

## Orientation {#dev_orientation}

- @subpage dev_architecture - the layers, the subsystem map, and the per-tick loop
  order. Start here.
- @subpage dev_concurrency - one cooperative loop, the async boundaries, and why
  there is no `std::atomic`.
- @subpage dev_build - the two build targets, the build options, and the gate.
- @subpage dev_footprint - what the OPTA image is made of, and the two levers
  that move it.

## The beat {#dev_beat}

- @subpage dev_scheduler - the fixed-period driver, overrun handling, and the
  self-instrumented metrics: when the beat happens.
- @subpage dev_tasks - the task table: rate groups, phase offsets, and the
  due/late/dropped rule. What runs in the beat.
- @subpage dev_scan - the control programs: what one is guaranteed to see, and
  the control-authority gate.

## The model and the field {#dev_data}

- @subpage dev_vars - the variable model: descriptors, value slots, the sink, and
  where overrides are applied. Everything else reads and writes through this.
- @subpage dev_modbus - the field bus and the plant network: what the bus costs,
  planning, framing, and the register map.
- @subpage dev_io - the controller's own terminals, and why a program cannot
  tell them from a remote register.
- @subpage dev_network - the optional config-driven network stack.

## The console {#dev_console}

The console has the most non-obvious machinery in the tree, because it drives a
4800-baud link and every byte is budgeted.

- @subpage dev_input - raw bytes to input events to the active page.
- @subpage dev_pages - the page interface, the render contract, and `Scrollback`.
- @subpage dev_screen - **the TX path.** The screen back-buffer: how a whole
  screen's worth of intent becomes the minimal paced ANSI on a slow link.
- @subpage dev_editor - **editor reconciliation.** The one renderer that still
  reasons about the screen itself, and why it earns the exception.

## Supervision and platform {#dev_supervision}

- @subpage dev_logging - a log entry from call site through rate limiting to every
  sink.
- @subpage dev_health - the power-on self-test, the platform alarms, and the
  watchdog verdict.
- @subpage dev_porting - what a third target needs: one new HAL implementation.
- @subpage testing - the test suites in detail.

## Which way the dependencies point {#dev_metrics_coupling}

Every cross-module `#include` points DOWN the stack, and the module graph is
acyclic. Only header (`.h` -> `.h`) edges count, because only those are
contagious: an include in a `.cpp` is private to one translation unit, while one
in a header lands in every file that transitively includes it. The config
headers are held out - every module includes them.

**There is not one upward edge.** The baseline is zero
so the first one to appear stands out; a baseline of "four known-good exceptions"
does not work, because the fifth blends in.

Fixing an upward edge means putting the declaration where it belongs, not adding an
indirection: `Console` forward-declares `Pages::Page` rather than including all of
`pages/`, and `library_vars.h` sits beside the umbrella because it is macro-only and
site-facing. An interface or a pimpl bought only to green a cell costs a vtable or
an allocation on a path that does not want one.

A NEW upward edge is a layering regression, and wants either a fix or a line
here saying why it earns its place.
