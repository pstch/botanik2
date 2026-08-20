# Extension hooks {#int_extending}

A site never overrides `setup()` or `loop()` - both are non-virtual, so the fixed
bring-up and cycle order cannot be skipped or reordered. Customisation goes through
a set of protected virtual hooks.

| Hook | Gate | Called | Default |
|---|---|---|---|
| `register_vars()` | `FEATURE_VARS` | at the top of `setup()`, before anything reads the bank | nothing; a site calls the generated `Site::register_all()` |
| `register_commands()` | `FEATURE_SHELL_PAGE` | in `setup()`, after the built-ins | registers the descriptor's command table with the live `Vars::Bank*` as `ctx` |
| `register_pages()` | `FEATURE_CONSOLE` | in `setup()`, before the terminal handshake | registers home / shell / logs / alarms / params / monitor / tasks / notes, in that order, each under its own flag |
| `register_telemetry()` | `FEATURE_MQTT` | in `setup()`, after the MQTT sinks are wired | publishes the descriptor's status groups |
| `on_setup()` | always | once, near the end of `setup()` - **before** the setup pass, the watchdog and control being enabled | nothing |
| `on_loop(now_ms)` | always | after every phase, before the watchdog kick | nothing |

Two of them are what a site normally writes: `register_vars()`, one line, and
`on_setup()`, where its control programs register. The reference site overrides
those two and nothing else.

There is no per-phase hook on the firmware. Work that has to happen at a point in
the beat is a `Component` (@ref dev_architecture) or a `Plc::Program`
(@ref int_tasks), registered in `on_setup()` - so it gets a name, a rate group, a
phase and a row on the TASKS page, which a hook on the base class would not.

## Declaring the site's rows

```cpp
void register_vars() override { Site::register_all(io()); }
```

`register_all()` is generated from the CSV tables and holds the one ordering rule
the bank checks: the parameters come first, the library's already at the front of
a run that has to stay contiguous. Splitting the call up is allowed, getting that
order wrong is not - `Bank::freeze()` names the row that broke it.

## Adding shell commands

Declare the table (see @ref int_tables) and put it in the descriptor. No hook needed
- the default `register_commands()` picks it up.

Override the hook only to pass a different `ctx`:

```cpp
void register_commands() override {
    _console.shell().set_commands(g_commands, g_command_count, &my_context);
}
```

## Adding a page

`register_pages()` is the tab order: the `add_page()` call order **is** the order,
and page 0 is the page shown at boot. Up to 8 pages - which the default set now
fills, so appending one means dropping one, or not calling the base at all.

```cpp
class GreenhouseXXX final : public Botanik::BotanikFirmware {
    MyStatusPage _status{*this};

public:
    GreenhouseXXX() : Botanik::BotanikFirmware(Site::g_site) {}

protected:
    void register_pages() override {
        BotanikFirmware::register_pages();      // keep the built-in set
        terminal().add_page(&_status);          // append your own
    }
};
```

Call the base first to keep the built-ins, or don't to replace the set entirely.
Reordering is just changing the call order. `add_page()` past the eighth is refused,
so a site adding its own generally overrides the whole method rather than appending.

`Pages::StatusPage<T>` is a ready-made two-column health grid, shipped but **not**
registered by default. Re-add it by subclassing it and appending it here. To write a
page from scratch, implement `Pages::Page` - see @ref dev_pages for the interface and
the render contract.

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

Site-owned components register the same way: a driver, a solar-position tracker,
anything with state of its own. Each overrides the `Component` phase hook whose
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
