# User manual {#user}

This is the manual written for operating a Botanik deployed unit and its human-machine interface.

Botanik provides an interactive serial console that multiplexes multiple distinct pages. All pages share a header bar (with the active page, a timestamp, and some status information) and an alarm bar (with the active alarms that have not been acknowledged).

@verbinclude home.txt

## Keyboard shortcuts

The console intercepts these itself, before the active page sees them. Every
other key reaches the page.

| key | what it does |
|---|---|
| `Alt+1` .. `Alt+9` | go to that page |
| `Alt+0` | go to the tenth page, where a site registers one |
| `Home` | go to the first page |
| `Tab`, `Ctrl+PageDown` | the next page |
| `Shift+Tab`, `Ctrl+PageUp` | the previous page |
| `Ctrl+L` | repaint everything: the two bars, then the page |
| `Ctrl+C` | cancel |

`Ctrl+C` is offered to the page first, so a page in the middle of an edit
cancels it. Where the page does not take it, the simulator quits and the
hardware does nothing.

## The pages {#user_pages}

- @subpage user_home
- @subpage user_shell
- @subpage user_logs
- @subpage user_alarms
- @subpage user_params
- @subpage user_monitor
- @subpage user_tasks
- @subpage user_system
- @subpage user_notes

## Control authority

Two things decide whether a control program drives anything, and the stricter
of them wins:

- the operator, in either of two ways. `programs disable <pat>` takes one
  program out of the scan, or a glob of them, or `all`; it holds that choice in
  memory, so a disabled program comes back on the next reboot. The
  `sys_control` setting is the same choice for every program at once, and being
  a parameter it can be saved to survive one: `params set sys_control 0`, then
  `params save sys_control`;
- the unit, through its run state: the laws run in RUN only, never while it is
  starting up, shutting down or halted. The header bar carries it, and the
  SYSTEM page spells it out.

`programs status` reports both at once, so it is what to read when nothing is
moving: it counts what is running, names what is disabled, and says whether
`sys_control` is on.

A program's own health is not a third gate. It is what the program says about
its last answer, shown per program on the TASKS page, and a FAULTED one keeps
running: what it writes when it cannot answer is its safe state.

A program that stops running does not leave what it was driving where it stood.
Its rows go to their declared safe state on the next beat, so a valve closes and
a fan stops rather than holding the last thing they were told.

The exception is a program that parks. Those keep being called on the way out,
so a roof finishes shutting before the unit halts instead of being dropped
half open.

