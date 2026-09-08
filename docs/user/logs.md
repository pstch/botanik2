# LOGS: the log tail {#user_logs}

The logs page is the third page and it shows log entries published by the firmare, in a scrollable view. Log entries have :
 - a timestamp, a log topic and a log level
 - an associated text message

The view supports scrolling back to see older messages, and SPACE can be used to write a log mark.

@verbinclude logs.txt

## Keyboard shortcuts

| key                            | what it does                         |
|--------------------------------|--------------------------------------|
| `Space`                        | write a MARK line into the stream    |
| `Enter`                        | clear, and return to the live bottom |
| `Up`, `Shift+Up`, `Ctrl+P`     | back one line                        |
| `Down`, `Shift+Down`, `Ctrl+N` | forward one line                     |
| `PageUp`, `PageDown`           | a screenful at a time                |

The live tail freezes while you are scrolled back, so a busy log cannot carry
the view away from what you are reading. `Enter` returns to the bottom.

@note The shifted arrows are bound too, because a bare arrow is command history
wherever an editor shares the screen. The shifted one is the scrollback key on
every page that has one.

## Log levels

Six, and they are bit values rather than a run, so a sink can carry any set of
them.

| level      | what it is for                                        |
|------------|-------------------------------------------------------|
| `debug`    | tracing, and the two high-volume observability topics |
| `info`     | the ordinary running commentary                       |
| `notice`   | something worth seeing without being wrong            |
| `warning`  | something wrong that the unit handled                 |
| `error`    | something wrong that it did not                       |
| `critical` | undroppable: the rate limiter never sheds one         |

## Log topics

A topic says which part of the firmware spoke. It is also what the per-topic
rate limiter buckets on.

| topic        | what it covers                                                                           |
|--------------|------------------------------------------------------------------------------------------|
| `MODBUS`     | the field bus                                                                            |
| `BOOT`       | bring-up, and the ending                                                                 |
| `NETWORK`    | the link, DNS and MQTT                                                                   |
| `TIME`       | NTP and the wall clock                                                                   |
| `STATUS`     | periodic state reporting                                                                 |
| `ASSERT`     | firmware assertions                                                                      |
| `ALARM_BEG`  | an alarm went active                                                                     |
| `ALARM_END`  | an alarm cleared                                                                         |
| `ALARM_FLAP` | one cleared without ever dwelling long enough to announce                                |
| `HEALTH`     | the health monitor: the POST verdict, the watchdog                                       |
| `STORAGE`    | persistence, which is its own topic and not a boot problem                               |
| `USER`       | an operator's own doing: the `log` command, and the audit line behind an edit or a force |
| `PROGRAM`    | a control program changed state                                                          |
| `QUALITY`    | a variable's validity changed                                                            |

## Rate-limiting

Two token buckets stand in front of the writers: one global, one per topic.
An entry spends a token from each, and an entry that cannot pay is dropped.
Both refill once per beat.

That is what stops one chattering subsystem from crowding out everything else:
its own topic bucket empties while the global one still has room for the rest.
`critical` is exempt and is never shed.

Dropping is reported rather than silent. The first loss is said at once, and
later ones only after a holdoff, so the report cannot itself become the flood.
`alarm_log_flood` raises while messages are being shed.

Past the limiter, each writer has a queue of its own. `Logger` hands the entry
over and returns; it never waits on a wire. A writer's queue is drained from the
main loop, and one that fills drops new entries rather than blocking the beat.

So there are two places an entry can be lost, and they mean different things: at
the limiter, the unit is talking faster than the policy allows, and at a queue,
a sink is slower than the unit is talking - a 4800-baud console being the usual
one. Both are counted, and the counts are what the loss reports carry.
