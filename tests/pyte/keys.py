"""Input byte sequences the firmware's InputParser decodes (console/parser.cpp).

Tests speak the real wire protocol: raw bytes a terminal would send. Sourced
from InputParser::_map_control_char / _map_esc_extension / _map_csi_to_command.
"""

ESC = b"\x1b"

# Submit vs. insert-a-newline. ENTER (CR) submits the line; SHIFT_ENTER is
# Alt+Enter (ESC + LF), which inserts a hard '\n' (a PS3 continuation row).
ENTER = b"\r"
SHIFT_ENTER = ESC + b"\n"

# Page switch: Alt+digit. ALT_1 -> page 0 (HOME), ALT_2 -> SHELL, ALT_3 -> LOGS,
# ALT_4 -> ALARMS, ALT_5 -> CONFIG, ALT_6 -> MONITOR (terminal.cpp routes ALT_n
# to switch_to(n-1)). STATUS was removed from the tab order. Only the ones some
# test presses are named here; the rest are ESC + the digit.
ALT_2 = ESC + b"2"
ALT_3 = ESC + b"3"
ALT_4 = ESC + b"4"

# Cursor / editing keys (CSI forms).
UP = ESC + b"[A"
DOWN = ESC + b"[B"
RIGHT = ESC + b"[C"
LEFT = ESC + b"[D"
END_KEY = ESC + b"[F"
DELETE = ESC + b"[3~"

# Control bytes.
BACKSPACE = b"\x7f"
CTRL_A = b"\x01"             # line start
CTRL_U = b"\x15"             # clear input

SPACE = b" "


def text(s):
    """Encode a printable string as input bytes."""
    return s.encode("utf-8")
