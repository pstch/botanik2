#!/usr/bin/env bash
# Run botanik_sim in fast mode on one named scenario. Needs a real terminal: the
# sim puts stdin into raw mode and bails without one.
#
#   scripts/sim_scenario.sh <scenario> [when]
#   scripts/sim_scenario.sh list
#
#   scenario  one of the names below                       (required)
#   when      2026-07-01T18:30:00Z   a full instant
#             18:30Z                 that time on the default day
#             2026-07-01             midnight on that day
#             omitted                the hour this scenario has something to show
#
# Speed is BOTANIK_SIM_SPEED, simulated seconds per real second, 0 for flat out.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${BOTANIK_SIM:-$HERE/../build/botanik_sim}"
SIM_H="$HERE/../examples/ref/src/sim.h"
DAY="${BOTANIK_SIM_DAY:-2026-07-01}"
SPEED="${BOTANIK_SIM_SPEED:-60}"

TABLE="$HERE/scenarios.tsv"
[ -r "$TABLE" ] || { echo "no scenario table at $TABLE" >&2; exit 2; }

# $1 name, $2 hour|what
field() {
    awk -F'\t' -v n="$1" -v c="$2" \
        '$1 !~ /^#/ && $1 == n { print (c == "hour" ? $2 : $3); found = 1 }
         END { exit !found }' "$TABLE"
}

if [ $# -eq 0 ] || [ "${1:-}" = list ] || [ "${1:-}" = -h ] || [ "${1:-}" = --help ]; then
    printf '%-8s %-6s %s\n' SCENARIO START 'WHAT IT SHOWS'
    awk -F'\t' '$1 !~ /^#/ { printf "%-8s %-6s %s\n", $1, $2, $3 }' "$TABLE"
    exit 0
fi

want="$1"; when="${2:-}"

# The names are sim.h's, so a scenario added there and not here is caught rather
# than silently falling back to `cycle`, which is what the sim itself does.
if [ -r "$SIM_H" ]; then
    declared=$(sed -n '/scenario_names()/,/};/p' "$SIM_H" | grep -oE '"[a-z]+"' | tr -d '"')
    for n in $declared; do
        field "$n" hour >/dev/null || echo "warning: sim.h declares '$n', this script does not" >&2
    done
fi

start_hour=$(field "$want" hour) || {
    echo "unknown scenario '$want' - try: $0 list" >&2; exit 2; }
[ -x "$BIN" ] || { echo "no sim at $BIN (build it, or set BOTANIK_SIM)" >&2; exit 2; }

case "$when" in
    "")        stamp="$DAY $start_hour" ;;               # the scenario's own hour
    *T*)       stamp="${when}" ;;                        # a full instant
    *:*)       stamp="$DAY ${when%Z}" ;;                 # a time on the default day
    *)         stamp="${when} 00:00" ;;                  # a bare day
esac
epoch=$(date -u -d "$stamp" +%s 2>/dev/null) || {
    echo "cannot read '$when' as a time (try 2026-07-01T18:30:00Z or 18:30Z)" >&2
    exit 2; }

# stderr carries the mock network's chatter and would scribble over the console.
log="${BOTANIK_SIM_LOG:-/tmp/botanik-sim-$want.log}"
echo "$want from $(date -u -d "@$epoch" '+%Y-%m-%dT%H:%M:%SZ'), at ${SPEED}x real time."
echo "$(field "$want" what)"
echo "diagnostics: $log   Ctrl-C to stop."
exec env \
    BOTANIK_SIM_CLOCK=fast \
    BOTANIK_SIM_SPEED="$SPEED" \
    BOTANIK_SIM_SCENARIO="$want" \
    BOTANIK_SIM_EPOCH="$epoch" \
    "$BIN" 2>"$log"
