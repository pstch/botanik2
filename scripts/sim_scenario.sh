#!/usr/bin/env bash
# Run botanik_sim in fast mode on one named scenario, starting at the hour that
# scenario has something to show. Needs a real terminal: the sim puts stdin into
# raw mode and bails without one.
#
#   scripts/sim_scenario.sh <scenario> [speed] [date]
#
#   scenario  fair hot cold storm damp dry probes supply cycle   (default cycle)
#   speed     simulated seconds per real second; 0 is flat out   (default 60)
#   date      the day to run, YYYY-MM-DD                         (default 2026-07-01)
#
# The start HOUR is per scenario, so a fault window is minutes away rather than
# most of a simulated day. `list` prints them.
set -euo pipefail

BIN="${BOTANIK_SIM:-$(dirname "$0")/../build/botanik_sim}"

# scenario:hour:what to watch for
ROWS=(
  "fair:06:00:an ordinary day: dawn, the roof opening, an irrigation cycle"
  "hot:11:00:cli_tooHot_degC, the whole cooling ladder, the cold bank it arms"
  "cold:02:00:cli_tooCold_degC, the screen deployed, the roof shut"
  "storm:11:00:wind over cli_windShut_mps: the roof lockout, and the rot guard against it"
  "damp:02:00:air at saturation: alarm_rot, and the skirt refusing to add water"
  "dry:11:00:the humidifier arm of the ladder"
  "probes:03:50:04:00 one ridge probe dies, 05:00 a second: the quorum, then not"
  "supply:08:50:09:00 a sag, 13:00 the grid gone and the generator on, 14:00 the UPS"
  "cycle:06:00:a different one of the above each simulated day"
)

if [ "${1:-}" = "list" ] || [ $# -eq 0 ]; then
    printf '%-8s %-6s %s\n' SCENARIO START 'WHAT IT SHOWS'
    for r in "${ROWS[@]}"; do
        IFS=: read -r name hh mm rest <<< "$r"
        printf '%-8s %-6s %s\n' "$name" "$hh:$mm" "$rest"
    done
    exit 0
fi

want="$1"; speed="${2:-60}"; day="${3:-2026-07-01}"
start=""
for r in "${ROWS[@]}"; do
    IFS=: read -r name hh mm rest <<< "$r"
    [ "$name" = "$want" ] && start="$hh:$mm"
done
if [ -z "$start" ]; then
    echo "unknown scenario '$want' - try: $0 list" >&2
    exit 2
fi
[ -x "$BIN" ] || { echo "no sim at $BIN (build it, or set BOTANIK_SIM)" >&2; exit 2; }

# stderr carries the mock network's chatter and would scribble over the console,
# so it goes to a file rather than the screen.
log="${BOTANIK_SIM_LOG:-/tmp/botanik-sim-$want.log}"
echo "$want from $day $start UTC, at ${speed}x real time."
echo "diagnostics: $log   Ctrl-C to stop."
exec env \
    BOTANIK_SIM_CLOCK=fast \
    BOTANIK_SIM_SPEED="$speed" \
    BOTANIK_SIM_SCENARIO="$want" \
    BOTANIK_SIM_EPOCH="$(date -u -d "$day $start" +%s)" \
    "$BIN" 2>"$log"
