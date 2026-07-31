# Site interface contract

The complete contract between `libbotanik` and one greenhouse's site-specific
code. Written to be followed literally, by a code generator or by an agent
maintaining a site.

`libbotanik` is generic. Everything specific to one physical greenhouse and its
PLC program is a **site**: a single directory, owned by the integrator's project
(typically the PLC IDE's `LLSketch` folder), holding the PLC payload layout and
the declarative tables describing it.

**A site is data, not code.** The library owns every page, every backend and the
whole loop order. A site supplies tables and one thin binding class. If you find
yourself writing behaviour in a site, check first whether a table can express it.

Read this together with `src/sites/gh_mlb41/`, which is the reference site and is
kept working; when this document and that directory disagree, the directory wins
and this document is a bug.

---

## 1. Files

Exactly these names. The generator owns the GENERATED ones and may overwrite them
wholesale; it must never touch the others.

| File | Owner | Contents |
|---|---|---|
| `botanik.h` | GENERATED | the only file that knows where `libbotanik` sits relative to this directory |
| `payload.h` | GENERATED | `PlcInputData_t` / `SiteData_t`, field-domain enums, layout asserts |
| `status.h` | GENERATED | read-only status variables + home-screen readings |
| `params.h` | GENERATED | writable config parameters |
| `alarms.h` | GENERATED | alarm conditions |
| `descriptor.h` | GENERATED | the `SiteDescriptor` bundling the tables |
| `config.h` | GENERATED | build-wide `BOTANIK_*` overrides |
| `commands.h` | hand-written | shell commands (code, not schema) |
| `sim.h` | hand-written | the mock PLC: data animator + emulator instance |
| `site.h` | hand-written | the firmware binding, `using BotanikSite` |

`site.h` is the entry point a harness includes. It pulls `descriptor.h` (which
pulls the rest) and `sim.h`.

## 2. Include rules

These are not style. They are what makes a site work both in the library tree and
vendored into an Arduino PLC IDE sketch, where **no include directory inside the
sketch folder exists** - not the sketch root, not `src/`. A quoted include
resolves against the including file's own directory, and that is the only
mechanism available.

1. **Sibling files by bare name**: `#include "data.h"`.
2. **The library only through `botanik.h`**: never `#include "libbotanik/..."`
   from a site file. `botanik.h` holds those paths, and is the single file to
   regenerate if the site directory moves.
3. **One deliberate exception**: `sim.h` includes `"botanik_sim/emulator.h"`
   src-rooted, because the emulator lives in the simulator harness rather than the
   library, is only ever built by our CMake (which supplies `-Isrc`), and sits
   inside the `TARGET_SIMULATOR` guard so a sketch build never reaches it.

Getting rule 2 wrong does not necessarily fail the build: an absolute-style path
can silently resolve to the *reference* site in the library tree instead of yours.

## 3. Namespacing

- **Payload types stay at global scope.** They mirror the PLC IDE's generated
  types and are the wire contract.
- **Everything else goes in `namespace Site`**, with `using namespace Botanik;`
  *inside* that namespace so the generated headers leak nothing into includers.

## 4. The payload

`payload.h` declares the two structs exchanged with the M4 coprocessor.
`SiteData_t` is what the PLC publishes (sensors, actuator feedback, alarm
booleans, reflected config); `PlcInputData_t` is what the host writes back
(staged config edits).

Rules, all enforced by `static_assert` in the generated file:

- `sizeof(...) % 4 == 0` for both. The combined `PlcInput/PlcOutput<>` must be
  padding-free and `ordered_copy` requires a word multiple.
- `sizeof(...) <= Config::PAYLOAD_OFFSET_MAX` (32767). Every table addresses the
  payload with a 16-bit byte offset.
- Order members **widest-first** and pad explicitly, so no implicit padding
  appears.
- One `uint8_t` boolean per alarm. There is no packed alarm bitset.
- Real-valued quantities are `float` in native units. Do not invent scaled-integer
  conventions.

Field **order is load-bearing**: every table refers to fields by `offsetof`.

Field-domain enums live here too, each as a struct wrapping `enum Value : uint8_t`
plus a `static const char* str(uint8_t)`. Payload fields stay raw `uint8_t`; the
tables point at `str` to render them.

## 5. The tables

Every row is a positional aggregate. **Field order is the highest-risk part of
this contract**: a misplaced string still compiles and silently mis-renders, and a
misplaced offset silently reads a neighbouring field. Emit every field explicitly.

### 5.1 Status variables (`status.h`)

`Botanik::ScalarField`, read-only, so `in_offset` is always `-1`:

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `name` | `const char*` | leaf name, without the group prefix |
| 2 | `out_offset` | `int16_t` | `offsetof(SiteData_t, field)` |
| 3 | `in_offset` | `int16_t` | `-1` for a status variable |
| 4 | `type` | `ScalarType` | read width and sign |
| 5 | `digits` | `uint8_t` | `SCALAR_F32` decimals, else `0` |
| 6 | `str` | `const char* (*)(uint8_t)` | `SCALAR_ENUM` only, else `nullptr` |
| 7 | `unit` | `const char*` | e.g. `"C"`, `""` for none |
| 8 | `desc` | `const char*` | one line |
| 9 | `notes` | `const char*` | caveats, may be `""` |

`ScalarType` is one of `SCALAR_U8`, `SCALAR_U16`, `SCALAR_U32`, `SCALAR_I16`,
`SCALAR_I32`, `SCALAR_F32`, `SCALAR_ENUM`, `SCALAR_STRING` (string is config-only).

Fields are collected into `ScalarGroup { topic, fields, count }`, and the groups
into `g_status_groups[]` + `g_status_group_count`. One table feeds three
consumers: the MONITOR page, the shell `status` command, and MQTT telemetry under
`<prefix>/telemetry/<topic>/<name>`. Leaf names therefore drop the group prefix:
`ac_supply/grid_v`, not `ac_supply/grid_ac_v`.

Home-screen readings are `HomeField { label, ScalarField var }` in
`g_home_fields[]`. The page lays out three columns of 8, filled column-major -
column 1 holds 6, its top two cells carrying the uptime readout and a spacer
(labels capped at 11 chars, values at 13); extras past
`Config::HOME_FIELDS_MAX` used to be dropped silently, so the generated file must
carry the `static_assert`.

### 5.2 Config parameters (`params.h`)

`Botanik::Params::Param` is a `ScalarField` (as above, but writable, so
`in_offset >= 0`) paired with a `ParamMeta`:

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `def` | `const char*` | default, as display text |
| 2 | `min` | `int32_t` | inclusive lower bound (numeric types) |
| 3 | `max` | `int32_t` | inclusive upper bound |
| 4 | `len` | `uint16_t` | `SCALAR_STRING` buffer size incl NUL, else `0` |
| 5 | `kind` | `uint8_t` | `StrKind` for strings, else `STR_NONE` |
| 6 | `group` | `const char*` | config-group name |

`StrKind` is `STR_NONE`, `STR_IPV4`, `STR_IPV4_OPT`, `STR_HOST`, `STR_HOST_OPT`
(the `_OPT` forms accept empty).

A parameter is declared over **both** payloads, which are different layouts:
`out_offset` reads the live value the PLC reflects, `in_offset` stages an edit.

**The commit model matters and is easy to get wrong.** Editing a value only
*stages* it. Each group has a commit serial, which is an ordinary `uint32_t`
parameter **named exactly after the group**: editing parameter `"net"` ships every
staged edit in group `"net"`. The PLC latches the group and reflects the serial
back, which the consuming component watches. So for every group you emit, emit a
same-named serial parameter alongside it.

### 5.3 Alarms (`alarms.h`)

`Botanik::Alarms::AlarmCondition`, site-authored prefix only (the dynamic suffix
is left zeroed for the model):

| # | Field | Type | Meaning |
|---|---|---|---|
| 1 | `code` | `const char*` | short code, max 8 chars |
| 2 | `desc` | `const char*` | human-readable |
| 3 | `offset` | `uint16_t` | `offsetof(SiteData_t, alarm_x)` |
| 4 | `on_s` | `int32_t` | dwell ON before announcing; `-1` disables |
| 5 | `off_s` | `int32_t` | dwell OFF before announcing; `-1` disables |
| 6 | `recent_s` | `int32_t` | RECENT-mode window; `0` = default, `-1` = never expires |
| 7 | `off_delay_s` | `int32_t` | hold ON after the raw bit clears; `0` = none |
| 8 | `min_on_s` | `int32_t` | minimum ON from onset; `0` = no latch |
| 9 | `notes` | `const char*` | remediation note, optional |

**Row index is the alarm bit**, so row order must match the payload's alarm
booleans and is part of the wire contract. Note `offset` is `uint16_t` here while
`ScalarField` offsets are `int16_t` (which uses `-1` as its read-only sentinel).

The model splices the generic platform-health alarms in above yours, so a site may
declare at most `Config::SITE_ALARMS_MAX`. Overrunning it used to clamp and
silently drop the tail; the generated file must carry the `static_assert`.

### 5.4 Shell commands (`commands.h`, hand-written)

`Botanik::Shell::ShellCommand { name, help, fn }` where `fn` is
`void (*)(ShellWriter& out, const char* args, uint16_t arglen, void* ctx)`. `ctx`
is the live `const SiteData_t*`. Registered after the built-ins, so a site
command cannot shadow `exit` or `reset`.

### 5.5 The descriptor (`descriptor.h`)

`Botanik::SiteDescriptor`, positional, each table followed by its `uint8_t` count:

```
name, params, param_count, commands, command_count,
status_groups, status_group_count, home_fields, home_field_count,
alarms, alarm_count
```

Any table may be `nullptr` / `0`. Every pointer is borrowed and must outlive the
firmware, which site statics do. All counts are `uint8_t`, so no table may exceed
`Config::TABLE_ROWS_MAX` (255).

## 6. Capacity limits

All in `libbotanik_config.h`, all `BOTANIK_*`-overridable, each pinned to its
authoritative source by a `static_assert` in the library so they cannot drift.
Every one of these used to fail *silently*; generated tables must assert against
them.

| Constant | Default | Guards against |
|---|---|---|
| `Config::HOME_FIELDS_MAX` | 22 | home fields past the three columns being ignored |
| `Config::SITE_ALARMS_MAX` | 56 | the alarm model clamping and dropping the tail |
| `Config::ALARM_SLOTS` | 64 | total slots, site + platform |
| `Config::PLATFORM_ALARMS_RESERVED` | 8 | the library's own share |
| `Config::PAYLOAD_OFFSET_MAX` | 32767 | a 16-bit table offset truncating |
| `Config::TABLE_ROWS_MAX` | 255 | a `uint8_t` count wrapping |

## 7. The firmware binding (`site.h`)

Thin by design: no members, no hook overrides in the common case.

```cpp
class GreenhouseXXX final : public Botanik::BotanikFirmware<PlcInputData_t, SiteData_t> {
public:
    GreenhouseXXX(Botanik::PlcIO::PlcInput<PlcInputData_t>*   in,
                  Botanik::PlcIO::PlcOutput<SiteData_t>* out)
        : Botanik::BotanikFirmware<PlcInputData_t, SiteData_t>(in, out, Site::g_site) {}

    GreenhouseXXX() : GreenhouseXXX(hal_input(), hal_output()) {}   // binds the HAL region
};
using BotanikSite = GreenhouseXXX;
```

Two constructors on purpose: the explicit-region one for the simulator, the
default one for hardware (it binds the coprocessor region through the HAL).

`setup()` and `loop()` are **not** overridable - the bring-up order and the
per-tick order are load-bearing (see `CONCURRENCY.md`). A site that genuinely
needs more customises through the protected hooks: `register_pages()` (tab order,
page 0 is the boot page), `register_commands()`, `register_telemetry()`,
`on_setup()`, `on_step(now_ms)`.

## 8. The mock PLC (`sim.h`, hand-written)

Whole body guarded by `#ifdef TARGET_SIMULATOR`, so it vanishes on hardware where
the real coprocessor plays this role. Two required names:

- `inline uint32_t dilated_timeout(uint32_t base_ms)` at **global** scope
  (the emulator forward-declares it there).
- `Site::MockPlc`, which owns the shared region and the emulator:
  `input()`, `output()`, `start(Botanik::Logs::Logger*)`.

Its animator is `Site::fill_output(SiteData_t* o, const PlcInputData_t* in,
uint32_t now_ms)`, called about once a second from the emulator thread.

The site owns this rather than the harness so the simulator runs against **your**
generated payload types: same layout, same table offsets, same alarm bits as the
hardware build. `fill_output` should also honour the config commit serials, since
latching a staged group is the coprocessor's job on hardware.

## 9. `config.h`

Build-wide `#define BOTANIK_*` overrides, pulled in by `libbotanik_config.h`
*before* its own defaults, so the whole build agrees. At minimum
`BOTANIK_SITE_NAME`. Feature flags (`BOTANIK_FEATURE_NETWORK` / `_NTP` / `_MQTT` /
`_SUN` / `_CONSOLE` / `_MONITOR` / `_PLC`) belong here.

Kept as its own file because it must be included before the library defaults, and
because the vendoring step points a generated `botanik_site_config.h` at it.

## 10. Building against a site

```sh
# a site in the library tree
cmake -B build -DBOTANIK_TARGET=SIMULATOR -DBOTANIK_SITE=<name>

# a site anywhere on disk, e.g. inside the integrator's project
cmake -B build -DBOTANIK_TARGET=SIMULATOR -DBOTANIK_SITE_DIR=/path/to/LLSketch/src/sites/<name>

make -C build botanik_sim     # run the emulator on the site
make -C build check           # the full gate
make -C build vendor          # emit the sketch tree (see quickstart)
```

CMake resolves either form to absolute `BOTANIK_SITE_HEADER` / `BOTANIK_SITE_CONFIG`
defines rather than adding an include directory, so a flat site directory cannot
shadow a library or Mbed header.

## 11. Checklist for a generated site

- [ ] Payload asserts present: both `% 4 == 0`, both `<= PAYLOAD_OFFSET_MAX`.
- [ ] One `uint8_t` alarm boolean per alarm row, in the same order as `alarms.h`.
- [ ] Every group in `params.h` has a same-named `SCALAR_U32` commit serial.
- [ ] Status rows all have `in_offset == -1`; parameter rows all have `>= 0`.
- [ ] `SCALAR_ENUM` rows point at a real `str`; every other row passes `nullptr`.
- [ ] `SCALAR_STRING` rows set `len`; every other row sets `0`.
- [ ] Capacity `static_assert`s present in `status.h`, `params.h`, `alarms.h`.
- [ ] No site file includes `libbotanik/...` directly except `botanik.h`.
- [ ] Everything but the payload types is inside `namespace Site`.
- [ ] `make -C build check` green, and the sim boots with the site's data moving.

## 12. Verifying against the PLC program

The one check that catches a regenerated PLC program drifting from a regenerated
site. Put it in the integrator's entry file, where the PLC IDE's types are in
scope:

```cpp
static_assert(sizeof(PLCSharedVarsInput_t)   == sizeof(Botanik::PlcIO::PlcInput<PlcInputData_t>), "");
static_assert(sizeof(PLCSharedVarsOutput_t)  == sizeof(Botanik::PlcIO::PlcOutput<SiteData_t>), "");
static_assert(alignof(PLCSharedVarsOutput_t) == alignof(Botanik::PlcIO::PlcOutput<SiteData_t>), "");
```
