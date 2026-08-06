"""Shell built-ins: `get`, `set` and `override`.

`get` / `set` replaced three commands (params / status / vars) that each walked
the same variable table through a different lens. `override` is the other half:
`set` edits a parameter, `override` forces any variable for maintenance. This
drives all three through the real console, since nothing else covers shell
command dispatch.
"""
import keys
from helpers import shell


def test_get_and_set(spawn):
    term = shell(spawn)
    term.send(b"help" + keys.ENTER)
    assert term.wait_for("read a variable"), "get missing from help"
    assert term.wait_for("list variables"), "list missing from help"
    assert term.wait_for("edit a parameter"), "set missing from help"

    term.send(b"get ntp_srv1" + keys.ENTER)
    assert term.wait_for("ntp_srv1"), "get <name> found nothing"

    term.send(b"set ntp_poll_sec 900" + keys.ENTER)
    assert term.wait_for("ntp_poll_sec = 900"), "set did not apply"

    term.send(b"get ntp_poll_sec" + keys.ENTER)
    assert term.wait_for("900"), "set did not round-trip through get"

    term.send(b"set nosuchthing 1" + keys.ENTER)
    assert term.wait_for("unknown variable"), "unknown name not reported"


def test_override(spawn):
    term = shell(spawn)

    # Nothing forced on a fresh boot.
    term.send(b"override list" + keys.ENTER)
    assert term.wait_for("none active"), "fresh boot reported an override"

    # Force a live sensor reading and read it back through `get`.
    term.send(b"override grid_L1_V 123.5" + keys.ENTER)
    assert term.wait_for("forced to"), "override did not take"
    term.send(b"get grid_L1_V" + keys.ENTER)
    # Both halves of it: what it is held at, and what it reads underneath.
    # "Has the sensor come back?" is the question someone deciding whether to
    # release an override actually has.
    assert term.wait_for("forced by hand"), "get did not flag the override"
    assert term.wait_for("123.5"), "override value did not reach the reading"
    assert term.wait_for("live "), "get did not show the value under the override"

    # A forgotten override must be one command away, and the listing has to say
    # that it outlives the session AND the reboot.
    term.send(b"override list" + keys.ENTER)
    assert term.wait_for("SURVIVE a reboot"), "override list missing"

    # Forcing a variable to read as a dead sensor is a separate job from forcing
    # its value, and both have to be sayable.
    term.send(b"override grid_L2_V 99 invalid" + keys.ENTER)
    assert term.wait_for("reading as invalid"), "validity was not honoured"

    # Release one by name; the live value takes over again.
    term.send(b"override grid_L1_V off" + keys.ENTER)
    assert term.wait_for("override off"), "override was not released"

    # ...and `override clear` releases the rest without walking the list, which
    # is how a maintainer hands the machine back.
    term.send(b"override clear" + keys.ENTER)
    assert term.wait_for("cleared"), "override clear did not report"
    term.send(b"override list" + keys.ENTER)
    assert term.wait_for("none active"), "override still listed after clear"


def test_control_disable_leaves_an_actuator_commandable(spawn):
    # `control disable` stops the control PROGRAMS, not you: with the scan
    # inhibited an override is the only thing that can move an actuator, which
    # is the whole reason to type it before working on the plant.
    term = shell(spawn)
    term.send(b"control disable" + keys.ENTER)
    assert term.wait_for("by hand"), "control disable did not mention overrides"

    term.send(b"override cli_screen 1" + keys.ENTER)
    assert term.wait_for("forced to"), "an output could not be forced by hand"
    term.send(b"get cli_screen" + keys.ENTER)
    assert term.wait_for("forced by hand"), "the forced output did not stick"

    term.send(b"override clear" + keys.ENTER)
    term.send(b"control enable" + keys.ENTER)
    assert term.wait_for("control: enabled"), "control could not be re-enabled"


def test_set_refuses_a_non_parameter(spawn):
    term = shell(spawn)
    term.send(b"set grid_L1_V 10" + keys.ENTER)
    assert term.wait_for("not a parameter"), "set accepted a sensor"
    assert term.wait_for("override"), "set did not point at override"


def test_control(spawn):
    term = shell(spawn)

    # Bring-up hands control over, so a running firmware reports enabled.
    term.send(b"control" + keys.ENTER)
    assert term.wait_for("control: enabled"), "control not enabled after boot"

    term.send(b"control disable" + keys.ENTER)
    assert term.wait_for("safe state"), "control disable did not report"
    term.send(b"control" + keys.ENTER)
    assert term.wait_for("DISABLED"), "control state did not stick"

    term.send(b"control enable" + keys.ENTER)
    assert term.wait_for("control: enabled"), "control could not be re-enabled"


def test_list_filters_by_prefix(spawn):
    term = shell(spawn)

    # A prefix is how you navigate a few hundred variables: they are named by
    # domain, so `list grid_` is the subsystem view.
    # A narrow prefix: a whole group's worth of rows scrolls the head of the
    # listing off the shell's log area.
    term.send(b"list grid_L1_" + keys.ENTER)
    assert term.wait_for("grid_L1_V"), "prefix list missed a matching variable"

    term.send(b"list zzz_nothing" + keys.ENTER)
    assert term.wait_for("nothing matches"), "a prefix matching nothing said otherwise"

    # `get` is now strictly one variable; it points at `list` to browse.
    term.send(b"get" + keys.ENTER)
    assert term.wait_for("usage: get"), "bare get did not show usage"


def test_features(spawn):
    term = shell(spawn)

    # The flags are compile-time, so this reports what the binary IS. It answers
    # what `version` cannot: an absent subsystem is otherwise indistinguishable
    # from a broken one over the terminal.
    term.send(b"features" + keys.ENTER)

    # The reference site turns the network group on in its own config.h, and
    # leaves the standalone sun tracker off - so this asserts both partitions
    # carry rows, not just that something printed.
    assert term.wait_for("console"), "features did not list the console"
    assert term.wait_for("mqtt"), "features did not list the site's network group"
    assert term.wait_for("off : sun"), "sun is off by default and must say so"
