# Contexts

Subsystems don't call into each other to render status - each publishes a small,
plain **context** struct, and the read-only consumers (the row-0 status bar, the
`stats` command, the metric pages, the health monitor) read those. The contexts
are borrowed pointers: a consumer holds a pointer it does not own, and every one
may be null when its subsystem is absent at runtime (compiled in only under its
matching `BOTANIK_FEATURE_*`).

## SystemContext - the render-path aggregate

@ref Botanik::SystemContext gathers the per-subsystem status structs the UI reads
into one borrowed-pointer aggregate, so the terminal/pages take a single handle
instead of a pointer per subsystem. Every member may be null; each is compiled in
only under its matching feature flag.

\startuml "SystemContext: the borrowed per-subsystem status pointers the UI reads, and the two status structs it dereferences most"
class SystemContext {
  scheduler : SchedulerContext*
  plc : PlcSyncContext* (FEATURE_PLC)
  network : const NetworkHandler* (FEATURE_NETWORK)
  ntp : const NtpClient* (FEATURE_NTP)
  mqtt : const MqttClient* (FEATURE_MQTT)
}

class SchedulerContext {
  cpu_load_ema : float
  cpu_jitter_ema : float
  stack_usage : float
  stack_bytes_free : uint32
  ticks_per_second : uint32
  overruns : uint32
  timestamp_ms : uint32
}

class PlcSyncContext {
  status() : PlcStatus
  faulted() : bool
  torn_count() : uint32
  last_ack_age_ms() : uint32
}

SystemContext o-- SchedulerContext : scheduler
SystemContext o-- PlcSyncContext : plc
\enduml

@ref Botanik::SystemContext::set_context lets the owner wire the aggregate after
construction, once every subsystem's address is known.

## The individual contexts

- @ref Botanik::Scheduler::SchedulerContext - the per-beat performance snapshot
  (`SchedulerContext : PerfContext`), written in place by @ref
  Botanik::Scheduler::LoopScheduler and read via `context()`. See the card in
  @ref md_docs_2SCHEDULER__PERF "Scheduler & performance".
- @ref Botanik::PlcIO::PlcSyncContext - the PLC link status derived from the
  cross-core ack timing, owned by @ref Botanik::PlcIO::PlcSyncHandler. The
  handshake and state machine are in @ref md_docs_2PLC__SYNC "PLC sync".
- The network contexts - @ref Botanik::Network::NetworkHandler,
  @ref Botanik::Network::NtpClient, @ref Botanik::Network::MqttClient - are the
  live components themselves, read for their status/IP/sync fields (each present
  only under `FEATURE_NETWORK` / `_NTP` / `_MQTT`).
