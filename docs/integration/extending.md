# Extension hooks {#int_extending}

A site never overrides `setup()` or `loop()` - both are non-virtual, so the fixed
bring-up and cycle order cannot be skipped or reordered. Customisation goes through
a set of protected virtual hooks.

| Hook | Gate | Called | Default |
|---|---|---|---|
| `register_vars()` | `FEATURE_VARS` | at the top of `setup()`, after the bank has taken the generated table | nothing; a site with a generated table needs none |
| `register_commands()` | `FEATURE_SHELL_PAGE` | in `setup()`, after the built-ins | registers the descriptor's command table with the live `Vars::Bank*` as `ctx` |
| `register_pages()` | `FEATURE_CONSOLE` | in `setup()`, before the terminal handshake | registers home / shell / logs / alarms / params / monitor / tasks / notes, in that order, each under its own flag |
| `register_telemetry()` | `FEATURE_MQTT` | in `setup()`, after the MQTT sinks are wired | publishes the descriptor's status groups |
| `on_setup()` | always | once, near the end of `setup()` - **before** the setup pass, the watchdog and control being enabled | nothing |
| `on_loop(now_ms)` | always | after every phase, before the watchdog kick | nothing |

One of them is what a site normally writes: `on_setup()`, where components of
its own register. The reference site overrides none: it is DATA, and its laws are
ST that the base's resource dispatches.

There is no per-phase hook on the firmware. Work that has to happen at a point in
the beat is a `Component` (@ref dev_architecture) or a `Plc::Program`
(@ref int_tasks), registered in `on_setup()` - so it gets a name, a rate group, a
phase and a row on the TASKS page, which a hook on the base class would not.

## Declaring the site's rows

By pointing the descriptor at the generated table. There is no call to write:

```cpp
// generated/vars.gen.cpp
const Botanik::SiteDescriptor g_site = {
    ...
    rows,          row_order, ROWS_COUNT, PARAM_FIRST, PARAM_COUNT,
};
```

The generator merges the library's rows into the site's, orders them - parameters
first and contiguous - and emits the bounds beside the table, so the ordering rule
holds by construction rather than being checked at bring-up. `register_vars()` is
left as the hook for a site whose rows are not one generated array.

## Adding shell commands

Declare the table (see @ref int_variables) and put it in the descriptor. No hook needed
- the default `register_commands()` picks it up.

Override the hook only to pass a different `ctx`:

```cpp
void register_commands() override {
    shell().set_commands(g_commands, g_command_count, &my_context);
}
```

## Adding a page

`register_pages()` is the tab order: the `add_page()` call order **is** the order,
and page 0 is the page shown at boot. The ceiling is `CONSOLE_PAGES_MAX` (10), and
the full default profile registers nine, so there is room for one more before a site
has to drop one.

```cpp
class GreenhouseXXX final : public Botanik::BotanikFirmware {
    MyPage _mine;

public:
    GreenhouseXXX() : Botanik::BotanikFirmware(Site::g_site),
                      _mine(terminal()) {}

protected:
    void register_pages() override {
        BotanikFirmware::register_pages();      // keep the built-in set
        terminal().add_page(&_mine);            // append your own
    }
};
```

Call the base first to keep the built-ins, or don't to replace the set entirely.
Reordering is just changing the call order; `add_page()` past the ceiling is refused.

### A site's own SYSTEM page

`Pages::StatusPage` is the SYSTEM page, and the base registers it. It is also the
intended base for a site health screen: subclass it, fill the `draw_payload_*` hooks
(one per cadence - static, dynamic, hot), and register **that** in place of the base
one rather than appending a second copy.

```cpp
class MySystemPage final : public Botanik::Pages::StatusPage {
public:
    MySystemPage(Botanik::Terminal::Console& c,
                 const Botanik::SystemContext* sys)
        : StatusPage(c, sys) {}

protected:
    void draw_payload_static()  override { _label_at(payload_row(), 2, "Boiler"); }
    void draw_payload_dynamic() override { /* the cold cells */ }
};
```

The `SystemContext*` comes from `system_context()`; nothing else hands it out. Its
first payload row is `StatusPage::PAYLOAD_ROW`, which is derived from what is
compiled in - the network and bus rows are feature-gated, so do not hardcode it.

Put it in the SYSTEM slot by overriding `system_page()` rather than
`register_pages()`. The base then registers yours and never its own, so the site
gets the slot instead of a second copy of the page:

```cpp
MySystemPage _system{terminal(), &system_context()};

Botanik::Pages::StatusPage& system_page() override { return _system; }
```

To write a page from scratch, implement `Pages::Page` - see @ref dev_pages for the
interface and the render contract.

## Post-wiring

`on_setup()` runs once, after every library component is constructed and wired but
**before** the setup pass, the hardware watchdog and control authority. That
ordering is what makes it safe: nothing you do here can energise an actuator on the
way past, and anything it registers is a task-table entry like any other.

It is where a site's control programs go in, and the call order **is** the
execution order within `PHASE_CONTROL` - so a law computing a setpoint registers
before the law acting on it:

```cpp
void on_setup() override {
    _target.register_tasks();       // decides the setpoint
    _cooling.register_tasks();      // reads it
    _windows.register_tasks();      // carries it out
}
```

Site-owned components register the same way: a driver, anything with state of
its own. Each overrides the `Component` phase hook whose
place in the data flow matches its work.

| Phase | Runs | For |
|---|---|---|
| `on_input(now)` | after the field commit and the expiry sweep | a driver poll; readings are in and coherent |
| `on_config(now)` | after the parameter commit | re-reading configuration the rest of the beat will use |
| `on_service(now)` | beside health and the network | a background state machine |
| `on_control(now)` | beside the control programs, **gated** | a control law that is not a `Plc::Program` |
| `on_output(now)` | after the programs, before the field write | shaping what goes out |
| `on_present(now)` | with the beat's decisions made | telemetry, counters, reporting |

**Only `PHASE_CONTROL` is gated by control authority.** The other five run in every
mode, which is what keeps a stopped plant diagnosable - bookkeeping, derived values
and telemetry stay correct while nothing is being driven. Anything that DRIVES the
field must be in the control phase, or it keeps driving through a health fault.

@warning Every hook must be **non-blocking and bounded**, like everything else in
the loop. Blocking here stalls the RTOS and trips the hardware watchdog.

`on_loop()` is the escape hatch and the last resort: it runs outside the task
table, so it has no phase, no rate, no cost sample and no TASKS row. A slow one is
invisible.
