# SHELL: the command line {#user_shell}

The shell page is the second page in the console, it's used to monitor the state of the runtime and interact with variables, parameters, alarms, etc.

@verbinclude shell.txt

## Keyboard shortcuts

| key                      | what it does                                      |
|--------------------------|---------------------------------------------------|
| `Enter`                  | run the line                                      |
| `Shift+Enter`            | a new line, to carry one command over several     |
| `Up`, `Ctrl+P`           | the previous command in the history               |
| `Down`, `Ctrl+N`         | the next one                                      |
| `Shift+Up`, `Shift+Down` | scroll the transcript a line                      |
| `PageUp`, `PageDown`     | scroll it a screenful, keeping one row of overlap |
| `Left`, `Ctrl+B`         | back one character                                |
| `Right`, `Ctrl+F`        | forward one character                             |
| `Alt+B`, `Alt+F`         | back or forward one word                          |
| `Ctrl+A`                 | the start of the line                             |
| `End`, `Ctrl+E`          | the end of the line                               |
| `Backspace`              | delete backwards                                  |
| `Delete`, `Ctrl+D`       | delete forwards                                   |
| `Ctrl+U`                 | clear the line                                    |
| `Ctrl+K`                 | kill to the end of the line                       |
| `Ctrl+Y`                 | yank the last kill back                           |
| `Ctrl+Space`             | set the mark                                      |
| `Ctrl+G`                 | clear the mark                                    |
| `Ctrl+W`                 | kill the region, or the word behind the cursor    |
| `Alt+W`                  | copy the region                                   |

The plain arrows are the command history here, which is why scrolling the
transcript is on the shifted ones - the same key it is on every page that has a
scrollback. Any key other than a scroll returns the view to the live bottom
first.

The kill ring is the console's, so a line cut here yanks on the NOTES page and
the other way about.

@note There is no completion. `Tab` never reaches this page: the console takes
it for the next page, before the page is offered anything.

## Commands

`help` lists these, `help <topic>` one group, and `<command> help` spells out a
command's verbs.

### System

| command              | what it does                                     |
|----------------------|--------------------------------------------------|
| `help [<topic>]`     | this help page, or a topic's help page           |
| `breakpoint`         | break into an attached debugger                  |
| `reboot confirm`     | soft reboot, letting the watchdog reset the unit |
| `halt confirm`       | park the plant, then stop the cycle              |
| `reset confirm`      | immediate reset                                  |
| `clear`              | clear the screen                                 |
| `exit confirm`       | exit the simulator; a reboot on hardware         |
| `logs on\|off`       | show logs in the shell                           |
| `log <level> <text>` | write an entry of your own to the log            |
| `ntpsync`            | force an NTP resync now                          |
| `control ...`        | whether the control programs drive at all        |

### Status

| command      | what it does                           |
|--------------|----------------------------------------|
| `version`    | show the code version                  |
| `stats`      | scheduler, stack and watchdog metrics  |
| `net`        | network, NTP and MQTT status           |
| `features`   | which subsystems are built in          |
| `modbus ...` | the field bus: plan, state and devices |
| `tasks ...`  | what the beat spends its time on       |

### Tables

| command         | what it does                          |
|-----------------|---------------------------------------|
| `params ...`    | the editable settings                 |
| `overrides ...` | force any variable by hand            |
| `programs ...`  | the control programs in the scan      |
| `alarms ...`    | the alarm table, and acknowledging    |
| `vars ...`      | read any variable, whatever its scope |

A command taking `...` is a noun that parses its own verb, so `params` alone
prints its verbs and `params set <name> <value>` does the work.
