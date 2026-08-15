# The serial monitor {#user_monitor}

A second serial port that only ever prints log lines. No screen, no pages, no
prompt: whatever the unit logs, it writes here, one line per entry, for as long as
something is listening.

| | |
|---|---|
| **Port** | monitor UART (OPTA: **USB CDC**) |
| **Baudrate** | the `monitor_baud` parameter, `115200` by default |
| **Framing** | `8N1` |
| **Content** | plain formatted log lines, one per entry |
| **Input** | discarded |

It exists because the console cannot do this job. The console is a full-screen
terminal on a slow link: it repaints, it pages, it drops the least important rows
when the byte budget runs out, and its log tail is seven rows deep. None of that is
what you want when you are recording what a unit did overnight, or watching a boot
that ends before a screen can be drawn.

So the two ports divide the work: the console is for **operating** the unit, the
monitor is for **watching** it.

## Connecting

Any terminal program will do, since nothing here is interactive:

```sh
picocom -b 115200 /dev/ttyACM0
screen /dev/ttyACM0 115200
cat /dev/ttyACM0 | tee botanik-$(date +%F).log     # keep the transcript
```

On the OPTA the monitor is the USB CDC port, so plugging the unit into a laptop is
all the wiring it needs. Nothing is sent back: input is read and discarded, so a
stray keypress in the terminal cannot reach the firmware.

@note On the simulator the stream goes to **stderr**, and suppresses itself when
that is a terminal so it cannot garble the UI on the same screen. Redirect it to
see it: `./botanik_sim 2> sim.log`.

## What it prints

The same entries as the LOGS page, in the same format (@ref user_logging), with two
differences that follow from the port having no screen to protect:

- **Nothing is dropped for want of bandwidth.** At 115200 baud the link is 24 times
  the console's, so the sink keeps up with the logger rather than the other way
  round.
- **It starts at the first line of the boot.** The console spends its first beats
  drawing a screen; the monitor is writing while that happens, which is what makes
  it the place to read a failing start-up.

The rate limiter still applies - it is the logger's, upstream of every sink - so a
topic that floods is still suppressed here, and the suppression is still reported.

## When to reach for it

- **A unit that will not come up.** The console needs the firmware healthy enough to
  paint; the monitor needs only the logger.
- **Recording a run.** Pipe it to a file and leave it; the transcript outlives the
  seven-row tail on HOME and the LOGS page's scrollback alike.
- **Watching a change take effect.** Edit a parameter on the console, watch the
  subsystem restart on the monitor, without the two competing for one screen.
- **A second pair of eyes.** Someone can watch the monitor while someone else drives
  the console - they are independent ports.

@note The monitor sink is a build-time feature (`LOGS_MONITOR`). `features` says
whether this binary has it, and `stats` reports the port's configured rate. The rate
itself is the `monitor_baud` parameter: changing it retunes the port, so
reconnect at the new speed.
