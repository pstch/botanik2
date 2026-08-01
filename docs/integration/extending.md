# Extension hooks {#int_extending}

A site never overrides `setup()` or `loop()` - both are non-virtual, so the fixed
bring-up and cycle order cannot be skipped or reordered. Customisation goes through
five protected virtual hooks.

| Hook | Gate | Called | Default |
|---|---|---|---|
| `register_commands()` | `FEATURE_CONSOLE` | in `setup()`, before `register_pages()` | registers the descriptor's command table with the live `Vars::Bank*` as `ctx` |
| `register_pages()` | `FEATURE_CONSOLE` | in `setup()`, before the terminal handshake | registers home / shell / logs / alarms / params / monitor, in that order |
| `register_telemetry()` | `FEATURE_MQTT` | in `setup()`, after the MQTT sinks are wired | publishes the descriptor's status groups |
| `on_setup()` | always | once, near the end of `setup()` - **before** the watchdog is armed and control is enabled | nothing |
| `on_step(now_ms)` | always | every beat, after the scan and before the logger drains | nothing |

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
and page 0 is the page shown at boot. Up to 8 pages.

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
Reordering is just changing the call order.

`Pages::StatusPage<T>` is a ready-made two-column health grid, shipped but **not**
registered by default. Re-add it by subclassing it and appending it here. To write a
page from scratch, implement `Pages::Page` - see @ref dev_pages for the interface and
the render contract.

## Per-beat logic

`on_step(now_ms)` runs every beat with fresh variable data. Use it for work that is
not a control program - a driver poll, a site-specific supervisor.

```cpp
void on_step(uint64_t now_ms) override {
    _my_driver.step(now_ms);
}
```

@warning It must be **non-blocking and bounded**, like everything else in the loop.
Blocking here stalls the RTOS and trips the hardware watchdog.

Control logic belongs in a `Scan::Task` instead (@ref int_tasks): tasks get
multi-rate scheduling, correct `dt_ms` handling, and are inhibited automatically when
control authority is withdrawn. `on_step()` gets none of that.

## Post-wiring

`on_setup()` runs once, after every component is constructed and wired but **before**
the hardware watchdog is armed and before control authority is enabled. That ordering
is what makes it safe: nothing you do here can energise an actuator on the way past.

Use it to register extra log sinks, construct site-owned components, or seed state.
Components a site owns outright - the solar-position tracker, for instance - are
constructed as members and stepped from `on_step()`.
