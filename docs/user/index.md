# User manual {#user}

This is the manual written for operating a Botanik deployed unit and its human-machine interface.

Botanik provides an interactive serial console that multiplexes multiple distinct pages. All pages share :
 - a header bar (with the active page, a timestamp, and some status information)
  - an alarm bar (with the active alarms that have not been acknowledged).

@verbinclude home.txt

## Keyboard shortcuts

The console intercepts these itself, before the active page sees them. Every
other key reaches the page directly.

| key                        | what it does                                     |
|----------------------------|--------------------------------------------------|
| `Alt+1` .. `Alt+9`         | go to that page                                  |
| `Alt+0`                    | go to the tenth page, where a site registers one |
| `Tab`, `Ctrl+PageDown`     | the next page                                    |
| `Shift+Tab`, `Ctrl+PageUp` | the previous page                                |
| `Ctrl+L`                   | repaint everything: the two bars, then the page  |
| `Ctrl+C`                   | if the page does not take it: exit the simulator |

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

The control programs can be disabled :
 - globally
   + using `control start|stop` for a volatile change
   + using `control enable|disable` for a persistent change
 - per-program
   + using `programs start|stop name` for a volatile change
   + using `programs enable|disable name` for a persistent change

Also, the programs only run when the unit is in the RUN state, they are not ran while the unit 
is starting up or shutting down : during these phases their outputs are kept at their safe state.

A program that stops running does not leave what it was driving where it stood.
By default, its rows go to their declared safe state on the next beat, so a valve closes and
a fan stops rather than holding the last thing they were told. However some programs
also have a "parking phase" : they keep being called on thw way out so that we can finish
moving actuators to their target position before we drop to the safe output state.half open.
