"""Input byte sequences the firmware's InputParser decodes (terminal/parser.cpp).

Tests speak the real wire protocol: raw bytes a terminal would send. Sourced
from InputParser::_map_control_char / _map_esc_extension / _map_csi_to_command.
"""

ESC = b"\x1b"

# Submit vs. insert-a-newline. ENTER (CR) submits the line; SHIFT_ENTER is
# Alt+Enter (ESC + LF), which inserts a hard '\n' (a PS3 continuation row).
ENTER = b"\r"
SHIFT_ENTER = ESC + b"\n"

# Page switch: Alt+digit. ALT_1 -> page 0 (HOME), ALT_2 -> SHELL, ALT_3 -> LOGS,
# ALT_4 -> STATUS, ALT_5 -> ALARMS (terminal.cpp routes ALT_n to switch_to(n-1)).
ALT_1 = ESC + b"1"
ALT_2 = ESC + b"2"
ALT_3 = ESC + b"3"
ALT_4 = ESC + b"4"
ALT_5 = ESC + b"5"
HOME_PAGE = ALT_1            # also reachable via the Home key (CSI H)

# Cursor / editing keys (CSI forms).
UP = ESC + b"[A"
DOWN = ESC + b"[B"
RIGHT = ESC + b"[C"
LEFT = ESC + b"[D"
HOME_KEY = ESC + b"[H"
END_KEY = ESC + b"[F"
DELETE = ESC + b"[3~"

# Control bytes.
BACKSPACE = b"\x7f"
TAB = b"\t"
CTRL_A = b"\x01"             # line start
CTRL_E = b"\x05"             # line end
CTRL_C = b"\x03"             # clear input
CTRL_U = b"\x15"             # clear input
CTRL_D = b"\x04"             # delete forward
CTRL_L = b"\x0c"             # redraw
ALT_B = ESC + b"b"           # word left
ALT_F = ESC + b"f"           # word right

SPACE = b" "


def text(s):
    """Encode a printable string as input bytes."""
    return s.encode("utf-8")
