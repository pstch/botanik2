# Extension hooks {#int_extending}

A site never overrides `setup()` or `loop()` - both are non-virtual, so the fixed
bring-up and cycle order cannot be skipped or reordered. Customisation goes through
a set of protected virtual hooks.

| Hook | Gate | Called | Default |
|---|---|---|---|
| `register_commands()` | `FEATURE_CONSOLE` | in `setup()`, before `register_pages()` | registers the descriptor's command table with the live `Vars::Bank*` as `ctx` |
| `register_pages()` | `FEATURE_CONSOLE` | in `setup()`, before the terminal handshake | registers home / shell / logs / alarms / params / monitor / tasks / notes, in that order (notes under `FEATURE_NOTES`) |
| `register_telemetry()` | `FEATURE_MQTT` | in `setup()`, after the MQTT sinks are wired | publishes the descriptor's status groups |
| `on_setup()` | always | once, near the end of `setup()` - **before** `tasks.setup()`, the watchdog and control being enabled | nothing |
| `on_input()` / `on_config()` / `on_service()` / `on_output()` / `on_present()` | always | one task each (`site_input`, ...) at the end of that phase | nothing |
| `on_control(now_ms)` | control authority | the `site_control` task, beside the scan driver | nothing |

Most sites override none of them. The reference site overrides none.

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

## Per-beat logic

There is one hook per phase of the beat, and you override the one whose place in
the data flow matches the work:

| Hook | Runs | For |
|---|---|---|
| `on_input(now)` | after the field commit and the expiry sweep | a driver poll; readings are in and coherent |
| `on_config(now)` | after the parameter commit | re-reading configuration the rest of the beat will use |
| `on_service(now)` | beside health and the network | a site's own background state machine |
| `on_control(now)` | beside the scan driver, **gated** | the site's own control law |
| `on_output(now)` | after the programs, before the field write | shaping what goes out |
| `on_present(now)` | with the beat's decisions made | telemetry, counters, reporting |

```cpp
void on_input(uint64_t now_ms) override {
    _my_driver.step(now_ms);
}
```

Each is registered as a task of its own (`site_input`, `site_config`, ...), so a
slow one is attributable on the TASKS page instead of hiding inside the base.

**Only `on_control()` is gated by control authority.** The other five run in every
mode, which is what keeps a stopped plant diagnosable - bookkeeping, derived
values and telemetry stay correct while nothing is being driven. Anything that
DRIVES the field must go in `on_control()`, or it keeps driving through a health
fault.

@warning Every one of them must be **non-blocking and bounded**, like everything
else in the loop. Blocking here stalls the RTOS and trips the hardware watchdog.

Control logic belongs in a `Plc::Program` instead (@ref int_tasks): programs get
multi-rate scheduling, correct `dt_ms` handling, their variable names resolved once
at bring-up, and are reset and reseeded across a control-authority edge. The hooks
carry no scheduling state of their own.

## Post-wiring

`on_setup()` runs once, after every component is constructed and wired but **before**
the hardware watchdog is armed and before control authority is enabled. That ordering
is what makes it safe: nothing you do here can energise an actuator on the way past.

Use it to register extra log sinks, construct site-owned components, or seed state.
Components a site owns outright - the solar-position tracker, for instance - are
constructed as members and stepped from whichever phase hook fits.
