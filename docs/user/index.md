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
