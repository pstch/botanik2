# SHELL: the command line {#user_shell}

@verbinclude shell.txt

The shell page is the second page in the console, it's used to monitor the state of the runtime and interact with variables, parameters, alarms, etc.


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
| `Home`, `Ctrl+A`         | the start of the line                             |
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

`help` opens on the main commands and the topics, `help <topic>` lists one
topic, and `<command> help` spells out a command's verbs.

Most commands are a noun and then a verb, the noun parsing its own verb, so
`params` is one subject with six verbs rather than six commands. A noun typed on
its own prints its verbs, which is the same thing `<noun> help` does.

Where an argument is written `<pat>` it is a glob over names: `*` matches any run
of characters, `?` matches one, and both ends are anchored. A pattern with no
wildcard in it is a plain prefix, so `cli_` is every climate row. Where it is
written `<name>`, only the whole name will do.

`all` is the word for the whole table, and only that word. A verb left bare never
means it: `params save` on its own prints its usage rather than committing
everything.

The four ways out of the unit take `confirm`, and nothing else after it. Typed
bare they say what they would do and stop.

### System

#### help [<topic>]

The main commands, then the topics. The topics are `system`, `status` and
`tables`, plus `site` where the installation registered commands of its own.

#### clear

Clear the transcript.

#### logs on|off|status

Whether log lines are interleaved into the transcript. This is a view setting on
this page: it changes nothing about what is logged, and the LOGS page, the
serial mirror and MQTT carry the same lines either way. Two things it does not
silence: entries at `CRITICAL`, which come through with the interleave off
because they are usually said once and never repeated, and entries that arrived
while another page was up, which are never shown here at all.

#### log <level> <text>

Write an entry of your own. The level is `debug`, `info`, `notice`, `warning`,
`error` or `critical`. It is filed under the `USER` topic and goes to every sink
the firmware's own lines go to, so what the shift did ends up in the same record
as what the unit did.

#### control status|start|stop|enable|disable

The operator's standing choice about whether the control programs drive at all.
`start` and `stop` apply to this run and a reboot forgets them; `enable` and
`disable` are the same edit written to storage, so the unit comes up the way it
was left. `status` prints the choice, the run state beside it, and how many
programs are running.

Stopping control takes every law out of the scan at once, and what they drove
goes to its safe state. It is one of two gates, so starting it on a unit that has
not reached RUN yet changes nothing until it does, and the command says so.

#### breakpoint

Break into an attached debugger. It refuses when there is none: a breakpoint
instruction with nothing attached faults the unit.

#### reboot confirm

Take the unit down and let the watchdog reset it. The request moves the
lifecycle rather than resetting anything: the next beat leaves RUN, which stops
the laws, `sys_shutdown_sec` runs, and only then is the watchdog kick withheld.
That window is what gets the retained rows and the parameters a program owes to
flash. The reply is a log entry rather than a printed line.

#### halt confirm

The same stop, and then the unit sits there with the cycle stopped and this
console with it. `reset` is the way back.

#### reset confirm

Reset now, mid-beat. It flushes the retained rows and the owed parameters and
clears the screen first, but it does not go through the lifecycle, so nothing is
parked and no program is given the chance to leave the scan tidily.

#### exit confirm

Exit the simulator. On hardware there is no exit, so this resets the unit, which
is why it is guarded like the other three.

#### ntpsync

Ask for an NTP resync now, rather than waiting for the next scheduled one.
Present only where NTP is built in.

### Status

#### version

The firmware version, the git hash it was built from, the site name and what it
is running on.

#### stats

The beat, in one screen: uptime with the tick and overrun counts, CPU load and
jitter (the exponential average, then the recent and peak loop times), stack
high-water, the watchdog, and the console serial's dropped bytes.

The watchdog row is the one to read when the beat is late. It gives the period,
how long ago the last kick went out, and whether kicks are being withheld, which
is otherwise invisible until the unit reboots and cannot say why.

#### net

The network stack: the link and the address, netmask and gateway, then the DNS
resolver, NTP and MQTT with their servers. Each of the three says whether it is
disabled, so an absent subsystem is not read as a broken one.

#### features

Which optional subsystems this image was built with, `on` then `off`. They are
compile-time flags, so there is nothing to set.

#### modbus stats|devices

`stats` is the field bus in summary: the poll plan (how many rows became how many
blocks on how many devices), the state of the line, the transaction counters, and
how many devices are not answering. A segment where nothing at all answers gets a
line pointing at the causes that take everything down together.

`devices` is the roll-call, one line per unit. `stats` deliberately gives counts
only, because twenty units would push the plan and the line off the top.

Where the SCADA side is built in, both forms also report the TCP slave: whether it
is listening, on which port, how many rows it serves and how many clients it has,
and the served, refused, written and rejected counts.

#### tasks stats|list [<pat>]|get <name>

`stats` is the beat as a whole: how many tasks are registered and how many slots
are left, the distinct periods in use, total activations, missed activations, and
the costliest task. Missed is the headline, because a dropped activation is never
caught up and this is the only record that the rate was not kept.

`list` is one line per task, name, phase, period, mean cost and missed count,
filtered by `<pat>`. `get` is one task in full: the phase and period it runs at,
its run and missed counts and when it last ran, its last, mean and peak cost, and
its duty against its own period rather than against the beat.

### Tables

#### params list|get|set|clear|reset|save

The editable settings.

| verb                    | what it does                                        |
|-------------------------|-----------------------------------------------------|
| `list [<pat>]`          | the parameters, and how many have unsaved edits     |
| `get <name>`            | the whole row: kind, group, band, what it reads now |
| `set <name> <value>`    | write it, live and unsaved                          |
| `save <name>` or `all`  | commit it to storage                                |
| `clear <name>` or `all` | forget an edit that was never saved                 |
| `reset <name>` or `all` | back to the factory default, live and in storage    |

`set` is live the moment it returns and is not durable: the reply says so, and
`save` is what makes it survive a reboot. `clear` is the undo for an uncommitted
edit and leaves the stored copy alone; `reset` is the destructive one, and takes
the stored copy with it so the next boot does not put the old value back.

`reset all` skips the retained parameters, which record what happened rather than
how the plant is tuned, and says how many it left alone. A single retained row
asked for by name is refused with the same explanation.

Naming a row that is not a parameter points at `overrides` instead of failing
flatly, since the next scan would put a value written here straight back.

#### overrides list|get|set|clear|save

Force any variable by hand, whatever its kind.

| verb                    | what it does                        |
|-------------------------|-------------------------------------|
| `list`                  | what is forced, and what is unsaved |
| `get <name>`            | the row, override and all           |
| `set <name> [<value>]`  | force it, or its quality            |
| `clear <name>` or `all` | release it                          |
| `save <name>` or `all`  | commit the force to storage         |

`set` with a value forces that value. With no value it freezes the row where it
stands. A trailing `valid` or `invalid` forces the row's `_qlt` companion, which
stands a variable up as a dead sensor without touching what it reports, and is
accepted either side of the value. `set <name> off` is another spelling of
`clear`.

An override is live at once and does not survive a reboot until saved. That is
deliberate: forcing a signal to chase a fault should not outlive the session,
while a failed sensor bypassed until the spare arrives is exactly what wants
committing.

#### programs status|list|start|stop|enable|disable

The control programs in the scan.

| verb                     | what it does                                        |
|--------------------------|-----------------------------------------------------|
| `status`                 | the counts, `sys_control`, and which cannot run     |
| `list [<pat>]`           | one line per program: name, health, whether it runs |
| `start <pat>` or `all`   | put it back in the scan                             |
| `stop <pat>` or `all`    | take it out, and drop what it drives to safe        |
| `enable <pat>` or `all`  | start, and save it for the next boot                |
| `disable <pat>` or `all` | stop, and save it for the next boot                 |

A stopped program's rows go to their declared safe state on the next beat, which
is the point of the verb: a coil left energised by a law nobody is running is
what it exists to prevent. Programs that park are the exception, and finish
travelling first.

`start` and `stop` are this run's; `enable` and `disable` are the same edit made
durable, exactly as under `control`. The gate a program is behind is a parameter
of its own, `prog_<name>_run`, so a unit that comes up with a law out of the
scan says which one on the PARAMS page. `stop` leaves that parameter edited and
unsaved, which the page marks with a trailing `*`, so `params save` commits it
and `params clear` puts it back.

The listing separates the two gates. A program reads `running`, `STOPPED` when
the operator took it out, or `held` when it is in the scan and something else is
stopping it, which is `sys_control` or a run state that is not RUN. Starting a
program on a unit whose control is off is answered with a line saying nothing
will move.

#### alarms list|get|ack|unack

The alarm table.

| verb                              | what it does                                |
|-----------------------------------|---------------------------------------------|
| `list [<pat>]`                    | the active alarms: code, variable, age, ack |
| `get <code>` or `<name>`          | one alarm, and the row behind it            |
| `ack <code>`, `<name>` or `all`   | acknowledge it                              |
| `unack <code>`, `<name>` or `all` | take the acknowledgement back               |

`list` filters on the CODE, which is what the alarm bar shows and how a plant's
alarms group (`TOO_*`, `WTR_*`). `get` takes either the code or the variable
name, and prints the dwell policy beside the state, since an alarm that should be
up and is not is usually inside its on-delay and one that should be down is
usually inside its hold. Where the table gives them, it also prints the
description and the remediation note.

Acknowledging clears an alarm from the alarm bar. It does not clear the
condition, and it applies to one episode: either edge drops it, so an alarm that
goes away and comes back is unacknowledged again. Only an active alarm can be
acknowledged; anything else is answered with "no active alarm".

`unack` is the same gesture the other way, for an ack put on the wrong row. It
is the ALARMS page's `Enter` toggle spelled out, and `unack all` undoes an
`ack all` that was too broad.

#### vars list|get|info

Read any variable, whatever its scope.

| verb           | what it does                               |
|----------------|--------------------------------------------|
| `list [<pat>]` | name, value, unit, kind, and the two marks |
| `get <name>`   | the value, and nothing else                |
| `info <name>`  | the whole row                              |

`get` is one line so it can be read at a glance or scraped by whatever wants the
first field. The only thing that rides with the value is its quality, because a
number a dead sensor is not producing is not a reading and a bare one would be
taken for one. `info` is where everything else about the row went.

`list` flags three things a bare number hides: `OVERRIDDEN`, `UNSAVED`, and a row
that is reading its declared default because nothing has written it yet.
