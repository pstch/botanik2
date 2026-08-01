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
    assert term.wait_for("read variables"), "get missing from help"
    assert term.wait_for("edit a parameter"), "set missing from help"
    assert term.wait_for("force one"), "override missing from help"

    term.send(b"get ntp_server1" + keys.ENTER)
    assert term.wait_for("ntp_server1"), "get <name> found nothing"

    term.send(b"set ntp_poll_s 900" + keys.ENTER)
    assert term.wait_for("ntp_poll_s = 900"), "set did not apply"

    term.send(b"get ntp_poll_s" + keys.ENTER)
    assert term.wait_for("900"), "set did not round-trip through get"

    term.send(b"set nosuchthing 1" + keys.ENTER)
    assert term.wait_for("unknown variable"), "unknown name not reported"


def test_override(spawn):
    term = shell(spawn)

    # Nothing forced on a fresh boot.
    term.send(b"override" + keys.ENTER)
    assert term.wait_for("none active"), "fresh boot reported an override"

    # Force a live sensor reading and read it back through `get`.
    term.send(b"override pwr_L1_V 123.5" + keys.ENTER)
    assert term.wait_for("forced to"), "override did not take"
    term.send(b"get pwr_L1_V" + keys.ENTER)
    assert term.wait_for("OVERRIDDEN"), "get did not flag the override"
    assert term.wait_for("123.5"), "override value did not reach the reading"

    # A forgotten override must be one command away.
    term.send(b"override" + keys.ENTER)
    assert term.wait_for("do NOT survive a reboot"), "override list missing"

    # Release it; the live value takes over again.
    term.send(b"override pwr_L1_V off" + keys.ENTER)
    assert term.wait_for("override off"), "override was not released"
    term.send(b"override" + keys.ENTER)
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
