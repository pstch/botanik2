# Alarms {#user_alarms}

An alarm is a PLC variable, which means it's registered as a boolean in the variables 
table with the ALARM scope, represented as boolean. An alarm is registered with :
 - a **short code** for the alarm bar (also used as MQTT sub-topic)
 - a **description** along with a **remediation note**
 - a **register index** for the built-in ModBus TCP slave
 - a duration (`onDwell`) the bit must hold before the alarm's START event is announced/published
   (`<0`: never announced) 
 - a duration (`offDwell`) the bit must stay clear before the alarm's END event is announced/published
   (`<0`: never announced) 
 - a duration (`recent`) the alarm stays in the "recent" view of the alarms table
   (`<0`: never expires)
 - a duration (`hold`) the alarm stays ACTIVE past the last true reading
   (`0`: follow the bit exactly)
 
Alarms will surface in :

- the **alarm bar** on the bottom row - active and unacknowledged only;
- the **ALARMS page** - the full table, with history and remediation;
- the **log** - one line on the `ALARM_BEG` / `ALARM_END` topic per transition;
- **MQTT**, where telemetry is enabled.


(not implemented yet) Some critical alarms may also trigger the alarm beeper and the warning light.

## Alarm state

An alarm has two independent states: whether it is **active**, and whether it has
been **acknowledged**. Active is driven by the plant, acknowledged is driven by you.

Two of the durations decide what is *announced*, and one decides what is *active*:

- `onDwell` and `offDwell` gate the log line and the published event, nothing else.
  A condition that comes and goes faster than its `onDwell` never announces a START,
  and its END is logged once as `ALARM_FLAP` instead of a START/END pair.
- `hold` gates the alarm itself. It keeps the condition active that long past the
  last true reading, re-armed for as long as the bit stays up. This is what stops a
  chattering input producing a chattering alarm, and what keeps a signal too brief
  to notice on screen up long enough to be seen and acknowledged. While it holds,
  the episode does not end, so the alarm keeps one acknowledgement rather than
  demanding a fresh one each time the bit twitches.

`recent` is neither: it is how long a *cleared* alarm keeps showing in the ALARMS
page's `recent` view.

## Overriding

There is no separate way to force an alarm. A condition reads its variable, so
overriding that variable is what forcing it means - `overrides set alarm_too_hot 1`
from the shell, or the MONITOR page. The override reaches the alarm model by the
same route it reaches control logic and telemetry, so a forced alarm logs,
publishes and lands on the alarm bar exactly like a real one. The ALARMS page shows
the row as forced `ON` or `OFF`, because a held alarm and a real one are otherwise
indistinguishable.

A forced condition follows the force immediately: `hold` does not apply to it,
since the hold exists to smooth a signal that flaps and a hand-held row does not.
Releasing the override drops the alarm on the next beat.

## Acknowledging

On the ALARMS page, select the row and press `a`. From the shell, `alarms ack <code>`
or `alarms ack all`. Either spelling of the condition works - the short code the
alarm bar shows (`SYS_RST`) or the variable name `alarms list` shows
(`alarm_system_reset`) - and `alarms get <code|name>` answers with the one condition
in detail: whether it is active, for how long, its policy and its remediation note.

Acknowledging means "seen", not "fixed". It:

- removes the code from the alarm bar;
- stops the `ON` badge being highlighted on the ALARMS page;
- demotes the row below still-unacknowledged alarms, without removing it;
- records the time, shown in the detail panel's `Ack'd` line.

It does **not** clear the alarm. The condition is still active, still logged, still
published. And an acknowledgement covers only the current episode: if the condition
clears and fires again, the new episode is unacknowledged and the alarm comes back
onto the bar.

The `unmasked` filter shows exactly what the alarm bar shows - active and not
acknowledged.

## Library alarms

Every deployed unit has these regardless of site, raised from the firmware's own health monitor :

| Code                 | Variable                | Condition                                   | Remediation                                           |
|----------------------|-------------------------|---------------------------------------------|-------------------------------------------------------|
| `SYS_RST`            | `alarm_system_reset`    | the unit restarted in the last five minutes | (maintainer) troubleshoot intermittent reboots        |
| `CPU_LOAD`           | `alarm_cpu_load`        | loop CPU load over its threshold            | (maintainer) troubleshoot task scheduling             |
| `CPU_JITT`           | `alarm_cpu_jitter`      | loop timing jitter over its threshold       | (maintainer) checks TASKS to see is loading the loop  |
| `CPU_XRUN`           | `alarm_cpu_xrun`        | the scheduler skipped a beat                | (maintainer) checks TASKS fo see is loading the loop  |
| `STACK`              | `alarm_stack`           | stack usage over its threshold              | (maintainer) contact platform maintainer              |
| `FLASH_FLT`          | `alarm_flash_error`     | a write to persistent storage failed        | (maintainer) replace faulty hardware                  |

The platform defined alarms should usually not trigger in a deployed unit, if they happen persistently, 
the user shall reach out to the deployment's maintainer.

How long each keeps showing in the `recent` view is set per alarm, by how long the
event stays worth knowing about. `SYS_RST`, `STACK` and `FLASH_FLT` keep for 30 days:
they are evidence about the unit itself, and are usually asked about well after the
fact. `CPU_LOAD`, `CPU_JITT` and `CPU_XRUN` keep for a day, because they are frequent
enough that a week of them would bury everything else in the view. The field bus and
scan alarms take the 7-day default, which is the window an intermittent fault is
read as a pattern over.