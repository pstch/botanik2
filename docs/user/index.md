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

Three things decide whether a control program drives anything, and the strictest
of them wins:

- the operator, through `sys_control` for every program at once, or
  `programs disable <pat>` for one of them, or a glob of them;
- the unit, through its run state: the laws run in RUN only, never while it is
  starting up, shutting down or halted. The header bar carries it, and the
  SYSTEM page spells it out;
- the program itself, through the health it reports. The TASKS page shows that
  per program.

A program that stops running does not leave what it was driving where it stood.
Its rows go to their declared safe state on the next beat, so a valve closes and
a fan stops rather than holding the last thing they were told.

The exception is a program that parks. Those keep being called on the way out,
so a roof finishes shutting before the unit halts instead of being dropped
half open.

