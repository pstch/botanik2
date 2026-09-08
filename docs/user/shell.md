# SHELL: the command line {#user_shell}

The shell page is the second page in the console, it's used to monitor the state of the runtime and interact with variables, parameters, alarms, etc.

@verbinclude shell.txt

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
| `logs on|off`        | show logs in the shell                           |
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

### System commands

### Status commands

### Variable table commands
