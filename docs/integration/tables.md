# The declarative tables {#int_tables}

Beyond the variable table itself (@ref int_variables), a site declares its field
devices, its home fields and its shell commands, and bundles what the library
must be handed into one `SiteDescriptor`.

## SiteDescriptor

The whole handoff to the library, and it is smaller than it was: everything a
rule can derive has moved into the tables, and what is left is what only the site
knows.

| # | Field | Type |
|---|---|---|
| 1 | `name` | `const char*` - must equal `Config::SITE_NAME` |
| 2-3 | `commands`, `command_count` | `const Shell::ShellCommand*`, `uint8_t` |
| 4-6 | `home_fields`, `home_field_count`, `home_refs` | `const Vars::HomeVar*`, `uint8_t`, `Vars::VarRef*` |
| 7 | `devices` | `const Modbus::DeviceTable*` - GENERATED from `tables/devices.csv`, gated on `MODBUS_RTU_CLIENT` |

```cpp
const Botanik::SiteDescriptor g_site = {
    Botanik::Config::SITE_NAME,
    g_commands,     g_command_count,
    g_home_fields,  g_home_field_count,  home_refs,
#if BOTANIK_FEATURE_MODBUS_RTU_CLIENT
    &g_device_table,
#endif
};
```

Any table may be null with count 0. Every pointer is **borrowed** and must outlive
the firmware - site statics, not locals.

@note **The variables are not here.** A site hands them over by overriding
`register_vars()` and calling the generated `register_all()`, which holds the
parameters-first ordering the parameter block depends on. Putting the tables in
the descriptor would put that ordering in every site's initialiser.

@note **The programs are not here either.** They are members of the firmware
subclass, registered in `on_setup()`, so there is no table of pointers to keep in
step with the members - and being members, there is exactly one of each by
construction.

@note `SiteDescriptor` has no default member initialisers, deliberately. An NSDMI
would stop it being a C++11 aggregate, and the descriptor would no longer
constant-initialise into flash.

## Field devices

Two CSVs under `tables/`, generated into the `DeviceTable` the descriptor points
at. Two files because a model is shared and a unit is not: twelve identical
sensors are twelve `devices.csv` rows over one `device_models.csv` row.

```
# device_models.csv
model  , shape      , size, align, multi, gap_ms, timeout_ms, desc               , notes
SDM630 , meter      , 80  , 2    ,      ,       ,           , Eastron SDM630     , 40 values per transaction...
WS_RELAY, relay_board, 8  ,      ,      ,       ,           , Waveshare Relay (D), the eight relays are coils 0..7...

# devices.csv
unit, model   , tag          , notes
5   , SDM630  , SRR/TAC/MTR-1, grid supply meter
31  , WS_RELAY, SRR/TDC/RO-1 ,
```

Neither file holds a signal map: **where a reading lives is the variable row's
business**, and this is only what a request must look like for the device to
answer at all (@ref dev_modbus). `shape` picks the factory - `meter` (registers,
aligned), `io_board` (registers, one per channel), `relay_board` (coils) - and
`size` is that shape's ceiling for one request.

The `tag` is the plant's own designation, the label on the enclosure, so a log
line reads `dev 34 (SRR/TDC/RO-4) not answering`. The library allows a null one;
`check-vars` does not, because a nameless device is one somebody added and
stopped halfway.

What `check-vars` proves, and none of it could be checked while the table was
hand-written C++:

- every unit a row addresses is declared - an undeclared one plans against the
  protocol's own limits instead, which is a silent downgrade
- a relay row's coil is within the board's coil count
- a row on an aligned model starts and spans on that alignment. The SDM630
  answers exception 02 to a request starting on an odd register, which on a bus
  reads exactly like a bad cable
- a declared device no row addresses is reported as a note, not an error

## Home fields

```cpp
struct HomeVar { const char* label; const char* name; };
```

The **only** declarative variable table left, because it is the only one no rule
derives: "Line V" is a caption a person chose for `grid_LN_V`, and nothing in the name
says the row belongs on the landing page or where.

```cpp
const HomeVar g_home_fields[] = { { "Line V", "grid_LN_V" }, ... };
extern VarRef g_home_refs[];   // defined in vars.cpp, resolved at bring-up

static_assert(g_home_field_count <= Config::HOME_FIELDS_MAX,
              "too many home fields: the extras would be silently dropped");
```

Laid out in three columns of eight, filled column-major. Column 1 holds only six - its
top two cells go to the uptime readout and a spacer - so the cap is **22**. Labels are
truncated at 11 characters, values at 13.

## Status groups: there aren't any

There used to be a `VarGroup[]` per site: one `VarRef[]` per domain plus a table
listing them. It was 72 rows restating what the names already said, and forgetting to
add a new variable to its group meant it silently never appeared on the MONITOR page
or in telemetry.

Two rules replace it, both properties of the row:

| | |
|---|---|
| `Vars::var_group(d)` | the variable's group: its name up to the first `_`. `grid_LN_V` is in `grid`. |
| `Vars::var_published(d)` | whether it is on the MONITOR page and in MQTT telemetry: true unless its scope is `P` (the PARAMS page owns it) or `A` (the ALARMS page does). |

So naming a variable is what files it. MQTT publishes to
`<prefix>/telemetry/<group>/<name>`, and a new variable is on the broker as soon as it
exists. The failure mode inverts too: a row shows up uninvited, which someone notices,
rather than never appearing, which nobody does.

@note The MONITOR table's rows follow the **variable table's own order** now that no
group table imposes one. Groups stay contiguous because the table is grouped by role,
but their order is the table's.

## Alarm conditions

There is no alarm table beside the variable table. An alarm is a **row in it**,
declared in `tables/alarms.csv`, carrying both the boolean that raises it and the
policy the model runs it on:

```
name          , code   , on_s, off_s, recent_s, hold_s, reg, desc                       , notes
alarm_ac_grid , AC_GRID, 2   , 2    , 0       , 10    , 800, grid AC out-of-spec/offline, check mains breaker
alarm_too_hot , TOO_HOT, 3   , 3    , 0       , 15    , 813, inside temperature too high, increase ventilation
```

| Column | Meaning |
|---|---|
| `name` | the variable; a control program drives it, the model reads it. A program's own table says WHICH program, and `check-vars` refuses two |
| `code` | short display code, max 8 chars - the ALARMS table's first column and the alarm bar |
| `on_s` / `off_s` | dwell before a START / END is announced; `-1` never announces that edge. They gate the log line and the notifier fan-out ONLY - neither touches whether the alarm is active |
| `recent_s` | RECENT filter window (0 = the default week, `<0` = never expires) |
| `hold_s` | keep the alarm ACTIVE this long past the last raw true, re-armed while the bit is up. The anti-flicker: it folds a chattering signal into one episode, and keeps a pulse too short to see up long enough to be acknowledged |
| `reg` | its register on the Modbus TCP slave, where it is published as a read-only coil. Hand-assigned like a parameter's and in the same space, but in a block of its own: a site's alarms in 800..899, the library's at the top of its 900..999 reserve |
| `desc` | shown in the ALARMS table |
| `notes` | the remediation line, shown under the table when the alarm is selected. Write it - it is the one place to say something specific to the real installation |

`AlarmController` builds its working conditions by scanning the bank for `SCOPE_A` rows
**in table order**, so condition *i* reads row *i*. That removes the whole class of
drift where an alarm table's row order and the variable it named had to be kept in
step by hand.

The policy lives in the `Payload` union's alarm arm, which fits inside the 16 bytes
a numeric band already costs - so carrying it on the row is free.

### The library's own rows

The bank holds **one** flat index space, and libbotanik has state that belongs in
the variable model - alarms it raises, readings it publishes, parameters it reads.
Those rows are declared exactly as a site's are, in CSV under
`src/libbotanik/vars/tables/`, and generated into
`src/libbotanik/vars/generated/vars.gen.h`. Nothing is spliced into a site's
table and no site mentions them: `Bank::register_vars()` is called once for the
library's set and once for the site's.

The library declares 62 rows today: the platform-health alarms (`SYS_RST`,
`CPU_XRUN`, `CPU_LOAD`, `STACK`, `CPU_JITT`, `FLASH_FLT`, the two field-bus ones
and `SCAN_LATE`), the field-bus counters, and the network, NTP, MQTT, console,
Modbus, RTU and platform-threshold parameters. Every one is gated on the feature
that reads it, in the `feature` column, so a bare build carries none of them.

**Whatever the library reads by name, the library declares.** That is the rule
that keeps a site from having to know what the firmware wants.

**Two ordering rules**, both from the same place: `Bank` needs the `ROLE_PARAM`
rows to be one contiguous run, because that is what lets the PARAMS page and the
shell address them by a dense 0-based index. So the library's parameters must
abut the site's. The library registers first and puts its parameters **last**
within its own set; a site registers second and puts its parameters **first**.
Both halves are the generator's doing, not a comment asking for it.

The library reserves Modbus TCP slave registers **900-999** for its own rows, so a
site never has to guess which numbers are safe: parameters and status values grow
upward from 900, its alarms take the top (986-999). A site's own alarms live in
800-899, below the reserve.

@note The parameter half is nearly full - 53 rows in the 86 between 900 and 985.
The next library row that needs more room moves the boundary rather than
renumbering anything: 600-799 is unused by any site today.

## Redeclaring a library row

A site's table may restate any row the library declared. `Bank::freeze()` keeps the
**last** declaration of a name and drops the earlier row from the table completely,
so the site's version is simply the row from then on:

```
# in the SITE's tables/params.csv, which registers after the library's rows
scope, name    , cap                 , kind    , str, reg, desc        , notes
PS   , ntp_srv1, BOTANIK_HOST_STR_LEN, STR_HOST,    , 924, NTP server 1, pinned by this site
```

This is how a site pins a library row's factory value, and it is not limited to the
value: the band, unit, digits, description and remediation text are the site's to
set, because the declaration is the whole row. There is nothing to add on the
library side - any row is overridable without anyone having anticipated it.

Two rules:

- **Keep `reg` as the library assigned it.** It is the same row to an external
  master, and moving it renumbers a map somebody holds.
- **Do not change `role` or `scope`.** A parameter must stay a parameter, or the
  contiguous `ROLE_PARAM` run the PARAMS page indexes into breaks - `freeze()`
  reports that as a stray parameter and POST fails on the count.

The shadowed row is *removed*, not marked: nothing that walks the table sees two
rows, so the register-uniqueness and name-resolution checks need no special case.
The cost is that a duplicate name is now legal, where it used to be impossible: two
rows that collide by accident quietly become one. Every shadow is therefore logged by
name at freeze, on the `BOOT` topic - check that log on a site that means to redeclare
nothing, which is the reference site's position.

## Shell commands

```cpp
struct ShellCommand {
    const char* name;
    const char* help;
    void (*fn)(ShellWriter& out, const char* args, uint16_t arglen, void* ctx);
};
```

`ctx` is the opaque pointer the firmware passes at registration - by default the live
`Vars::Bank*`, so a command can read and write the field:

```cpp
static void cmd_gh(Shell::ShellWriter& out, const char*, uint16_t, void* ctx) {
    const Vars::Bank* io = static_cast<const Vars::Bank*>(ctx);
    ...
}

const Shell::ShellCommand g_commands[] = {
    { "gh", "greenhouse summary (live variables)", &cmd_gh },
};
```

@warning Site commands are matched **before** the built-ins, so a site table can
shadow `exit` or `reset`. Do not reuse a built-in name.

Control programs (`Plc::Program`) are covered in @ref int_tasks.

## Capacity limits

These live in `libbotanik_config.h` and each is cross-asserted at its authoritative
source, so the two cannot drift.

| Limit | Default | Bounds |
|---|---|---|
| `HOME_FIELDS_MAX` | 22 | home fields (3 columns x 8, minus the uptime cells) |
| `ALARM_SLOTS` | 64 | `SCOPE_A` rows the model has room for - the site's and the library's together, since they share one table |

Platform alarms are appended **after** the site's, so site conditions always fill
from index 0.

@note `VAR_MAX` bounds the whole table and is a site override: the bank's value
store and name index are sized off it, so trimming it to what the site declares
is what stops a 512-row default costing RAM nobody uses.
`scripts/vars_gen.py` reports the count against it and fails when the table
outgrows it, before the firmware is built.

@note `MODBUS_TCP_MAX_ROWS` bounds the register map, which holds every parameter,
status value and alarm - 306 on the reference site, past the 256 default. A site
that outgrows it serves a SHORT map: the rows past the end answer nothing, which
an integrator reads as a device that does not have them. The slave logs it as a
critical at bring-up.
