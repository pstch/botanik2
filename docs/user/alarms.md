# Alarms {#user_alarms}

An alarm **is a variable**. It is a boolean row in the same table as everything else
(@ref user_variables), declared with the alarm scope, and what makes it an alarm is
the dwell policy it carries alongside the boolean. There is no separate alarm list to
keep in step.

Two consequences worth having:

- an alarm is readable and forcible by name like any other variable - `get
  alarm_dc_ups`, or `override alarm_dc_ups 1` to exercise whatever watches it;
- the ALARMS page shows that variable name beside the code, so what you see on screen
  is what you type at the prompt (the `alarm_` prefix is stripped on screen).

Each alarm has a short **code** for the eye, a description, a **remediation note**,
and the dwell policy that decides how long the condition must hold before it counts.

Where they surface:

- the **alarm bar** on the bottom row - active and unacknowledged only;
- the **ALARMS page** - the full table, with history and remediation;
- the **log** - one line on the `ALARM_BEG` / `ALARM_END` topic per transition;
- **MQTT**, where telemetry is enabled.

## Lifecycle

An alarm has two independent states: whether it is **active**, and whether it has
been **acknowledged**.

Active is driven by the plant, through the dwell policy: a condition must hold for
its ON dwell before the alarm is announced, and clear for its OFF dwell before it is
announced as gone. Some conditions also hold on for a minimum time from onset, or
for a delay after the raw signal clears, so a flapping input does not produce a
flapping alarm. A condition that toggles too fast to satisfy its dwell is logged
once as `ALARM_FLAP`.

Acknowledged is driven by you.

## Acknowledging

On the ALARMS page, select the row and press `a`. From the shell,
`alarms ack <code>` or `alarms ack all`. Either spelling of the condition works -
the short code the alarm bar shows (`SYS_RST`) or the variable name `alarms list`
shows (`alarm_system_reset`).

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

## Platform alarms

Every unit has these regardless of site, raised from the firmware's own health:

| Code | Variable | Condition | Notes |
|---|---|---|---|
| `SYS_RST` | `alarm_system_reset` | the unit restarted in the last five minutes | expected after a deliberate reboot |
| `CPU_LOAD` | `alarm_cpu_load` | loop CPU load over its threshold | check what is loading the loop - the TASKS page attributes it per task |
| `CPU_JITT` | `alarm_cpu_jitter` | loop timing jitter over its threshold | |
| `CPU_XRUN` | `alarm_cpu_xrun` | the scheduler skipped a beat | a beat's work ran past its deadline |
| `STACK` | `alarm_stack` | stack usage over its threshold | |
| `FLASH_FLT` | `alarm_flash_error` | a write to persistent storage failed | **sticky until reboot** |

`FLASH_FLT` is the one to act on: parameters will not survive a reboot until it is
resolved. It is the same fault behind `set`'s `(NOT SAVED: storage refused it)` and
the PARAMS page's `NOT SAVED` warning - one problem seen from three places.

The `cpu:` and `stk:` figures in the status bar are the same measurements the
`CPU_LOAD` and `STACK` alarms watch, and `stats` shows their recent and peak values.

## Remediation

Each condition may carry a note saying what to actually do about it, shown in the
`Remediation` line of the ALARMS detail panel. It comes from the site, so it is
written in terms of the real installation - "check mains breaker / utility feed"
rather than a generic message. Read it before acknowledging.
