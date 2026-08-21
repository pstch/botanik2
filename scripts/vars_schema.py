#!/usr/bin/env python3
"""What each table column MEANS, in one place.

The checker reports against these names and the integration docs are generated
from them, so a column cannot be documented as one thing and enforced as
another. Adding a column to vars_extract.FILES without an entry here is caught
by scripts/vars_report.py --schema.
"""

# column -> (short label, what it is)
COLUMNS = {
    # identity and presentation
    "scope":     ("scope", "which factory this row is: the file's own kinds, "
                           "when it holds more than one. `SVR` is `SV` "
                           "RETAINED, `GR` is `G` retained"),
    "name":      ("name", "the symbolic name. The storage key, the MQTT leaf, and "
                          "what a program resolves. Its prefix up to the first "
                          "underscore is its display group"),
    "type":      ("type", "the row's own value type (T_BOOL, T_U16, T_F32, ...)"),
    "def":       ("default", "an input's fallback once invalid, an output's SAFE "
                             "state, a parameter's factory value"),
    "min":       ("min", "a parameter's lowest accepted edit; an input's lowest "
                         "believable reading"),
    "max":       ("max", "a parameter's highest accepted edit; an input's highest "
                         "believable reading"),
    "digits":    ("dgt", "decimals shown on the HMI (T_F32 only)"),
    "unit":      ("unit", "engineering unit, shown beside the value"),
    "access":    ("access", "RW (the default, left empty) or RO. A parameter "
                            "an external master may read but never write"),
    "range":     ("accepts", "what a new value is checked against: a band, an "
                             "enum converter, or a string kind"),
    "_scaling":  ("scaling", "which calibration this row is scaled by - derived "
                             "from the parameters it names, not declared"),
    "enum":      ("enum", "the vocabulary this member belongs to, and the C++ "
                          "enum the generator emits"),
    "member":    ("member", "the C++ enumerator. Unscoped, so it must be unique "
                            "across every vocabulary in the file"),
    "code":      ("code", "what goes on the wire. A CONTRACT: a vocabulary may "
                          "gain members and must never renumber them"),
    "label":     ("label", "what an operator reads and types. May change freely"),
    "enum":      ("enum", "the vocabulary this member belongs to, and the C++ "
                          "enum the generator emits"),
    "member":    ("member", "the C++ enumerator. Unscoped, so it must be unique "
                            "across every vocabulary in the file"),
    "code":      ("code", "what goes on the wire. A CONTRACT: a vocabulary may "
                          "gain members and must never renumber them"),
    "label":     ("label", "what an operator reads and types. May change freely"),
    "desc":      ("description", "one line, shown under the value"),
    "notes":     ("notes", "free text. On a field row, the published map entry it "
                           "was transcribed from"),
    "feature":   ("feature", "BOTANIK_FEATURE_* this row is compiled under; empty "
                             "means always. Several are ANDed"),
    "retain":    ("retain", "`true` on a row whose value IS the history: litres, "
                            "hours run, the day a tray was last watered. A "
                            "computed row is then kept across a reboot (IEC "
                            "RETAIN); a parameter already persists, so it is "
                            "`params reset` that leaves it alone"),

    # persistence and publication
    "reg":       ("reg", "on a parameter, status value or alarm: its register on "
                         "the Modbus TCP slave, hand-assigned and a contract. On a "
                         "field row: the register address on the device. On a "
                         "local row: the channel"),
    "cap":       ("cap", "a text parameter's slot size, including the NUL"),
    "kind":      ("kind", "what a text edit is validated against (STR_IPV4, "
                          "STR_HOST, ...)"),
    "str":       ("converter", "the function rendering an enum code as a word. Its "
                               "labels ARE the row's range"),

    # alarms
    "code":      ("code", "short display code, at most 9 characters"),
    "on_s":      ("on", "seconds the condition must hold before a START is "
                        "announced; -1 never announces it"),
    "off_s":     ("off", "the same for the END of an episode"),
    "recent_s":  ("recent", "how long a cleared alarm stays in the RECENT view; "
                            "0 uses the default, negative is forever"),
    "hold_s":    ("hold", "keep the condition ACTIVE this long past the last raw "
                          "true. The anti-flicker: it bridges a chattering signal "
                          "into one episode"),

    # field bus
    "dev":       ("dev", "Modbus unit address. 0 means a LOCAL channel, and the "
                         "transport follows from that"),
    "fc":        ("fc", "Modbus function code put on the wire (4 read, 5/6 write)"),
    "regs":      ("n", "registers the value spans; 2 for a 32-bit value"),
    "wire":      ("wire", "the type ON THE WIRE, which an analog row converts from"),
    "fallback":  ("fallback", "what the row reads once its reading is invalid"),
    "safe":      ("safe", "where the output is driven when control is withdrawn"),
    "timeout":   ("timeout", "ms a reading may be believed; past it the row goes "
                             "invalid. Required on a bus input"),
    "poll":      ("poll", "ms between reads; empty derives it from the timeout"),
    "word_swap": ("swap", "a 32-bit value whose registers arrive low word first"),
    "invert":    ("invert", "active-low wiring, folded in so the logic reads "
                            "true = asserted"),
    "coil":      ("coil", "coil number on the relay module"),
    "isr":       ("isr", "rising / falling / both makes a LOCAL input an edge "
                         "counter (HAL::Pulse); empty leaves it polled"),

    # analog calibration, inline on the analog rows
    "cal_raw_min": ("raw min", "wire units at the low calibration point"),
    "cal_raw_max": ("raw max", "wire units at the high calibration point"),
    "cal_eng_min": ("eng min", "engineering units at that low point"),
    "cal_eng_max": ("eng max", "engineering units at that high point"),
    "cal_min":     ("band min", "lowest believable engineering value"),
    "cal_max":     ("band max", "highest believable engineering value"),
}

# What a cell may hold, said once.
CELL_RULES = [
    "A cell holds the C++ token text: `T_F32` stays a symbol, `30*24*3600` stays "
    "an expression, `BOTANIK_HOST_STR_LEN` stays a macro.",
    "An empty cell means the column does not apply to that row, and the generator "
    "checks that it really does not.",
    "Columns are addressed BY NAME, never by position, so adding one does not "
    "renumber the rest.",
    "Padding is alignment only and is stripped on read; `scripts/vars_gen.py "
    "--format` re-derives it.",
    "A line whose first non-space character is `#` is a comment. This is where a "
    "group heading or a register-block legend goes.",
    "A calibration cell is either `parameter@default` (operator-trimmable) or "
    "`=constant` (fixed in firmware).",
]


# The field-bus tables have a namespace of their own: `unit` there is a Modbus
# address, and `unit` on a variable row is degrees Celsius.
DEVICE_COLUMNS = {
    "model":      ("model", "the C++ symbol the generated DeviceModel takes "
                            "(`MDL_<model>`), and what a device row names"),
    "shape":      ("shape", "which factory builds it: meter (registers, "
                            "aligned), io_board (registers, one per channel) or "
                            "relay_board (coils)"),
    "size":       ("size", "the shape's ceiling for ONE request: registers, or "
                           "the coil count. 0 means the protocol's own"),
    "align":      ("align", "what a start address and a quantity must be a "
                            "multiple of; 2 on a device storing 32-bit values in "
                            "register pairs. Meters only"),
    "multi":      ("multi", "whether FC15/FC16 exist; empty takes the shape's "
                            "own default. Without them the device is driven a "
                            "point at a time"),
    "gap_ms":     ("gap", "silence this model needs after its answer, in ms, "
                          "when that is more than the bus default"),
    "timeout_ms": ("timeout", "ms this model may take to ANSWER; empty uses the "
                              "bus deadline. A device slower than it is otherwise "
                              "indistinguishable from a dead one"),
    "unit":       ("unit", "the Modbus unit address, 1..247. Unit 0 is the "
                           "controller's own terminals and is never declared"),
    "tag":        ("tag", "the plant's own designation, the label on the "
                          "enclosure, so a log line names a cabinet"),
    "desc":       ("description", "the model name that reaches the logs and the "
                                  "`modbus` command"),
    "notes":      ("notes", "free text. On a model, what the manual says the "
                            "device will and will not answer"),
}
