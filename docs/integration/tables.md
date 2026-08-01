# The declarative tables {#int_tables}

Beyond the variable table itself (@ref int_variables), a site declares four more
tables and bundles the lot into one `SiteDescriptor`.

## SiteDescriptor

The whole handoff to the library. Field order:

| # | Field | Type |
|---|---|---|
| 1 | `name` | `const char*` - must equal `Config::SITE_NAME` |
| 2-3 | `commands`, `command_count` | `const Shell::ShellCommand*`, `uint8_t` |
| 4-5 | `status_groups`, `status_group_count` | `const Vars::VarGroup*`, `uint8_t` |
| 6-7 | `home_fields`, `home_field_count` | `const Vars::HomeVar*`, `uint8_t` |
| 8-9 | `alarms`, `alarm_count` | `const Alarms::AlarmCondition*`, `uint8_t` |
| 10 | `vars` | `Vars::VarTables` - **by value** |
| 11-13 | `tasks`, `task_count`, `task_stats` | `const Scan::Task*`, `uint8_t`, `Scan::TaskStats*` |

```cpp
const Botanik::SiteDescriptor g_site = {
    Botanik::Config::SITE_NAME,
    g_commands,      g_command_count,
    g_status_groups, g_status_group_count,
    g_home_fields,   g_home_field_count,
    g_alarms,        g_alarm_count,
    SITE_VAR_TABLES,
    g_tasks,         g_task_count,   g_task_stats,
};
```

Any table may be null with count 0. Every pointer is **borrowed** and must outlive
the firmware - site statics, not locals.

@note `SiteDescriptor` has no default member initialisers, deliberately. An NSDMI
would stop it being a C++11 aggregate, and the descriptor would no longer
constant-initialise into flash.

## Status groups and home fields

```cpp
struct VarGroup { const char* topic; const VarRef* refs; uint16_t count; };
struct HomeVar  { const char* label; VarRef ref; };
```

One `VarGroup` table, three consumers: the MONITOR page, the shell's `get`, and MQTT
telemetry (published as `<prefix>/<telemetry>/<topic>/<name>`, where the leaf is the
variable's own `name`). There is no second place where names must be kept in sync.

```cpp
const VarRef g_grp_power[] = { {V_PWR_LN_V}, {V_PWR_LL_V}, {V_PWR_HZ}, ... };

#define GRP(nm) { #nm, g_grp_##nm, sizeof(g_grp_##nm)/sizeof(g_grp_##nm[0]) }
const VarGroup g_status_groups[] = { GRP(power), GRP(ats), GRP(climate), ... };
```

Home fields are the landing-page readings, laid out in three columns of eight,
filled column-major. Column 1 holds only six - its top two cells go to the uptime
readout and a spacer - so the cap is **22**. Labels are truncated at 11 characters,
values at 13.

```cpp
const HomeVar g_home_fields[] = { { "Line V", { V_PWR_LN_V } }, ... };

static_assert(g_home_field_count <= Config::HOME_FIELDS_MAX,
              "too many home fields: the extras would be silently dropped");
```

## Alarm conditions

Each condition names the boolean variable that carries it. The model reads that
variable each beat, applies the dwell policy, and announces transitions - there is no
aggregate bitset anywhere.

The static metadata a site fills, in order:

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `code` | `const char*` | short code, max 8 chars |
| 2 | `desc` | `const char*` | human-readable description |
| 3 | `ref` | `Vars::VarRef` | the boolean carrying the condition |
| 4 | `on_s` | `int32_t` | seconds ON before announcing (-1 disables) |
| 5 | `off_s` | `int32_t` | seconds OFF before announcing (-1 disables) |
| 6 | `recent_s` | `int32_t` | the "recent" filter window (0 = one week, <0 = never expires) |
| 7 | `off_delay_s` | `int32_t` | hold ON this long after the raw signal clears |
| 8 | `min_on_s` | `int32_t` | minimum ON time from onset |
| 9 | `notes` | `const char*` | remediation text, may be null |

```cpp
{"AC_GRID", "Grid AC out-of-spec/offline", {V_ALARM_AC_GRID}, 2, 2, 0, 0, 10,
 "check mains breaker / utility feed"},
```

A dynamic suffix follows, which the model owns and the site leaves zeroed - hence
the `-Wmissing-field-initializers` suppression around the table. `notes` is last in
the static block precisely so rows that stop earlier still zero-initialise it.

Write the `notes`. It is what the operator reads on the ALARMS page when the alarm
fires, and it is the one place to say something specific to the real installation.

```cpp
static_assert(g_alarm_count <= Config::SITE_ALARMS_MAX,
              "too many site alarms: the tail would be clamped and never fire");
```

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

Control programs (`Scan::Task`) are covered in @ref int_tasks.

## Capacity limits

These live in `libbotanik_config.h` and each is cross-asserted at its authoritative
source, so the two cannot drift.

| Limit | Default | Bounds |
|---|---|---|
| `HOME_FIELDS_MAX` | 22 | home fields (3 columns x 8, minus the uptime cells) |
| `ALARM_SLOTS` | 64 | total alarm conditions |
| `PLATFORM_ALARMS_RESERVED` | 8 | reserved for the platform-health alarms |
| `SITE_ALARMS_MAX` | 56 | site alarm conditions (`ALARM_SLOTS` minus the reserved block) |

Platform alarms are appended **after** the site's, so site conditions always fill
from index 0.
