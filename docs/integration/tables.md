# The declarative tables {#int_tables}

Beyond the variable table itself (@ref int_variables), a site declares four more
tables and bundles the lot into one `SiteDescriptor`.

## SiteDescriptor

The whole handoff to the library. Field order:

| # | Field | Type |
|---|---|---|
| 1 | `name` | `const char*` - must equal `Config::SITE_NAME` |
| 2-3 | `commands`, `command_count` | `const Shell::ShellCommand*`, `uint8_t` |
| 6-8 | `home_fields`, `home_field_count`, `home_refs` | `const Vars::HomeVar*`, `uint8_t`, `Vars::VarRef*` |
| 9 | `vars` | `Vars::VarTables` - **by value**; alarms are `SCOPE_A` rows in here |
| 10-11 | `programs`, `program_count` | `Plc::Program* const*`, `uint8_t` |

```cpp
const Botanik::SiteDescriptor g_site = {
    Botanik::Config::SITE_NAME,
    g_commands,      g_command_count,
    g_home_fields,   g_home_field_count,   g_home_refs,
    SITE_VAR_TABLES,
    g_programs,      g_program_count,
};
```

Any table may be null with count 0. Every pointer is **borrowed** and must outlive
the firmware - site statics, not locals.

@note `SiteDescriptor` has no default member initialisers, deliberately. An NSDMI
would stop it being a C++11 aggregate, and the descriptor would no longer
constant-initialise into flash.

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

There is no alarm table. An alarm is a **row in the variable table**, declared with
`A()`, carrying both the boolean that raises it and the policy the model runs it on:

```cpp
//name              code        on off recent hold  minOn reg  description                    remediation
A("alarm_ac_grid",  "AC_GRID",  2, 2,  0,     0,    10,   800, "Grid AC out-of-spec/offline", "check mains breaker"),
A("alarm_too_hot",  "TOO_HOT",  3, 3,  0,     0,    15,   813, "Inside temperature too high", "increase ventilation"),
```

| Argument | Meaning |
|---|---|
| `name` | the variable; a control program drives it, the model reads it |
| `code` | short display code, max 8 chars - the ALARMS table's first column and the alarm bar |
| `on_s` / `off_s` | dwell before a START / END is announced; `-1` never announces that edge |
| `recent_s` | RECENT filter window (0 = the default week, `<0` = never expires) |
| `hold_on_s` | hold ON this long after the raw bit clears |
| `min_on_s` | minimum ON time from onset |
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

A site's `g_vars[]` is not only the site's. libbotanik has state that belongs in the
variable model - alarms it raises, readings it publishes, parameters it reads - and
the bank holds **one** contiguous descriptor array, so those rows live in the site's
table. One `#define` splices the lot:

```cpp
const Variable g_vars[] = {
    BOTANIK_LIBRARY_VARS          // the library's rows, first, no comma

    ...the site's parameters...   // must come first among the site's own
    ...everything else...
};
```

No comma after it: every row inside carries its own, so a group with nothing in it
expands to nothing and the rows around it still read correctly.

`library_vars.h` defines four groups - alarms, status values, computed globals,
parameters - each a concatenation of whatever subsystem headers contribute rows to it.
Three are empty today. Filling one is an edit to that file and nothing else; a site
already splicing `BOTANIK_LIBRARY_VARS` picks the new rows up on the next build.

Today the only tenant is the platform-health alarm group (`alarms/platform.h`):
`SYS_RST`, `CPU_XRUN`, `CPU_LOAD`, `STACK`, `CPU_JITT`, `FLASH_FLT`. They are `SCOPE_A`
rows like any other - no second alarm table, no merge, no reserved index range - and
`HealthMonitor` is an ordinary consumer of the bank, resolving the six names once at
bring-up and writing the boolean.

**Two ordering rules**, both from the same place: `Bank` needs the `ROLE_PARAM` rows to
be one contiguous run, because that is what lets the PARAMS page and the shell address
them by a dense 0-based index. So the library's parameters must abut the site's. Sites
put theirs at the top, which pins it: the library block goes **first**, and its
parameters go **last within it**. Nothing else cares about position - alarm conditions
are numbered by table order, so the library's alarms simply head the ALARMS page.

The library reserves Modbus TCP slave registers **900-999** for its own rows, so a site
never has to guess which numbers are safe: parameters and status values grow upward
from 900, its alarms take the top (990-999). A site's own alarms live in 800-899, below
the reserve.

@warning A site that omits the splice still builds and boots, and loses whatever the
library expected to find - today, every platform-health alarm. POST reports each
missing row by name, the bank counts and logs every unresolved lookup, and
`tests/test_site_names.cpp` fails before either.

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
