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
# ALT_4 -> ALARMS, ALT_5 -> PARAMS, ALT_6 -> MONITOR, ALT_7 -> TASKS,
# ALT_8 -> NOTES (terminal.cpp routes ALT_n to switch_to(n-1)). STATUS was
# removed from the tab order. The full set is named: test_pages.py walks every
# registered page, so every one of these is pressed by something.
ALT_1 = ESC + b"1"
ALT_2 = ESC + b"2"
ALT_3 = ESC + b"3"
ALT_4 = ESC + b"4"
ALT_5 = ESC + b"5"
ALT_6 = ESC + b"6"
ALT_7 = ESC + b"7"
ALT_8 = ESC + b"8"

# Alt+letter: word motion, and Alt+W to copy a table's selected cell into the
# kill ring (scroll_table.cpp).
ALT_B = ESC + b"b"
ALT_F = ESC + b"f"
ALT_W = ESC + b"w"

# Cursor / editing keys (CSI forms).
UP = ESC + b"[A"
DOWN = ESC + b"[B"
RIGHT = ESC + b"[C"
LEFT = ESC + b"[D"
END_KEY = ESC + b"[F"
DELETE = ESC + b"[3~"
PAGE_UP = ESC + b"[5~"
PAGE_DOWN = ESC + b"[6~"

# Shift+arrow: the xterm modifier form, param2 = 1 + Shift. Scrolls a page's
# scrollback one line (the shell's bare UP/DOWN are its command history).
SHIFT_UP = ESC + b"[1;2A"
SHIFT_DOWN = ESC + b"[1;2B"

# Control bytes.
BACKSPACE = b"\x7f"
CTRL_A = b"\x01"             # line start
CTRL_K = b"\x0b"             # cut to end of line, into the kill ring
CTRL_L = b"\x0c"             # console-level full repaint, chrome included (console.cpp)
CTRL_O = b"\x0f"             # NOTES: write the note out (nano's key, not ^S/XOFF)
CTRL_U = b"\x15"             # clear input
CTRL_W = b"\x17"             # cut the word before the cursor
CTRL_Y = b"\x19"             # paste the kill ring back

SPACE = b" "


def text(s):
    """Encode a printable string as input bytes."""
    return s.encode("utf-8")
