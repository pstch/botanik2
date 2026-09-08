# User manual {#user}

This is the manual written for operating a Botanik deployed unit and its human-machine interface.

Botanik provides an interactive serial console that multiplexes multiple distinct pages. All pages share a header bar (with the active page, a timestamp, and some status information) and an alarm bar (with the active alarms that have not been acknowledged).

@verbinclude home.txt

## Keyboard shortcuts

The console intercepts these itself, before the active page sees them. Every
other key reaches the page directly.

TODO: visually align this table so that it's easier to edit

| key | what it does |
|---|---|
| `Alt+1` .. `Alt+9` | go to that page |
| `Alt+0` | go to the tenth page, where a site registers one |
| `Home` | go to the first page |
| `Tab`, `Ctrl+PageDown` | the next page |
| `Shift+Tab`, `Ctrl+PageUp` | the previous page |
| `Ctrl+L` | repaint everything: the two bars, then the page |
| `Ctrl+C` | if not handled by the page : exit the simulator|

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

- the operator, through `control` for every program at once or `programs` for
  one of them, and both spell the difference the same way. `start` and `stop`
  last until the next reboot; `enable` and `disable` save the choice, so the
  unit comes up that way. `programs` has no saved form: the roster decides
  which programs exist, and a unit coming up with one silently out of the scan
  is a plant nobody can explain;
- the unit, through its run state: the laws run in RUN only, never while it is
  starting up, shutting down or halted. The header bar carries it, and the
  SYSTEM page spells it out.

`control status` is what to read when nothing is moving. It reports both gates
at once, counts how many programs that leaves running, and says so when control
is on but the unit has not reached RUN. `programs status` names the ones that
are stopped.

A program's own health is not a third gate. It is what the program says about
its last answer, shown per program on the TASKS page, and a FAULTED one keeps
running: what it writes when it cannot answer is its safe state.

A program that stops running does not leave what it was driving where it stood.
Its rows go to their declared safe state on the next beat, so a valve closes and
a fan stops rather than holding the last thing they were told.

The exception is a program that parks. Those keep being called on the way out,
so a roof finishes shutting before the unit halts instead of being dropped
half open.

