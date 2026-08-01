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
    term.send(b"override pwr_L1_V 123.5" + keys.ENTER)
    assert term.wait_for("forced to"), "override did not take"
    term.send(b"get pwr_L1_V" + keys.ENTER)
    assert term.wait_for("OVERRIDDEN"), "get did not flag the override"
    assert term.wait_for("123.5"), "override value did not reach the reading"

    # A forgotten override must be one command away.
    term.send(b"override list" + keys.ENTER)
    assert term.wait_for("do NOT survive a reboot"), "override list missing"

    # Release it; the live value takes over again.
    term.send(b"override pwr_L1_V off" + keys.ENTER)
    assert term.wait_for("override off"), "override was not released"
    term.send(b"override list" + keys.ENTER)
    assert term.wait_for("none active"), "override still listed after off"


def test_set_refuses_a_non_parameter(spawn):
    term = shell(spawn)
    term.send(b"set pwr_L1_V 10" + keys.ENTER)
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
    # domain, so `list pwr_` is the subsystem view.
    # A narrow prefix: `list pwr_` now spans the meters AND the 20 voltage-monitor
    # parameters, so the head of that listing scrolls off the shell's log area.
    term.send(b"list pwr_L1_" + keys.ENTER)
    assert term.wait_for("pwr_L1_V"), "prefix list missed a matching variable"

    term.send(b"list zzz_nothing" + keys.ENTER)
    assert term.wait_for("nothing matches"), "a prefix matching nothing said otherwise"

    # `get` is now strictly one variable; it points at `list` to browse.
    term.send(b"get" + keys.ENTER)
    assert term.wait_for("usage: get"), "bare get did not show usage"
